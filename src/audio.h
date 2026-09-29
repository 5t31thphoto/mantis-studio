// ============================================================
//  MANTIS STUDIO — audio engine
//  One streaming mixer renders every voice (drum pads, 6-voice lead,
//  mono bass, the audio clip, the click) in 192-sample blocks at 22.05 kHz
//  on core 0. The sequencer runs inside the render loop: sample-accurate.
//  Core2 shares one I2S port between mic and speaker, so recording pauses
//  playback; a haptic metronome keeps you in time while the mic is live.
// ============================================================
#pragma once
#include <stdint.h>

enum Trk : uint8_t { T_DRUM = 0, T_LEAD, T_BASS, T_AUDIO, T_COUNT };
enum Pad : uint8_t { PAD_HAT_C = 0, PAD_HAT_O, PAD_KICK, PAD_SNARE, PAD_COUNT };

namespace aud {
static const int STEPS = 128;                 // 4 bars of 32nds
static const int MAXEV = 192;
struct Ev { uint8_t step, len, note, flags; };

enum RecStage : uint8_t { REC_IDLE = 0, REC_COUNT, REC_CAPTURE, REC_OK, REC_QUIET };
enum LeadSound : uint8_t { LS_PLUCK = 0, LS_PAD, LS_CHIP, LS_COUNT };
enum BassSound : uint8_t { BS_ELECTRO = 0, BS_CHIP, BS_DUB, BS_COUNT };

void begin();
void service(float dt);                       // main thread, every frame

// ---- transport ----
bool playing();
void play();                                  // from bar 1
void stop();
float bpm();
void setBpm(float b);
float loopPhase();                            // 0..1
int step();                                   // 0..127 (-1 stopped)
void metronome(bool on);
void metronomeSync();

// ---- tracks ----
extern bool rec[T_COUNT], mute[T_COUNT];
extern float vol[T_COUNT], master;
extern float meter[T_COUNT];                  // 0..1 per track
void clearTrack(int t);

// ---- drums ----
void padHit(int pad);
bool stepOn(int pad, int st);
extern uint32_t padFlashMs[PAD_COUNT];

// ---- sampler (drum pads) ----
void recStart(int pad);
RecStage recStage();
float recProgress();
int recPad();
bool userSample(int pad);
void clearUser(int pad);
bool sdSample(int pad);                  // pad is playing the SD card's sample
void clearPad(int pad);                  // explicit clear: built-in sound, remembered (SD file untouched)
void stepSet(int pad, int st, bool on);  // step editor (st in 32nds)

// ---- lead / bass ----
void noteOn(int trk, int note);
void noteOff(int trk, int note);
void arp(const uint8_t *notes, int n);        // n = 0 stops the arpeggiator
extern uint8_t leadSound, bassSound;
extern float leadTone;                        // 0..1 brightness (tilt)
const Ev *events(int trk, int &n);

// ---- audio clip ----
void clipRec(int bars);                       // pauses playback, 1 bar count-in, records, resumes
RecStage clipStage();
float clipProgress();
int clipBars();
bool hasClip();
const int8_t *clipPeaks();                    // 160 columns
extern int clipBarsSel;

// ---- project ----
bool save(int slot);
bool load(int slot);
bool sdOk();

// ---- meters for the mantis / visuals ----
extern float level, peak, zcr, onset, beatPos, beatConf, bass;
bool speakerLive();
void sfx(float pitch);
}
