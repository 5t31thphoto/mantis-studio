#pragma once
#include <Arduino.h>
#include "../Config.h"

namespace Mantis {

enum TrackId : uint8_t { TRACK_DRUM=0, TRACK_LEAD=1, TRACK_BASS=2, TRACK_AUDIO=3, TRACK_COUNT=4 };

enum EventFlags : uint8_t { EV_ACCENT=1, EV_TIE=2, EV_GATE=4 };

struct __attribute__((packed)) NoteEvent {
  uint32_t tick;
  uint16_t duration;
  uint8_t note;
  uint8_t velocity;
  uint8_t instrument;
  uint8_t flags;
};

struct TrackState {
  NoteEvent events[MAX_EVENTS];
  uint16_t count = 0;
  bool muted = false;
  bool solo = false;
  bool recording = false;
};

class Sequencer {
public:
  void clear();
  TrackState& track(TrackId id) { return _tracks[id]; }
  const TrackState& track(TrackId id) const { return _tracks[id]; }
  bool add(TrackId id, uint32_t tick, uint16_t duration, uint8_t note, uint8_t velocity, uint8_t instrument, uint8_t flags=0);
  void removeLastRecorded(TrackId id, uint32_t startTick, uint32_t endTick);
  void setQuantize(uint16_t ticks) { _quantize = ticks; }
  uint16_t quantize() const { return _quantize; }
  uint32_t snap(uint32_t tick, uint32_t loopTicks) const;
  bool anySolo() const;
private:
  TrackState _tracks[TRACK_COUNT];
  uint16_t _quantize = PPQ / 4; // 1/16
};

extern Sequencer gSequencer;
}
