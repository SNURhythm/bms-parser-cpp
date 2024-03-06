#include "../src/BMSParser.h"
#include "../src/Chart.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>
#include <functional>
#include "sqlite3.h"
#include <sys/stat.h>
#include <codecvt>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
void parallel_for(int n, std::function<void(int start, int end)> f)
{
  unsigned int nThreads = std::thread::hardware_concurrency();
  nThreads = nThreads == 0 ? 1 : nThreads;
  unsigned int batchSize = n / nThreads;
  unsigned int remainder = n % nThreads;
  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < nThreads; i++)
  {
    unsigned int start = i * batchSize;
    unsigned int end = start + batchSize;
    if (i == nThreads - 1)
    {
      end += remainder;
    }
    threads.push_back(std::thread(f, start, end));
  }
  for (auto &t : threads)
  {
    t.join();
  }
}

std::string ws2s(const std::wstring &wstr)
{
  return std::string().assign(wstr.begin(), wstr.end());
}

std::string ws2s_utf8(const std::wstring &wstr)
{
  using convert_typeX = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_typeX, wchar_t> converterX;

  return converterX.to_bytes(wstr);
}

void parse_single_metadata(const std::filesystem::path &bmsFile)
{
  std::wstring wpath = bmsFile.wstring();
  BMSParser parser;
  BMSChart *chart;
  std::atomic_bool cancel = false;
  std::cout << "Parsing..." << std::endl;
  parser.Parse(wpath, &chart, false, true, cancel);
  std::cout << "BmsPath:" << std::filesystem::path(chart->Meta.BmsPath).string() << std::endl;
  std::cout << "MD5: " << chart->Meta.MD5 << std::endl;
  std::cout << "SHA256: " << chart->Meta.SHA256 << std::endl;
  std::cout << "Title: " << ws2s(chart->Meta.Title) << std::endl;
  std::cout << "SubTitle: " << ws2s(chart->Meta.SubTitle) << std::endl;
  std::cout << "Artist: " << ws2s(chart->Meta.Artist) << std::endl;
  std::cout << "SubArtist: " << ws2s(chart->Meta.SubArtist) << std::endl;
  std::cout << "Genre: " << ws2s(chart->Meta.Genre) << std::endl;
  std::cout << "PlayLevel: " << chart->Meta.PlayLevel << std::endl;
  std::cout << "Total: " << chart->Meta.Total << std::endl;
  std::cout << "StageFile: " << ws2s_utf8(chart->Meta.StageFile) << std::endl;
  std::cout << "Bpm: " << chart->Meta.MinBpm << "~" << chart->Meta.MaxBpm << " (" << chart->Meta.Bpm << ")" << std::endl;
  std::cout << "Rank: " << chart->Meta.Rank << std::endl;
  std::cout << "TotalNotes: " << chart->Meta.TotalNotes << std::endl;
  std::cout << "Difficulty: " << chart->Meta.Difficulty << std::endl;
  std::cout << "TotalLength: " << chart->Meta.TotalLength << std::endl;
  std::cout << "PlayLength: " << chart->Meta.PlayLength << std::endl;
}
enum DiffType
{
  Deleted,
  Added
};
struct Diff
{
  std::filesystem::path path;
  DiffType type;
};

#ifdef _WIN32
void findFilesWin(const std::wstring &directoryPath, std::vector<Diff> &diffs, const std::unordered_set<std::wstring> &oldFiles, std::vector<std::wstring> &directoriesToVisit)
{
  WIN32_FIND_DATAW findFileData;
  HANDLE hFind = FindFirstFileW((directoryPath + L"\\*.*").c_str(), &findFileData);

  if (hFind != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      {
        std::wstring filename(findFileData.cFileName);

        if (filename.size() > 4)
        {
          std::wstring ext = filename.substr(filename.size() - 4);
          std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
          if (ext == L".bms" || ext == L".bme" || ext == L".bml")
          {
            std::wstring dirPath;

            std::wstring fullPath = directoryPath + L"\\" + filename;
            if (oldFiles.find(fullPath) == oldFiles.end())
            {
              diffs.push_back({fullPath, Added});
            }
          }
        }
      }
      else if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
        std::wstring filename(findFileData.cFileName);

        if (filename != L"." && filename != L"..")
        {
          directoriesToVisit.push_back(directoryPath + L"\\" + filename);
        }
      }
    } while (FindNextFileW(hFind, &findFileData) != 0);
    FindClose(hFind);
  }
}
#else
// TODO: Use platform-specific method for faster traversal
void findFilesUnix(const std::filesystem::path &directoryPath, std::vector<Diff> &diffs, const std::unordered_set<std::filesystem::path> &oldFiles, std::vector<std::filesystem::path> &directoriesToVisit)
{
  DIR *dir = opendir(directoryPath.c_str());
  if (dir)
  {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
      if (entry->d_type == DT_REG)
      {
        std::string filename = entry->d_name;
        if (filename.size() > 4)
        {
          std::string ext = filename.substr(filename.size() - 4);
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          if (ext == ".bms" || ext == ".bme" || ext == ".bml")
          {
            std::filesystem::path fullPath = directoryPath / filename;
            if (oldFiles.find(fullPath) == oldFiles.end())
            {
              diffs.push_back({fullPath, Added});
            }
          }
        }
      }
      else if (entry->d_type == DT_DIR)
      {
        std::string filename = entry->d_name;
        if (filename != "." && filename != "..")
        {
          directoriesToVisit.push_back(directoryPath / filename);
        }
      }
    }
    closedir(dir);
  }
}
#endif

void find_new_bms_files(std::vector<Diff> &diffs, const std::unordered_set<std::filesystem::path> &oldFiles, const std::filesystem::path &path)
{
#ifdef _WIN32
  std::vector<std::wstring> directoriesToVisit;
  directoriesToVisit.push_back(path.wstring());
  std::unordered_set<std::wstring> oldFilesWs;
  for (auto &f : oldFiles)
  {
    oldFilesWs.insert(f.wstring());
  }

#else
  std::vector<std::filesystem::path> directoriesToVisit;
  directoriesToVisit.push_back(path);
#endif

  while (!directoriesToVisit.empty())
  {
    std::filesystem::path currentDir = directoriesToVisit.back();
    directoriesToVisit.pop_back();

#ifdef _WIN32

    findFilesWin(currentDir, diffs, oldFilesWs, directoriesToVisit);
#else
    findFilesUnix(currentDir, diffs, oldFiles, directoriesToVisit);
#endif
  }
}
bool construct_folder_db(const std::filesystem::path &path)
{
  sqlite3 *db;
  char *zErrMsg = 0;
  int rc;
  rc = sqlite3_open("bms.db", &db);
  if (rc)
  {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    return false;
  }
  auto query = "CREATE TABLE IF NOT EXISTS chart_meta ("
               "path       TEXT primary key,"
               "md5        TEXT not null,"
               "sha256     TEXT not null,"
               "title      TEXT,"
               "subtitle   TEXT,"
               "genre      TEXT,"
               "artist     TEXT,"
               "sub_artist  TEXT,"
               "folder     TEXT,"
               "stage_file  TEXT,"
               "banner     TEXT,"
               "back_bmp    TEXT,"
               "preview    TEXT,"
               "level      REAL,"
               "difficulty INTEGER,"
               "total     REAL,"
               "bpm       REAL,"
               "max_bpm     REAL,"
               "min_bpm     REAL,"
               "length     INTEGER,"
               "rank      INTEGER,"
               "player    INTEGER,"
               "keys     INTEGER,"
               "total_notes INTEGER,"
               "total_long_notes INTEGER,"
               "total_scratch_notes INTEGER,"
               "total_backspin_notes INTEGER"
               ")";
  char *errMsg;
  rc = sqlite3_exec(db, query, nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
    return false;
  }
  // search for bms files
  std::unordered_set<std::filesystem::path> oldFiles;
  sqlite3_stmt *stmt;
  rc = sqlite3_prepare_v2(db, "SELECT path FROM chart_meta", -1, &stmt, nullptr);
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    oldFiles.insert(std::filesystem::path(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))));
  }
  sqlite3_finalize(stmt);
  std::vector<Diff> diffs;
  std::cout << "Finding new bms files" << std::endl;
  find_new_bms_files(diffs, oldFiles, path);
  std::cout << "Found " << diffs.size() << " new bms files" << std::endl;
  for (auto &diff : diffs)
  {
    std::wcout << diff.path << L" " << diff.type << std::endl;
  }
  std::atomic_bool is_committing = false;
  std::atomic_int success_count = 0; // commit every 100 files
  sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);

  parallel_for(diffs.size(), [&](int start, int end)
               {
    for(int i = start; i < end; i++){
      if(diffs[i].type == Added){
        BMSParser parser;
        BMSChart *chart;
        std::atomic_bool cancel = false;
        try{
          parser.Parse(diffs[i].path.wstring(), &chart, false, true, cancel);
          // std::cout << "Title: " << ws2s(chart->Meta.Title) << std::endl;
          // std::cout << "SubTitle: " << ws2s(chart->Meta.SubTitle) << std::endl;
          // std::cout << "Artist: " << ws2s(chart->Meta.Artist) << std::endl;
        }catch(std::exception &e){
          delete chart;
          std::wcerr << "Error parsing " << diffs[i].path << ": " << e.what() << std::endl;
          continue;
        }
        if(chart == nullptr){
          continue;
        }
        ++success_count;
        if(success_count % 100 == 0){
          is_committing = true;
          sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
          sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
          is_committing = false;
        }
        auto query = "REPLACE INTO chart_meta ("
            "path,"
            "md5,"
            "sha256,"
            "title,"
            "subtitle,"
            "genre,"
            "artist,"
            "sub_artist,"
            "folder,"
            "stage_file,"
            "banner,"
            "back_bmp,"
            "preview,"
            "level,"
            "difficulty,"
            "total,"
            "bpm,"
            "max_bpm,"
            "min_bpm,"
            "length,"
            "rank,"
            "player,"
            "keys,"
            "total_notes,"
            "total_long_notes,"
            "total_scratch_notes,"
            "total_backspin_notes"
            ") VALUES("
            "@path,"
            "@md5,"
            "@sha256,"
            "@title,"
            "@subtitle,"
            "@genre,"
            "@artist,"
            "@sub_artist,"
            "@folder,"
            "@stage_file,"
            "@banner,"
            "@back_bmp,"
            "@preview,"
            "@level,"
            "@difficulty,"
            "@total,"
            "@bpm,"
            "@max_bpm,"
            "@min_bpm,"
            "@length,"
            "@rank,"
            "@player,"
            "@keys,"
            "@total_notes,"
            "@total_long_notes,"
            "@total_scratch_notes,"
            "@total_backspin_notes"
            ")";

        sqlite3_stmt *stmt;
        rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
          fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
          return;
        }
        sqlite3_bind_text(stmt, 1, diffs[i].path.string().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, (chart->Meta.MD5).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, (chart->Meta.SHA256).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, ws2s(chart->Meta.Title).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, ws2s(chart->Meta.SubTitle).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, ws2s(chart->Meta.Genre).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, ws2s(chart->Meta.Artist).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, ws2s(chart->Meta.SubArtist).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, ws2s_utf8(chart->Meta.Folder).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, ws2s_utf8(chart->Meta.StageFile).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, ws2s_utf8(chart->Meta.Banner).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, ws2s_utf8(chart->Meta.BackBmp).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 13, ws2s_utf8(chart->Meta.Preview).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 14, chart->Meta.PlayLevel);
        sqlite3_bind_int(stmt, 15, chart->Meta.Difficulty);
        sqlite3_bind_double(stmt, 16, chart->Meta.Total);
        sqlite3_bind_double(stmt, 17, chart->Meta.Bpm);
        sqlite3_bind_double(stmt, 18, chart->Meta.MaxBpm);
        sqlite3_bind_double(stmt, 19, chart->Meta.MinBpm);
        sqlite3_bind_int64(stmt, 20, chart->Meta.PlayLength);
        sqlite3_bind_int(stmt, 21, chart->Meta.Rank);
        sqlite3_bind_int(stmt, 22, chart->Meta.Player);
        sqlite3_bind_int(stmt, 23, chart->Meta.KeyMode);
        sqlite3_bind_int(stmt, 24, chart->Meta.TotalNotes);
        sqlite3_bind_int(stmt, 25, chart->Meta.TotalLongNotes);
        sqlite3_bind_int(stmt, 26, chart->Meta.TotalScratchNotes);
        sqlite3_bind_int(stmt, 27, chart->Meta.TotalBackSpinNotes);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
          fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
          delete chart;
          return;
        }
        sqlite3_finalize(stmt);
        delete chart;
      }
    } });

  sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

  sqlite3_close(db);
  return true;
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    std::cout << "Usage: " << argv[0] << " <bms file | folder>" << std::endl;
    return 1;
  }
  bool isFolder = false;
  // check if it's a folder
  struct stat s;
  if (stat(argv[1], &s) == 0)
  {
    if (s.st_mode & S_IFDIR)
    {
      isFolder = true;
    }
  }
  std::cout << "isFolder: " << isFolder << std::endl;
  if (isFolder)
  {
    construct_folder_db(std::filesystem::path(argv[1]));
  }
  else
  {
    parse_single_metadata(std::filesystem::path(argv[1]));
  }
  return 0;
}
