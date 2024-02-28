#include "../src/BMSParser.h"
#include "../src/Chart.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <functional>
#include "sqlite3.h"
#include <sys/stat.h>
#include <codecvt>
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

void parse_single_meta(std::string path)
{
  std::string bmsFile = path;
  BMSParser parser;
  BMSChart *chart;
  std::atomic_bool cancel = false;
  parser.Parse(bmsFile, &chart, false, false, cancel);
  std::cout<<"BmsPath:" <<chart->Meta.BmsPath<<std::endl;
  std::cout << "MD5: " << chart->Meta.MD5 << std::endl;
  std::cout << "SHA256: " << chart->Meta.SHA256 << std::endl;
  std::cout << "Title: " << chart->Meta.Title << std::endl;
  std::cout << "SubTitle: " << chart->Meta.SubTitle << std::endl;
  std::cout << "Artist: " << chart->Meta.Artist << std::endl;
  std::cout << "SubArtist: " << chart->Meta.SubArtist << std::endl;
  std::cout << "Genre: " << chart->Meta.Genre << std::endl;
  std::cout << "PlayLevel: " << chart->Meta.PlayLevel << std::endl;
  std::cout << "Total: " << chart->Meta.Total << std::endl;
  std::cout << "StageFile: " << chart->Meta.StageFile << std::endl;
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
  std::string path;
  DiffType type;
};
#include <filesystem> // Add this include at the top of your file
std::string ws2s(const std::wstring& wstr)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.to_bytes(wstr);
}
#ifdef _WIN32
void findFilesWin(const std::wstring &directoryPath, std::vector<Diff> &diffs, const std::set<std::string> &oldFiles, std::vector<std::string> &directoriesToVisit)
{
  WIN32_FIND_DATAW findFileData;
  HANDLE hFind = FindFirstFileW((directoryPath + L"\\*.*").c_str(), &findFileData);

  if (hFind != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      {
        std::wstring ws(findFileData.cFileName);
        std::string filename = ws2s(ws);

        if (filename.size() > 4)
        {
          std::string ext = filename.substr(filename.size() - 4);
          if (ext == ".bms" || ext == ".bme" || ext == ".bml")
          {
            std::string dirPath;
            
            std::string fullPath = ws2s(directoryPath) + "\\" + filename;
            if (oldFiles.find(fullPath) == oldFiles.end())
            {
              diffs.push_back({fullPath, Added});
            }
          }
        }
      } else if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
        std::wstring ws(findFileData.cFileName);
        std::string filename(ws.begin(), ws.end());
        if (filename != "." && filename != "..")
        {
          directoriesToVisit.push_back(ws2s(directoryPath) + "\\" + filename);
        }
      }
    } while (FindNextFileW(hFind, &findFileData) != 0);
    FindClose(hFind);
  }
}
#else
void findFilesUnix(const std::string &directoryPath, std::vector<Diff> &diffs, const std::set<std::string> &oldFiles, std::vector<std::string> &directoriesToVisit)
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
          if (ext == ".bms" || ext == ".bme" || ext == ".bml")
          {
            std::string fullPath = directoryPath + "/" + filename;
            if (oldFiles.find(fullPath) == oldFiles.end())
            {
              diffs.push_back({fullPath, Added});
            }
          }
        }
      } else if (entry->d_type == DT_DIR)
      {
        std::string filename = entry->d_name;
        if (filename != "." && filename != "..")
        {
          directoriesToVisit.push_back(directoryPath + "/" + filename);
        }
      }
    }
    closedir(dir);
  }
}
#endif

void find_new_bms_files(std::vector<Diff> &diffs, const std::set<std::string> &oldFiles, const std::string path)
{
  std::vector<std::string> directoriesToVisit;
  directoriesToVisit.push_back(path);
  while (!directoriesToVisit.empty())
  {
    std::string currentDir = directoriesToVisit.back();
    directoriesToVisit.pop_back();
#ifdef _WIN32
    std::wstring wPath(currentDir.begin(), currentDir.end());
    findFilesWin(wPath, diffs, oldFiles, directoriesToVisit);
#else
    findFilesUnix(currentDir, diffs, oldFiles, directoriesToVisit);
#endif
  }
}
bool construct_folder_db(std::string path)
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
  std::set<std::string> oldFiles;
  sqlite3_stmt *stmt;
  rc = sqlite3_prepare_v2(db, "SELECT path FROM chart_meta", -1, &stmt, nullptr);
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    oldFiles.insert(std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))));
  }
  sqlite3_finalize(stmt);
  std::vector<Diff> diffs;
  std::cout << "Finding new bms files" << std::endl;
  find_new_bms_files(diffs, oldFiles, path);
  std::cout << "Found " << diffs.size() << " new bms files" << std::endl;
  for (auto &diff : diffs)
  {
    std::cout << diff.path << " " << diff.type << std::endl;
  }

  parallel_for(diffs.size(), [&](int start, int end)
               {
    for(int i = start; i < end; i++){
      if(diffs[i].type == Added){
        BMSParser parser;
        BMSChart *chart;
        std::atomic_bool cancel;
        try{
          parser.Parse(diffs[i].path, &chart, false, false, cancel);
        }catch(std::exception &e){
          delete chart;
          std::cerr << "Error parsing " << diffs[i].path << ": " << e.what() << std::endl;
          continue;
        }
        if(chart == nullptr){
          continue;
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
        sqlite3_bind_text(stmt, 1, diffs[i].path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, chart->Meta.MD5.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, chart->Meta.SHA256.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, chart->Meta.Title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, chart->Meta.SubTitle.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, chart->Meta.Genre.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, chart->Meta.Artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, chart->Meta.SubArtist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, chart->Meta.Folder.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, chart->Meta.StageFile.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, chart->Meta.Banner.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, chart->Meta.BackBmp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 13, chart->Meta.Preview.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 14, chart->Meta.PlayLevel);
        sqlite3_bind_int(stmt, 15, chart->Meta.Difficulty);
        sqlite3_bind_double(stmt, 16, chart->Meta.Total);
        sqlite3_bind_double(stmt, 17, chart->Meta.Bpm);
        sqlite3_bind_double(stmt, 18, chart->Meta.MaxBpm);
        sqlite3_bind_double(stmt, 19, chart->Meta.MinBpm);
        sqlite3_bind_int64(stmt, 20, chart->Meta.TotalLength);
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
    construct_folder_db(std::string(argv[1]));
  }
  else
  {
    parse_single_meta(std::string(argv[1]));
  }
  return 0;
}
