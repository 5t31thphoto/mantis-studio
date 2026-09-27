#pragma once
#include <Arduino.h>
#include <SD.h>
#include <M5Unified.h>
#include "../Config.h"
#include "../seq/Track.h"

namespace Mantis {

class AudioEngine {
public:
  void begin();
  void update(uint32_t tick, bool transportPlaying);
  void stopAll();
  void triggerDrum(uint8_t pad, uint8_t velocity=120);
  void triggerNote(TrackId track, uint8_t note, uint8_t velocity, uint8_t instrument=0, uint16_t duration=PPQ);
  void setTrackVolume(TrackId t, uint8_t v);
  uint8_t trackVolume(TrackId t) const { return _volume[t]; }
  void setMaster(uint8_t v);
  uint8_t master() const { return _master; }
  bool beginDrumSampleRecord(uint8_t pad);
  bool updateDrumSampleRecord();
  bool drumRecording() const { return _drumRecActive; }
  uint8_t drumRecordingPad() const { return _drumRecPad; }
  uint16_t drumRecordElapsedMs() const;
  bool beginAudioRecord();
  bool updateAudioRecord();
  void finishAudioRecord();
  bool audioRecording() const { return _audioRecActive; }
  uint32_t audioRecordSamples() const { return _audioRecSamples; }
  bool loadAudioClip(const char* path);
  bool hasAudioClip() const { return _audioClip != nullptr && _audioClipSamples > 0; }
  void playAudioClip();
  void stopAudioClip();
  int16_t* audioClip() { return _audioClip; }
  uint32_t audioClipSamples() const { return _audioClipSamples; }
  uint32_t audioClipRate() const { return _audioClipRate; }
  bool sdReady() const { return _sdReady; }
  void ensureProjectDirs();
  void saveDrumSample(uint8_t pad);
  int16_t* drumSample(uint8_t pad) { return _drum[pad]; }
  uint32_t drumSampleLen(uint8_t pad) const { return _drumLen[pad]; }

private:
  static constexpr uint8_t DRUM_CHANNEL_BASE=0;
  static constexpr uint8_t SYNTH_CHANNEL_BASE=1;
  static constexpr uint8_t AUDIO_CHANNEL=7;
  static constexpr uint8_t VOICES=6;
  static constexpr uint32_t DRUM_SAMPLES = SAMPLE_RATE * DRUM_MAX_MS / 1000;
  static constexpr uint32_t SYNTH_SAMPLES = SAMPLE_RATE * SYNTH_MAX_MS / 1000;
  int16_t* _drum[4]{};
  int16_t* _drumPlay[4]{};
  uint32_t _drumLen[4]{};
  int16_t* _voice[VOICES]{};
  uint32_t _voiceLen[VOICES]{};
  int16_t* _audioClip=nullptr;
  uint32_t _audioClipSamples=0;
  uint32_t _audioClipRate=SAMPLE_RATE;
  uint8_t _volume[TRACK_COUNT]={210,190,200,210};
  uint8_t _master=190;
  bool _sdReady=false;
  bool _drumRecActive=false;
  uint8_t _drumRecPad=0;
  uint32_t _drumRecStart=0;
  uint32_t _drumRecSamples=0;
  bool _audioRecActive=false;
  uint32_t _audioRecStart=0;
  uint32_t _audioRecSamples=0;
  uint32_t _audioWriteCursor=0;
  uint8_t _recBlock[1024]{};
  int16_t* allocSamples(size_t samples);
  void makeDrum(uint8_t pad);
  void makeVoice(uint8_t slot,uint8_t note,uint8_t velocity,uint8_t instrument,TrackId track,uint16_t duration);
  bool pickVoice(uint8_t &slot);
  float midiHz(uint8_t note) const;
  void hapticBass(float energy);
  void saveWav(const char* path,const int16_t* data,uint32_t samples,uint32_t rate);
  bool loadWav(const char* path);
};
extern AudioEngine gAudio;
}
