#include "Transport.h"

namespace Mantis {
Transport gTransport;

void Transport::begin() { _baseUs = micros(); _baseTick = 0; _lastTick = 0; }

void Transport::setBpm(float bpm) { _bpm = constrain(bpm, MIN_BPM, MAX_BPM); if (_playing) { _baseTick = tick(); _baseUs = micros(); } }
void Transport::setBars(uint8_t bars) { _bars = constrain(bars, 1, 16); if (_playing) restart(); }
uint32_t Transport::tickAtMicros(uint64_t us) const {
  const double ticksPerUs = ((double)_bpm * (double)PPQ) / 60000000.0;
  return _baseTick + (uint32_t)((us - _baseUs) * ticksPerUs);
}
uint32_t Transport::absoluteTick() const { return _playing ? tickAtMicros(micros()) : _baseTick; }
uint32_t Transport::tick() const {
  uint32_t t = absoluteTick();
  uint32_t len = loopTicks();
  return len ? t % len : 0;
}
uint8_t Transport::beat() const { return (uint8_t)((tick() / PPQ) % BEATS_PER_BAR); }
uint8_t Transport::bar() const { return (uint8_t)((tick() / (PPQ * BEATS_PER_BAR)) % _bars); }

void Transport::play() { if (_countInActive) return; _playing = true; _baseUs = micros(); _baseTick = _lastTick; }
void Transport::stop() { _lastTick = tick(); _playing = false; _baseTick = _lastTick; }
void Transport::restart() { _baseTick = 0; _lastTick = 0; _baseUs = micros(); _playing = true; }
void Transport::seekTick(uint32_t t) { _baseTick = t % loopTicks(); _lastTick = _baseTick; _baseUs = micros(); }

void Transport::startCountIn(bool startTransportAfter) {
  if (_countInActive) return;
  _countInStartTransport = startTransportAfter;
  _countInActive = true;
  _countInNumber = (uint8_t)(_countInBars * BEATS_PER_BAR);
  _countInRemainingMs = _countInNumber * (uint32_t)(60000.0f / _bpm);
  _lastCountInMs = millis();
}
void Transport::cancelCountIn() { _countInActive = false; _countInNumber = 0; _countInRemainingMs = 0; }

bool Transport::consumeCountInBeat() {
  if (!_countInActive) return false;
  uint32_t now = millis();
  uint32_t beatMs = (uint32_t)(60000.0f / _bpm);
  if (now - _lastCountInMs < beatMs) return false;
  _lastCountInMs += beatMs;
  if (_countInNumber > 0) --_countInNumber;
  _countInRemainingMs = _countInNumber * beatMs;
  if (_countInNumber == 0) {
    _countInActive = false;
    if (_countInStartTransport) restart();
  }
  return true;
}

void Transport::update() {
  _beatPulse = false;
  _barPulse = false;
  if (_countInActive) { consumeCountInBeat(); return; }
  if (!_playing) return;
  uint32_t nowTick = absoluteTick();
  uint32_t prev = _lastTick;
  if (nowTick != prev) {
    uint32_t len = loopTicks();
    if (_loopEnabled && len && nowTick / len != prev / len) {
      nowTick %= len;
      _baseTick = nowTick;
      _baseUs = micros();
      prev %= len;
    }
    uint32_t prevBeat = prev / PPQ;
    uint32_t curBeat = nowTick / PPQ;
    if (curBeat != prevBeat) {
      _beatPulse = true;
      _beatCounter = (uint8_t)(curBeat % BEATS_PER_BAR);
      ++_beatSerial;
      if (_beatCounter == 0) _barPulse = true;
    }
    _lastTick = nowTick;
  }
}
}
