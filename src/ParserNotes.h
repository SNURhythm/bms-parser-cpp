#pragma once

#include "Chart.h"
#include "LongNote.h"
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <limits>
#include <optional>
#include <utility>

namespace bms_parser::detail {

// Section.java retains LN identities even when another row replaces their slot.
// The arena keeps those identities separate from the active timeline slots.
class ParserNotes {
public:
  using Position = double;
  enum class Kind { Normal, Head, Tail, Mine };
  struct Event {
    Kind kind;
    int wav;
    float damage;
    TimeLine *timeline;
    Position pos;
    long long timing = 0;
    Event *pair = nullptr;
    LongNoteType type = LongNoteType::Undefined;
    Note *note = nullptr;
    std::unique_ptr<Note> owner;
  };
  static constexpr int Lanes = 16;
  std::array<std::map<Position, Event *>, Lanes> lanes;

  explicit ParserNotes(int silentWav, int mode)
      : noWav(silentWav), lnType(LongNoteTypeFromLnMode(mode)) {}

  void normal(int lane, Position pos, int wav, bool endpoint, TimeLine *timeline) {
    auto &slots = lanes[lane];
    if (!endpoint) {
      slots[pos] = create(Kind::Normal, pos, wav, timeline);
      last[lane] = pos;
      return;
    }
    if (!last[lane] || !(*last[lane] < pos)) return;
    auto it = slots.find(*last[lane]);
    if (it == slots.end()) return;
    auto *head = it->second;
    if (head->kind == Kind::Normal) {
      const auto timing = head->timing;
      head = create(Kind::Head, head->pos, head->wav, head->timeline);
      head->timing = timing;
      head->type = lnType;
      it->second = head;
    } else if ((head->kind == Kind::Head || head->kind == Kind::Tail) && !head->pair) {
      open[lane] = nullptr;
      suppressed[lane] = false;
    } else return;
    auto *tail = create(Kind::Tail, pos, noWav, timeline);
    slots[pos] = tail;
    pair(head, tail);
    complete(lane, *head);
    last[lane] = pos;
  }

  void longNote(int lane, Position pos, int wav, TimeLine *timeline) {
    auto &slots = lanes[lane];
    if (inside(lane, pos)) {
      if (!open[lane] && !suppressed[lane]) {
        suppressed[lane] = true;
      } else {
        if (open[lane] && open[lane]->pos != std::numeric_limits<double>::denorm_min())
          erase(lane, open[lane]->pos);
        open[lane] = nullptr;
        suppressed[lane] = false;
      }
      return;
    }
    // Java uses Double.MIN_VALUE as its sentinel, even if a real section
    // rounds to that value. Preserve the resulting unpaired visible head.
    if (suppressed[lane] ||
        (open[lane] && open[lane]->pos == std::numeric_limits<double>::denorm_min())) {
      suppressed[lane] = false;
      open[lane] = nullptr;
      return;
    }
    if (!open[lane]) {
      const auto old = slots.find(pos);
      if (old != slots.end() && old->second->kind == Kind::Normal &&
          old->second->wav != wav) background(*old->second);
      slots[pos] = open[lane] = create(Kind::Head, pos, wav, timeline);
      last[lane] = pos;
      return;
    }
    auto *head = open[lane];
    auto it = slots.upper_bound(head->pos);
    while (it != slots.end() && it->first < pos) {
      if (it->second->kind == Kind::Normal) background(*it->second);
      if (last[lane] == it->first) last[lane].reset();
      it = slots.erase(it);
    }
    head->type = lnType;
    auto *tail = create(Kind::Tail, pos, head->wav == wav ? noWav : wav, timeline);
    slots[pos] = tail;
    pair(head, tail);
    complete(lane, *head);
    open[lane] = nullptr;
    last[lane] = pos;
  }

  void mine(int lane, Position pos, int wav, float damage, TimeLine *timeline) {
    if (lanes[lane].count(pos) || inside(lane, pos)) return;
    auto *event = create(Kind::Mine, pos, wav, timeline);
    event->damage = damage;
    lanes[lane][pos] = event;
    last[lane] = pos;
  }

  template<class Timing>
  void setTiming(Position measureStart, Timing timing) {
    // Previously visited positions are immutable, except a rounded cell at
    // the current bar. LNOBJ copies an older candidate's cached time itself.
    for (size_t i = untimedBegin; i < events.size(); ++i)
      if (events[i].pos >= measureStart)
        events[i].timing = timing(events[i].pos);
    untimedBegin = events.size();
    for (int lane = 0; lane < Lanes; ++lane) {
      const auto boundary = lanes[lane].find(measureStart);
      if (boundary != lanes[lane].end())
        boundary->second->timing = timing(measureStart);
      if (open[lane] && open[lane]->pos == measureStart)
        open[lane]->timing = timing(measureStart);
    }
  }

  void advance(Chart &chart, bool materialize, Position boundary) {
    const auto scratchLanes = chart.Meta.GetScratchLaneIndices();
    size_t possibleLive = 0;
    for (int lane = 0; lane < Lanes; ++lane) {
      auto &slots = lanes[lane];
      Position keepFrom = boundary;
      // A later close can still remove normal notes anywhere inside an open
      // hold. Retain that unresolved range, including a detached open head.
      if (open[lane]) keepFrom = std::min(keepFrom, open[lane]->pos);
      if (last[lane]) {
        const auto candidate = slots.find(*last[lane]);
        if (candidate != slots.end() &&
            (candidate->second->kind == Kind::Normal || !candidate->second->pair))
          keepFrom = std::min(keepFrom, *last[lane]);
      }
      const bool scratch = std::find(scratchLanes.begin(), scratchLanes.end(), lane)
                           != scratchLanes.end();
      auto it = slots.begin();
      while (it != slots.end() && it->first < keepFrom) {
        publish(chart, *it->second, lane, scratch, materialize);
        if (last[lane] == it->first) last[lane].reset();
        it = slots.erase(it);
      }
      auto &intervals = completed[lane];
      while (!intervals.empty() && intervals.begin()->second < boundary)
        intervals.erase(intervals.begin());
      possibleLive += 2 * (slots.size() + (open[lane] != nullptr));
    }
    // Collect only when enough events are retired. An unresolved chart-long
    // hold therefore grows linearly instead of being copied every measure.
    if (events.size() > 2 * possibleLive + 1024) collect(chart);
  }

  void finish(Chart &chart, bool materialize) {
    for (int lane = 0; lane < Lanes; ++lane)
      if (open[lane] && open[lane]->pos != std::numeric_limits<double>::denorm_min())
        erase(lane, open[lane]->pos);
    const auto scratchLanes = chart.Meta.GetScratchLaneIndices();
    for (int lane = 0; lane < Lanes; ++lane) {
      const bool scratch = std::find(scratchLanes.begin(), scratchLanes.end(), lane)
                           != scratchLanes.end();
      for (auto &[pos, event] : lanes[lane])
        publish(chart, *event, lane, scratch, materialize);
    }
    // Detached partners are observable through LN pointers, but not playable.
    for (auto &event : events)
      if (event.owner) chart.DetachedNotes.push_back(std::move(event.owner));
  }

private:
  const int noWav;
  const LongNoteType lnType;
  std::deque<Event> events;
  size_t untimedBegin = 0;
  std::array<std::optional<Position>, Lanes> last{};
  std::array<Event *, Lanes> open{};
  std::array<bool, Lanes> suppressed{};
  // Union of closed intervals: membership does not depend on the identities
  // retained for LN pairing. Tree lookup avoids revisiting all earlier holds.
  std::array<std::map<Position, Position>, Lanes> completed;

  Event *create(Kind kind, Position pos, int wav, TimeLine *timeline) {
    events.push_back({kind, wav, 0, timeline, pos, 0, nullptr,
                      LongNoteType::Undefined, nullptr, nullptr});
    return &events.back();
  }
  void collect(Chart &chart) {
    std::map<Event *, Event *> retained;
    const auto retain = [&](Event *event) {
      if (!event) return;
      retained.emplace(event, nullptr);
      if (event->pair) retained.emplace(event->pair, nullptr);
    };
    for (int lane = 0; lane < Lanes; ++lane) {
      for (const auto &[pos, event] : lanes[lane]) retain(event);
      retain(open[lane]);
    }
    std::deque<Event> remaining;
    for (auto &[event, relocated] : retained) {
      remaining.push_back(std::move(*event));
      relocated = &remaining.back();
    }
    for (auto &event : remaining)
      if (event.pair) event.pair = retained.at(event.pair);
    for (int lane = 0; lane < Lanes; ++lane) {
      for (auto &[pos, event] : lanes[lane]) event = retained.at(event);
      if (open[lane]) open[lane] = retained.at(open[lane]);
    }
    for (auto &event : events)
      if (event.owner) chart.DetachedNotes.push_back(std::move(event.owner));
    events.swap(remaining);
    untimedBegin = events.size();
  }
  static void publish(Chart &chart, Event &event, int lane, bool scratch,
                      bool materialize) {
    chart.Meta.PlayLength = std::max(chart.Meta.PlayLength, event.timing);
    if (event.kind == Kind::Mine) {
      if (IsCountedNoteTime(event.timing)) ++chart.Meta.TotalLandmineNotes;
    } else if (IsCountedNoteTime(event.timing) &&
               (event.kind != Kind::Tail || event.type == LongNoteType::ChargeNote ||
                event.type == LongNoteType::HellChargeNote)) {
      ++chart.Meta.TotalNotes;
      if (event.kind == Kind::Head || event.kind == Kind::Tail) {
        if (scratch) ++chart.Meta.TotalBackSpinNotes;
        else ++chart.Meta.TotalLongNotes;
      } else if (scratch) ++chart.Meta.TotalScratchNotes;
    }
    if (materialize) {
      materializeEvent(event, lane);
      event.timeline->SetNote(lane, event.owner.release());
    }
  }
  static void pair(Event *start, Event *end) {
    start->pair = end;
    end->pair = start;
    end->kind = start->pos < end->pos ? Kind::Tail : Kind::Head;
    start->kind = end->kind == Kind::Tail ? Kind::Head : Kind::Tail;
    end->type = start->type;
  }
  static void background(const Event &event) {
    if (event.timeline) event.timeline->AddBackgroundNote(new Note(event.wav));
  }
  bool inside(int lane, Position pos) const {
    const auto &intervals = completed[lane];
    const auto after = intervals.upper_bound(pos);
    return after != intervals.begin() && pos <= std::prev(after)->second;
  }
  void complete(int lane, const Event &head) {
    Position start = head.pos, end = head.pair->pos;
    // The Java containment check never matches a backwards pair.
    if (start > end) return;
    auto &intervals = completed[lane];
    auto it = intervals.lower_bound(start);
    if (it != intervals.begin() && std::prev(it)->second >= start)
      it = std::prev(it);
    while (it != intervals.end() && it->first <= end) {
      start = std::min(start, it->first);
      end = std::max(end, it->second);
      it = intervals.erase(it);
    }
    intervals.emplace_hint(it, start, end);
  }
  void erase(int lane, Position pos) {
    if (last[lane] == pos) last[lane].reset();
    lanes[lane].erase(pos);
  }
  static void materializeEvent(Event &event, int lane) {
    if (event.note) return;
    if (event.kind == Kind::Head || event.kind == Kind::Tail) {
      auto *ln = new LongNote(event.wav, event.type);
      event.note = ln;
      event.owner.reset(ln);
      if (event.pair) {
        materializeEvent(*event.pair, lane);
        auto *partner = static_cast<LongNote *>(event.pair->note);
        if (event.kind == Kind::Tail) ln->Head = partner;
        else ln->Tail = partner;
      }
    } else if (event.kind == Kind::Mine) {
      event.note = new LandmineNote(event.damage);
      event.owner.reset(event.note);
      event.note->Wav = event.wav;
    } else {
      event.note = new Note(event.wav);
      event.owner.reset(event.note);
    }
    event.note->Lane = lane;
    event.note->Timeline = event.timeline;
  }
};

} // namespace bms_parser::detail
