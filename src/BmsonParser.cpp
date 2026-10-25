#include "BmsonParser.h"
#include "LongNote.h"
#include "SHA256.h"
#include "md5.h"
#include "third_party/picojson.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace bms_parser::bmson {
namespace {
namespace json = bms_parser_picojson;
using Value = json::value;
using Tick = std::uint64_t;

const Value &field(const Value &object, const char *name) {
  if (!object.is<json::object>()) throw std::runtime_error("Expected object");
  return object.get(name);
}
const json::array &array(const Value &value) {
  static const json::array empty;
  if (value.is<json::null>()) return empty;
  if (!value.is<json::array>()) throw std::runtime_error("Expected array");
  return value.get<json::array>();
}
double number(const Value &value, double fallback = 0) {
  if (value.is<json::null>()) return fallback;
  if (!value.is<double>() || !std::isfinite(value.get<double>()))
    throw std::runtime_error("Expected finite number");
  return value.get<double>();
}
std::string string(const Value &value, const std::string &fallback = {}) {
  if (value.is<json::null>()) return fallback;
  if (!value.is<std::string>()) throw std::runtime_error("Expected string");
  return value.get<std::string>();
}
std::string resourcePath(const Value &value) {
  auto path = string(value);
  std::replace(path.begin(), path.end(), '\\', '/');
  if (path.find('\0') != std::string::npos || (!path.empty() && path.front() == '/') ||
      path.find(':') != std::string::npos)
    throw std::runtime_error("Invalid resource path");
  std::size_t begin = 0;
  while (begin <= path.size()) {
    const auto end = path.find('/', begin);
    if (path.substr(begin, end == std::string::npos ? end : end - begin) == "..")
      throw std::runtime_error("Parent resource path");
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return path;
}
bool boolean(const Value &value) {
  if (value.is<json::null>()) return false;
  if (!value.is<bool>()) throw std::runtime_error("Expected boolean");
  return value.get<bool>();
}
Tick tick(const Value &value) {
  const auto n = number(value);
  if (n < 0 || n > UINT32_MAX || std::floor(n) != n)
    throw std::runtime_error("Invalid pulse");
  return static_cast<Tick>(n);
}
int id(const Value &value) {
  auto n = tick(value);
  if (n > INT32_MAX) throw std::runtime_error("Resource ID out of range");
  return static_cast<int>(n);
}
int signedInt(const Value &value) {
  const auto n = number(value);
  if (n < INT32_MIN || n > INT32_MAX || std::floor(n) != n)
    throw std::runtime_error("Signed integer out of range");
  return static_cast<int>(n);
}
long long frameTime(const Value &value) {
  if (value.is<int64_t>()) return value.get<int64_t>();
  const auto n = number(value);
  // Integers outside int64 fall back to double in picojson. Exclude both
  // rounded endpoints before conversion; exact limits take the path above.
  if (n <= -9223372036854775808.0 || n >= 9223372036854775808.0 || std::floor(n) != n)
    throw std::runtime_error("Frame time out of range");
  return static_cast<long long>(n);
}
long long micros(long double value) {
  if (!std::isfinite(value) || value < 0 ||
      value >= static_cast<long double>(LLONG_MAX))
    throw std::runtime_error("Timing out of range");
  return static_cast<long long>(value);
}
std::size_t textStart(const std::vector<unsigned char> &bytes) {
  std::size_t i = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
                          bytes[2] == 0xbf ? 3 : 0;
  while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t' ||
                             bytes[i] == '\r' || bytes[i] == '\n')) ++i;
  return i;
}
// Limit recursive JSON nesting before entering the vendored reader, including
// unknown extension fields. Work is linear in source bytes, independent of y.
bool safeJson(const std::vector<unsigned char> &bytes, std::atomic_bool &cancelled) {
  int depth = 0;
  bool quoted = false, escaped = false;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if ((i & 4095) == 0 && cancelled) return false;
    const auto c = bytes[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > 128) return false; }
    else if (c == '}' || c == ']') { if (--depth < 0) return false; }
  }
  return !quoted && depth == 0 && !cancelled;
}
struct Position {
  double bpm = 0;
  double stop = 0;
  double scroll = 1;
  bool scrollChange = false;
  bool bar = false;
  int bga = -1, layer = -1;
  std::optional<BgaEvent> bgaEvent;
  long long time = 0, stopMicros = 0;
  double effectiveBpm = 0, effectiveScroll = 1;
};
struct SoundNote {
  Tick y = 0, length = 0;
  int lane = -1;
  bool continuation = false;
  bool up = false;
  int type = 0;
};
struct Channel {
  int source;
  std::vector<SoundNote> notes;
};
struct AuxiliaryNote {
  int source = -1;
  float damage = 0;
};
struct PlayNote {
  Tick end = 0;
  int wav = -1, type = 0;
};

std::unique_ptr<Chart> decode(const std::vector<unsigned char> &bytes,
                               bool ready, bool metaOnly,
                               std::atomic_bool &cancelled, ChartScanResult *scan) {
  if (cancelled || !safeJson(bytes, cancelled)) return {};
  const auto first = textStart(bytes);
  Value root;
  std::string error;
  auto remaining = json::parse(root, bytes.begin() + first, bytes.end(), &error);
  while (remaining != bytes.end() && (*remaining == ' ' || *remaining == '\t' ||
                                      *remaining == '\r' || *remaining == '\n')) ++remaining;
  if (remaining != bytes.end()) return {};
  if (!error.empty() || !root.is<json::object>() || cancelled) return {};
  const auto &version = field(root, "version");
  const bool hasVersion = root.get<json::object>().count("version") != 0;
  if (hasVersion && !version.is<std::string>()) return {};
  const std::string versionText = string(version);
  const bool legacy = !hasVersion || versionText == "0.21" || versionText == "0.2.1";
  if (!legacy && versionText != "1.0.0" && versionText != "1.0") return {};
  const auto &info = field(root, "info");
  auto chart = std::make_unique<Chart>();
  auto &meta = chart->Meta;
  MD5 digest;
  digest.update(bytes.data(), static_cast<MD5::size_type>(bytes.size()));
  meta.MD5 = digest.finalize().hexdigest();
  meta.SHA256 = sha256(bytes);
  meta.Title = string(field(info, "title"));
  meta.Artist = string(field(info, "artist"));
  meta.Genre = string(field(info, "genre"));
  meta.Bpm = number(field(info, legacy ? "initBPM" : "init_bpm"));
  if (!(meta.Bpm > 0)) return {};
  meta.MinBpm = meta.MaxBpm = meta.MostPrevalentBpm = meta.GuessedBeatBpm = meta.Bpm;
  const auto judgeRank = number(field(info, legacy ? "judgeRank" : "judge_rank"), 100);
  // jbms-parser tolerates old editors that write BMS rank enums (0..4).
  // A negative value leaves the normal BMS rank default intact.
  if (judgeRank >= 0) {
    meta.Rank = static_cast<int>(std::min(judgeRank, 1000000.0));
    meta.RankType = judgeRank < 5 ? JudgeRankType::BmsRank : JudgeRankType::DefExRank;
  }
  meta.Values["bmson.judge_rank"] = std::to_string(judgeRank);
  meta.Total = number(field(info, "total"), 100);
  meta.HasTotal = true;
  meta.PlayLevel = number(field(info, "level"));
  meta.PlayLevelText = std::to_string(static_cast<int>(std::clamp(meta.PlayLevel, 0.0, double(INT32_MAX))));
  meta.KeyMode = 7;
  std::string mode = string(field(info, "mode_hint"), "beat-7k");
  if (mode == "beat-5k") meta.KeyMode = 5;
  else if (mode == "beat-10k") { meta.KeyMode = 10; meta.IsDP = true; }
  else if (mode == "beat-14k") { meta.KeyMode = 14; meta.IsDP = true; }
  else if (mode == "popn-5k" || mode == "popn-9k") meta.KeyMode = 9;
  else if (mode == "keyboard-24k") meta.KeyMode = 24;
  else if (mode == "keyboard-24k-double") { meta.KeyMode = 48; meta.IsDP = true; }
  else if (mode != "beat-7k") return {}; // Never silently reinterpret another game layout.
  meta.Values["bmson.mode_hint"] = mode;
  meta.Player = meta.IsDP ? 3 : 1;
  meta.LnMode = signedInt(field(info, "ln_type"));
  if (meta.LnMode < 0 || meta.LnMode > 3) meta.LnMode = 0;
  const auto lanes = meta.GetTotalLaneIndices();
  const std::set<int> validLanes(lanes.begin(), lanes.end());
  const auto scratchLanes = meta.GetScratchLaneIndices();
  const std::set<int> scratchSet(scratchLanes.begin(), scratchLanes.end());
  const int laneCapacity = std::max(16, *std::max_element(lanes.begin(), lanes.end()) + 1);
  const auto laneFor = [&](const Value &x) {
    if (!x.is<double>()) return -1;
    const auto lane = number(x);
    return lane >= 1 && lane <= laneCapacity && std::floor(lane) == lane &&
        validLanes.count(static_cast<int>(lane) - 1) ? static_cast<int>(lane) - 1 : -1;
  };
  if (!legacy) {
    meta.SubTitle = string(field(info, "subtitle"));
    const auto chartName = string(field(info, "chart_name"));
    if (!chartName.empty()) meta.SubTitle += (meta.SubTitle.empty() ? "" : " ") + std::string("[") + chartName + "]";
    for (const auto &artist : array(field(info, "subartists"))) {
      if (!meta.SubArtist.empty()) meta.SubArtist += ",";
      meta.SubArtist += string(artist);
    }
    meta.Banner = std::filesystem::u8path(resourcePath(field(info, "banner_image")));
    meta.StageFile = std::filesystem::u8path(resourcePath(field(info, "eyecatch_image")));
    meta.BackBmp = std::filesystem::u8path(resourcePath(field(info, "back_image")));
    meta.Preview = std::filesystem::u8path(resourcePath(field(info, "preview_music")));
  }
  double resolution = legacy ? 240 : number(field(info, "resolution"), 240);
  if (resolution <= 0) resolution = 240;
  if (resolution > UINT32_MAX || std::floor(resolution) != resolution) return {};
  const Tick measureTicks = static_cast<Tick>(resolution) * 4;
  std::map<Tick, Position> positions;
  positions[0];
  const auto check = [&]() { if (cancelled) throw std::runtime_error("Cancelled"); };
  for (const auto &event : array(field(root, legacy ? "bpmNotes" : "bpm_events"))) {
    check();
    const auto bpm = number(field(event, legacy ? "v" : "bpm"));
    const auto y = tick(field(event, "y"));
    if (bpm > 0) positions[y].bpm = bpm;
  }
  for (const auto &event : array(field(root, legacy ? "stopNotes" : "stop_events"))) {
    check();
    const auto duration = number(field(event, legacy ? "v" : "duration"));
    const auto y = tick(field(event, "y"));
    if (duration >= 0) positions[y].stop += duration;
  }
  for (const auto &event : array(field(root, "scroll_events"))) {
    auto &pos = positions[tick(field(event, "y"))];
    pos.scroll = number(field(event, "rate"), 1);
    pos.scrollChange = true;
  }
  std::vector<Channel> channels;
  for (const auto &source : array(field(root, legacy ? "soundChannel" : "sound_channels"))) {
    check();
    if (channels.size() >= INT32_MAX - 1) return {};
    Channel channel{static_cast<int>(channels.size()) + 1, {}};
    chart->WavTable[channel.source] = resourcePath(field(source, "name"));
    for (const auto &value : array(field(source, "notes"))) {
      check();
      SoundNote n;
      n.y = tick(field(value, "y"));
      n.length = tick(field(value, "l"));
      n.lane = laneFor(field(value, "x"));
      n.continuation = boolean(field(value, "c"));
      n.up = boolean(field(value, "up"));
      n.type = signedInt(field(value, "t"));
      if (n.type < 0 || n.type > 3) n.type = 0;
      positions[n.y];
      if (n.length > 0 && n.lane >= 0) positions[n.y + n.length];
      channel.notes.push_back(n);
    }
    std::stable_sort(channel.notes.begin(), channel.notes.end(), [](const auto &a, const auto &b) { return a.y < b.y; });
    channels.push_back(std::move(channel));
  }
  std::map<std::pair<Tick, int>, AuxiliaryNote> hidden, mines;
  for (const auto *kind : {"key_channels", "mine_channels"}) {
    const bool isMine = std::string(kind) == "mine_channels";
    for (const auto &source : array(field(root, kind))) {
      check();
      if (chart->WavTable.size() >= INT32_MAX - 1) return {};
      const int wav = static_cast<int>(chart->WavTable.size()) + 1;
      chart->WavTable[wav] = resourcePath(field(source, "name"));
      for (const auto &value : array(field(source, "notes"))) {
        check();
        const auto y = tick(field(value, "y"));
        const auto lane = laneFor(field(value, "x"));
        positions[y];
        if (lane < 0) continue;
        const double damage = isMine ? number(field(value, "damage")) : 0;
        if (std::abs(damage) > std::numeric_limits<float>::max()) return {};
        const AuxiliaryNote note{wav, static_cast<float>(damage)};
        if (isMine) mines.emplace(std::make_pair(y, lane), note);
        else hidden[{y, lane}] = note;
      }
    }
  }
  const auto &bga = field(root, "bga");
  if (!bga.is<json::null>()) {
    for (const auto &header : array(field(bga, legacy ? "bgaHeader" : "bga_header"))) {
      check();
      chart->BmpTable[id(field(header, legacy ? "ID" : "id"))] = resourcePath(field(header, "name"));
    }
    const auto resolve = [&](int resource) {
      return chart->BmpTable.count(resource) ? resource : BgaSequenceBlank;
    };
    std::map<int, std::shared_ptr<const BgaSequence>> sequences;
    for (const auto &entry : array(field(bga, "bga_sequence"))) {
      check();
      if (entry.is<json::null>()) continue; // Java ignores null definitions.
      const int sequenceId = id(field(entry, "id"));
      const auto &frames = field(entry, "sequence");
      if (!frames.is<json::array>()) throw std::runtime_error("Expected BGA sequence");
      BgaSequence sequence;
      for (const auto &frame : array(frames)) {
        check();
        const auto &image = field(frame, "id");
        const bool isEnd = image.is<json::null>() || signedInt(image) == INT32_MIN;
        // Unlike Java's raw runtime indexes, resolve authored header IDs just
        // like direct events; arbitrary IDs must not select unrelated images.
        sequence.push_back({frameTime(field(frame, "time")),
                            isEnd ? BgaSequenceBlank : resolve(signedInt(image)), isEnd});
      }
      sequences[sequenceId] = std::make_shared<const BgaSequence>(std::move(sequence));
    }
    const auto sequenceFor = [&](int resource) {
      const auto found = sequences.find(resource);
      if (found != sequences.end()) return found->second;
      return std::make_shared<const BgaSequence>(BgaSequence{
          {0, resolve(resource), false}, {500, BgaSequenceBlank, true}});
    };
    const char *fields[] = {legacy ? "bgaNotes" : "bga_events", legacy ? "layerNotes" : "layer_events", legacy ? "poorNotes" : "poor_events"};
    for (int layer = 0; layer < 3; ++layer) {
      for (const auto &event : array(field(bga, fields[layer]))) {
        check();
        auto &pos = positions[tick(field(event, "y"))];
        if (layer == 0) {
          const int resource = resolve(id(field(event, legacy ? "ID" : "id")));
          if (resource != BgaSequenceBlank) pos.bga = resource;
          continue;
        }
        BgaEvent decoded;
        if (layer == 2) {
          decoded.Condition = BgaEventCondition::Miss;
          decoded.Interval = 1;
          decoded.Sequences.push_back(sequenceFor(id(field(event, legacy ? "ID" : "id"))));
        } else {
          const auto condition = string(field(event, "condition"));
          if (condition == "miss") decoded.Condition = BgaEventCondition::Miss;
          else if (condition == "play") decoded.Condition = BgaEventCondition::Play;
          decoded.Interval = signedInt(field(event, "interval"));
          const auto &idSet = field(event, "id_set");
          if (decoded.Condition != BgaEventCondition::Always || !idSet.is<json::null>())
            pos.layer = -1;
          if (idSet.is<json::null>()) {
            const int resource = id(field(event, legacy ? "ID" : "id"));
            decoded.Sequences.push_back(sequenceFor(resource));
            if (sequences.count(resource)) pos.layer = -1;
            // Standard bmson layers retain existing unconditional playback.
            // Extended sequences/alternatives and PLAY/MISS remain metadata.
            if (decoded.Condition == BgaEventCondition::Always && !sequences.count(resource) &&
                resolve(resource) != BgaSequenceBlank) pos.layer = resource;
          } else {
            for (const auto &resource : array(idSet)) {
              check();
              decoded.Sequences.push_back(sequenceFor(id(resource)));
            }
          }
        }
        // Java replaces the event at a pulse. Processing poor_events last
        // gives them precedence over layer_events at that same pulse.
        pos.bgaEvent = std::move(decoded);
      }
    }
    const auto reference = [&](int resource) {
      const auto found = chart->BmpTable.find(resource);
      if (found != chart->BmpTable.end()) chart->ReferencedBmpTable[resource] = found->second;
    };
    std::set<const BgaSequence *> referencedSequences;
    // The legacy playback API owns a flat copy per MISS event. Bound that
    // expansion to 4 MiB of frame IDs even when a tiny input reuses a long
    // sequence many times. Timed metadata itself shares immutable storage.
    constexpr std::size_t maxPoorFrames = 1024 * 1024;
    std::size_t poorFrames = 0;
    for (const auto &[y, pos] : positions) {
      check();
      reference(pos.bga);
      reference(pos.layer);
      if (!pos.bgaEvent) continue;
      const auto &event = *pos.bgaEvent;
      if (event.Condition == BgaEventCondition::Miss && !event.Sequences.empty()) {
        const auto size = event.Sequences.front()->size();
        if (size > maxPoorFrames - poorFrames) throw std::runtime_error("Too many MISS frames");
        poorFrames += size;
      }
      for (const auto &sequence : event.Sequences) {
        check();
        if (referencedSequences.insert(sequence.get()).second)
          for (const auto &frame : *sequence) { check(); reference(frame.BmpId); }
      }
    }
  }
  const auto &lines = field(root, "lines");
  if (lines.is<json::null>()) {
    const Tick last = positions.rbegin()->first;
    if (last / measureTicks > 1000000) return {}; // Bound implicit bar generation.
    for (Tick y = 0; y <= last; y += measureTicks) positions[y].bar = true;
  } else {
    for (const auto &line : array(lines)) { check(); positions[tick(field(line, "y"))].bar = true; }
  }
  const Tick last = positions.rbegin()->first;
  const Tick end = (last / measureTicks + 1) * measureTicks;
  positions[end];
  double bpm = meta.Bpm, scroll = 1;
  long double elapsed = ready ? 240000000.0L / bpm : 0;
  (void)micros(elapsed);
  Tick previous = 0;
  std::map<double, long double> bpmDurations;
  for (auto &[y, pos] : positions) {
    check();
    const auto delta = (y - previous) * 60000000.0L / resolution / bpm;
    elapsed += delta;
    bpmDurations[bpm] += delta;
    pos.time = micros(elapsed);
    if (pos.bpm > 0) bpm = pos.bpm;
    if (pos.scrollChange) scroll = pos.scroll;
    pos.effectiveBpm = bpm;
    pos.effectiveScroll = scroll;
    meta.MinBpm = std::min(meta.MinBpm, bpm);
    meta.MaxBpm = std::max(meta.MaxBpm, bpm);
    const auto stop = pos.stop * 60000000.0L / resolution / bpm;
    pos.stopMicros = micros(stop);
    elapsed += stop;
    bpmDurations[bpm] += stop;
    if (scan) {
      scan->HasBpmStop = scan->HasBpmStop || pos.stop > 0;
      scan->HasScrollChange = scan->HasScrollChange || scroll != 1;
    }
    previous = y;
  }
  meta.TotalLength = micros(elapsed);
  long double longest = -1;
  for (const auto &[tempo, duration] : bpmDurations) {
    if (duration > longest) { longest = duration; meta.MostPrevalentBpm = tempo; }
  }
  meta.GuessedBeatBpm = meta.MostPrevalentBpm;
  if (scan) scan->HasBga = !chart->BmpTable.empty();
  const bool materialize = !metaOnly && !scan;
  int nextWav = static_cast<int>(chart->WavTable.size()) + 1;
  const auto sliceId = [&](int source, long long start, long long duration) {
    if (nextWav == INT32_MAX) throw std::runtime_error("Too many slices");
    const int wav = nextWav++;
    if (materialize) {
      chart->WavSlices[wav] = {{source, start, duration, wav}};
    }
    return wav;
  };
  const auto layerWav = [&](int &target, int wav) {
    if (target < 0) { target = wav; return; }
    if (!materialize) return;
    auto &layers = chart->WavSlices.at(target);
    // Incoming IDs are single slices, visited monotonically in channel/pulse
    // order. Repeated assignments of one chord slice cannot add another layer.
    if (layers.back().SliceId == wav) return;
    const auto slice = chart->WavSlices.at(wav).front();
    if (layers.size() == 1) {
      // Base slices may be shared by other chord keys. Detach only once; the
      // composite then belongs to this playable note and grows in place.
      if (nextWav == INT32_MAX) throw std::runtime_error("Too many layers");
      const auto firstSlice = layers.front();
      target = nextWav++;
      chart->WavSlices[target] = {firstSlice, slice};
    } else layers.push_back(slice);
  };
  std::map<std::pair<Tick, int>, PlayNote> playable;
  std::map<std::pair<Tick, int>, int> releases;
  std::multimap<Tick, int> background;
  std::map<int, std::map<Tick, Tick>> holds;
  std::map<int, std::set<Tick>> occupied;
  for (const auto &channel : channels) {
    long long offset = 0;
    for (std::size_t i = 0; i < channel.notes.size();) {
      check();
      std::size_t next = i + 1;
      while (next < channel.notes.size() && channel.notes[next].y == channel.notes[i].y) ++next;
      bool restart = false;
      for (auto j = i; j < next; ++j) restart = restart || !channel.notes[j].continuation;
      if (restart) offset = 0;
      long long duration = -1;
      if (next < channel.notes.size()) {
        bool nextRestart = false;
        std::size_t j = next;
        while (j < channel.notes.size() && channel.notes[j].y == channel.notes[next].y) {
          nextRestart = nextRestart || !channel.notes[j].continuation;
          ++j;
        }
        if (!nextRestart) duration = positions.at(channel.notes[next].y).time - positions.at(channel.notes[i].y).time;
      }
      const int wav = sliceId(channel.source, offset, duration);
      const bool hasPlayable = std::any_of(channel.notes.begin() + i, channel.notes.begin() + next,
          [](const auto &note) { return note.lane >= 0; });
      bool backgroundAdded = false;
      for (auto j = i; j < next; ++j) {
        check();
        const auto &n = channel.notes[j];
        if (n.lane < 0) {
          if (!hasPlayable && !backgroundAdded) { background.emplace(n.y, wav); backgroundAdded = true; }
          continue;
        }
        const auto key = std::make_pair(n.y, n.lane);
        auto &laneHolds = holds[n.lane];
        auto preceding = laneHolds.lower_bound(n.y);
        const bool hasPreceding = preceding != laneHolds.begin();
        if (hasPreceding) --preceding;
        if (n.up) {
          // Java overwrites an already admitted tail. Its pending HashMap
          // selects one earlier release; first-authored is deterministic here.
          if (hasPreceding && preceding->second == n.y) releases[key] = wav;
          else releases.emplace(key, wav);
          continue;
        }
        if (hasPreceding && n.y <= preceding->second) {
          background.emplace(n.y, wav);
          continue;
        }
        auto entry = playable.find(key);
        if (entry != playable.end()) {
          // Same-kind notes at one head layer; incompatible heads are ignored
          // by Java, unlike a note lying inside an admitted hold.
          if (entry->second.end == n.y + n.length) layerWav(entry->second.wav, wav);
          continue;
        }
        auto &laneOccupied = occupied[n.lane];
        if (n.length > 0) {
          const auto interior = laneOccupied.upper_bound(n.y);
          if (interior != laneOccupied.end() && *interior <= n.y + n.length) {
            background.emplace(n.y, wav);
            continue;
          }
          laneHolds[n.y] = n.y + n.length;
          laneOccupied.insert(n.y + n.length);
        }
        laneOccupied.insert(n.y);
        playable.emplace(key, PlayNote{n.y + n.length, wav, n.type});
      }
      if (duration >= 0) offset += duration;
      i = next;
    }
  }
  // Channel-order admission above also supplies an ordered hold index for
  // mines, avoiding a whole-chart scan for each auxiliary note.
  for (auto it = mines.begin(); it != mines.end();) {
    const auto [y, lane] = it->first;
    bool conflict = playable.count(it->first) != 0;
    const auto laneHolds = holds.find(lane);
    if (laneHolds != holds.end()) {
      auto hold = laneHolds->second.upper_bound(y);
      if (hold != laneHolds->second.begin()) {
        --hold;
        conflict = conflict || y <= hold->second;
      }
    }
    if (conflict) it = mines.erase(it);
    else ++it;
  }
  std::map<Tick, TimeLine *> timelines;
  if (materialize) {
    if (ready) {
      auto measure = std::make_unique<Measure>();
      for (int beat = 0; beat < 4; ++beat) {
        auto timeline = std::make_unique<TimeLine>(laneCapacity, false);
        timeline->Timing = micros(beat * 60000000.0L / meta.Bpm);
        timeline->BeatPosition = beat / 4.0;
        timeline->Bpm = meta.Bpm;
        timeline->IsFirstInMeasure = beat == 0;
        timeline->AddBackgroundNote(new Note(Parser::MetronomeWav));
        measure->TimeLines.push_back(timeline.release());
      }
      chart->Measures.push_back(measure.release());
    }
    Measure *measure = nullptr;
    Tick measureStart = 0;
    for (auto &[y, pos] : positions) {
      if (y == end) break;
      check();
      if (!measure || (pos.bar && y != measureStart)) {
        if (measure) measure->Scale = double(y - measureStart) / measureTicks;
        auto owned = std::make_unique<Measure>();
        owned->Pos = double(y) / measureTicks + (ready ? 1 : 0);
        owned->Timing = pos.time;
        chart->Measures.push_back(owned.get());
        measure = owned.release();
        measureStart = y;
      }
      auto timeline = std::make_unique<TimeLine>(laneCapacity, false);
      timeline->Timing = pos.time;
      timeline->BeatPosition = double(y) / measureTicks + (ready ? 1 : 0);
      timeline->IsFirstInMeasure = pos.bar;
      timeline->Bpm = pos.effectiveBpm;
      timeline->BpmChange = timeline->BpmChangeApplied = pos.bpm > 0;
      timeline->StopLength = pos.stop * 48 / resolution;
      timeline->ParsedStopDuration = pos.stopMicros;
      timeline->Scroll = pos.effectiveScroll;
      timeline->ScrollChange = pos.scrollChange;
      timeline->BgaBase = pos.bga;
      timeline->BgaLayer = pos.layer;
      if (pos.bgaEvent) {
        if (pos.bgaEvent->Condition == BgaEventCondition::Miss) {
          BgaPoorSequence poor;
          if (!pos.bgaEvent->Sequences.empty()) {
            poor.Frames.reserve(pos.bgaEvent->Sequences.front()->size());
            for (const auto &frame : *pos.bgaEvent->Sequences.front()) {
              check();
              poor.Frames.push_back(frame.BmpId);
            }
          }
          timeline->BgaPoor = std::move(poor);
        }
        timeline->BgaEvents.push_back(std::move(*pos.bgaEvent));
      }
      timelines[y] = timeline.get();
      measure->TimeLines.push_back(timeline.release());
    }
    if (measure) measure->Scale = double(end - measureStart) / measureTicks;
    for (const auto &[y, wav] : background) timelines.at(y)->AddBackgroundNote(new Note(wav));
  }
  for (const auto &[key, note] : playable) {
    check();
    const auto [y, lane] = key;
    const bool isLong = note.end > y;
    const auto type = LongNoteTypeFromLnMode(note.type == 0 ? meta.LnMode : note.type);
    const int count = isLong && (type == LongNoteType::ChargeNote || type == LongNoteType::HellChargeNote) ? 2 : 1;
    const bool scratch = scratchSet.count(lane) != 0;
    const int counted = (IsCountedNoteTime(positions.at(y).time) ? 1 : 0) +
        (count == 2 && IsCountedNoteTime(positions.at(note.end).time) ? 1 : 0);
    meta.TotalNotes += counted;
    if (isLong) {
      if (scratch) meta.TotalBackSpinNotes += counted;
      else meta.TotalLongNotes += counted;
    } else if (scratch) meta.TotalScratchNotes += counted;
    meta.PlayLength = std::max(meta.PlayLength, positions.at(note.end).time);
    if (!materialize) continue;
    if (isLong) {
      auto head = std::make_unique<LongNote>(note.wav, type);
      const auto release = releases.find({note.end, lane});
      auto tail = std::make_unique<LongNote>(release == releases.end() ? Parser::NoWav : release->second, type);
      head->Tail = tail.get();
      tail->Head = head.get();
      timelines.at(y)->SetNote(lane, head.release());
      timelines.at(note.end)->SetNote(lane, tail.release());
    } else timelines.at(y)->SetNote(lane, new Note(note.wav));
  }
  for (const auto &[key, note] : hidden) {
    check();
    chart->ReferencedWavTable[note.source] = chart->WavTable.at(note.source);
    if (materialize) timelines.at(key.first)->SetInvisibleNote(key.second, new Note(note.source));
  }
  for (const auto &[key, note] : mines) {
    check();
    const auto time = positions.at(key.first).time;
    if (IsCountedNoteTime(time)) ++meta.TotalLandmineNotes;
    meta.PlayLength = std::max(meta.PlayLength, time);
    chart->ReferencedWavTable[note.source] = chart->WavTable.at(note.source);
    if (materialize) {
      auto mine = std::make_unique<LandmineNote>(note.damage);
      mine->Wav = note.source;
      timelines.at(key.first)->SetLandmineNote(key.second, mine.release());
    }
  }
  // Keep only virtual sounds reachable from effective notes, including shared
  // chord slices but excluding discarded release sounds. Collect source
  // references only from surviving recipes, alongside effective hidden/mines.
  std::set<int> usedSounds;
  if (materialize) for (const auto &[key, note] : playable) {
    check();
    usedSounds.insert(note.wav);
    if (note.end > key.first) {
      const auto release = releases.find({note.end, key.second});
      if (release != releases.end()) usedSounds.insert(release->second);
    }
  }
  if (materialize) for (const auto &[y, wav] : background) { check(); usedSounds.insert(wav); }
  for (auto it = chart->WavSlices.begin(); it != chart->WavSlices.end();) {
    check();
    if (!usedSounds.count(it->first)) it = chart->WavSlices.erase(it);
    else {
      for (const auto &slice : it->second) {
        check();
        chart->ReferencedWavTable[slice.SourceWav] = chart->WavTable.at(slice.SourceWav);
      }
      ++it;
    }
  }
  // Preserve authored units: the player chooses its default gain for 1.0.
  meta.TotalIsRelative = !legacy;
  if (!legacy) meta.Total = std::abs(meta.Total);
  meta.Values["bmson.version"] = legacy ? "0.2.1" : "1.0.0";
  if (!materialize) { chart->WavSlices.clear(); chart->ReferencedWavTable.clear(); }
  return chart;
}
} // namespace

bool IsSource(const std::vector<unsigned char> &bytes, const std::filesystem::path &sourcePath) {
  auto extension = sourcePath.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
  const auto first = textStart(bytes);
  return extension == ".bmson" || (first < bytes.size() && bytes[first] == '{');
}
std::unique_ptr<Chart> Parse(const std::vector<unsigned char> &bytes, bool ready,
                            bool metaOnly, std::atomic_bool &cancelled,
                            ChartScanResult *scan) {
  try { return decode(bytes, ready, metaOnly, cancelled, scan); }
  catch (const std::runtime_error &) { return {}; }
}
} // namespace bms_parser::bmson
