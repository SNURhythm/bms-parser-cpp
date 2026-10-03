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
    completed[lane].push_back(head);
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
    completed[lane].push_back(head);
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
  void setTiming(Timing timing) {
    for (auto &event : events) event.timing = timing(event.pos);
  }

  void finish(Chart &chart, bool materialize) {
    for (int lane = 0; lane < Lanes; ++lane)
      if (open[lane] && open[lane]->pos != std::numeric_limits<double>::denorm_min())
        erase(lane, open[lane]->pos);
    const auto scratchLanes = chart.Meta.GetScratchLaneIndices();
    for (int lane = 0; lane < Lanes; ++lane) {
      const bool scratch = std::find(scratchLanes.begin(), scratchLanes.end(), lane)
                           != scratchLanes.end();
      for (auto &[pos, event] : lanes[lane]) {
        chart.Meta.PlayLength = std::max(chart.Meta.PlayLength, event->timing);
        if (event->kind == Kind::Mine) {
          if (IsCountedNoteTime(event->timing)) ++chart.Meta.TotalLandmineNotes;
        } else if (IsCountedNoteTime(event->timing) &&
                   (event->kind != Kind::Tail || event->type == LongNoteType::ChargeNote ||
                    event->type == LongNoteType::HellChargeNote)) {
          ++chart.Meta.TotalNotes;
          if (event->kind == Kind::Head || event->kind == Kind::Tail) {
            if (scratch) ++chart.Meta.TotalBackSpinNotes;
            else ++chart.Meta.TotalLongNotes;
          } else if (scratch) ++chart.Meta.TotalScratchNotes;
        }
        if (materialize) materializeEvent(*event, lane);
      }
    }
    if (!materialize) return;
    for (int lane = 0; lane < Lanes; ++lane) {
      for (auto &[pos, event] : lanes[lane])
        event->timeline->SetNote(lane, event->note);
    }
    // Detached partners are observable through LN pointers, but not playable.
    for (auto &event : events) {
      if (!event.note) continue;
      const auto &slots = lanes[event.note->Lane];
      const auto active = slots.find(event.pos);
      if (active == slots.end() || active->second != &event)
        chart.DetachedNotes.emplace_back(event.note);
    }
  }

private:
  const int noWav;
  const LongNoteType lnType;
  std::deque<Event> events;
  std::array<std::optional<Position>, Lanes> last{};
  std::array<Event *, Lanes> open{};
  std::array<bool, Lanes> suppressed{};
  std::array<std::vector<Event *>, Lanes> completed;

  Event *create(Kind kind, Position pos, int wav, TimeLine *timeline) {
    events.push_back({kind, wav, 0, timeline, pos});
    return &events.back();
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
    for (const auto *start : completed[lane])
      if (start->pos <= pos && pos <= start->pair->pos) return true;
    return false;
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
      if (event.pair) {
        materializeEvent(*event.pair, lane);
        auto *partner = static_cast<LongNote *>(event.pair->note);
        if (event.kind == Kind::Tail) ln->Head = partner;
        else ln->Tail = partner;
      }
    } else if (event.kind == Kind::Mine) {
      event.note = new LandmineNote(event.damage);
      event.note->Wav = event.wav;
    } else event.note = new Note(event.wav);
    event.note->Lane = lane;
    event.note->Timeline = event.timeline;
  }
};

} // namespace bms_parser::detail
