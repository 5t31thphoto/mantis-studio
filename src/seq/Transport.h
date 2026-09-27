#pragma once
#include <Arduino.h>
#include "../Config.h"

namespace Mantis {

class Transport {
public:
  void begin();
  void update();
  void setBpm(float bpm);
  float bpm() const { return _bpm; }
  void setBars(uint8_t bars);
  uint8_t bars() const { return _bars; }
  uint32_t loopTicks() const { return (uint32_t)_bars * BEATS_PER_BAR * PPQ; }
  uint32_t tick() const;
  uint32_t absoluteTick() const;
  uint8_t beat() const;
  uint8_t bar() const;
  bool playing() const { return _playing; }
  void play();
  void stop();
  void restart();
  void seekTick(uint32_t tick);
  void setLoopEnabled(bool enabled) { _loopEnabled = enabled; }
  bool loopEnabled() const { return _loopEnabled; }
  bool beatPulse() const { return _beatPulse; }
  bool barPulse() const { return _barPulse; }
  uint8_t beatCounter() const { return _beatCounter; }
  uint32_t beatSerial() const { return _beatSerial; }
  void setCountIn(uint8_t bars) { _countInBars = bars; }
  bool countInActive() const { return _countInActive; }
  uint8_t countInNumber() const { return _countInNumber; }
  uint32_t countInRemainingMs() const { return _countInRemainingMs; }
  void startCountIn(bool startTransportAfter = true);
  void cancelCountIn();
  bool consumeCountInBeat();

private:
  float _bpm = DEFAULT_BPM;
  uint8_t _bars = DEFAULT_BARS;
  bool _playing = false;
  bool _loopEnabled = true;
  uint64_t _baseUs = 0;
  uint32_t _baseTick = 0;
  uint32_t _lastTick = 0;
  uint32_t _beatSerial = 0;
  uint8_t _beatCounter = 0;
  bool _beatPulse = false;
  bool _barPulse = false;
  uint8_t _countInBars = 1;
  bool _countInActive = false;
  bool _countInStartTransport = true;
  uint8_t _countInNumber = 0;
  uint32_t _countInRemainingMs = 0;
  uint32_t _lastCountInMs = 0;
  uint32_t tickAtMicros(uint64_t us) const;
};

extern Transport gTransport;
}
