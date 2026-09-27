// ============================================================
//  MANTIS STUDIO — haptic subwoofer
//  The Core2's vibration motor is an ERM: an off-centre weight whose spin
//  rate IS its vibration frequency, and spin rate follows the motor rail
//  voltage. So we can play it in tune: each bass note becomes a motor speed
//  in the note's own pitch class (folded into the motor's range by octaves).
//  The chassis carries the bass notes the little speaker can't reach.
//
//  - rail voltage is written directly in millivolts (AXP192 LDO3 / AXP2101 DLDO1)
//  - between the 100 mV rail steps, sigma-delta dithering + the rotor's inertia
//    give in-between speeds (fine pitch)
//  - note onsets kick-start the rotor with a short overdrive (tight rhythm),
//    glides slide the rotor, releases cut it; kicks get a punch-and-decay
//  - events come from the audio render with the time they will be HEARD
//  - tune(): measures the real motor speed at every rail step with the IMU
// ============================================================
#pragma once
#include <stdint.h>

namespace sub {
enum Mode : uint8_t { OFF = 0, KICK, FULL, MODE_COUNT };
extern uint8_t mode;

void begin();
void service();                                   // main thread; call as often as possible
void tap(uint8_t level, uint16_t ms);             // UI / metronome taps (only when the sub is idle)

// from the audio render task (single producer, lock-free); atMs = when the sound is heard
void evBassOn(uint8_t note, uint32_t atMs, bool legato);
void evBassOff(uint32_t atMs);
void evKick(uint32_t atMs);

typedef void (*TuneProgress)(float u, float hz);
bool tune(TuneProgress cb);                        // blocking self-calibration (~8 s)
bool tuned();
float nowHz();                                     // pitch the motor is playing (0 = still)
float bandLo();
float bandHi();
void setBusy(bool b);                              // true while something else owns the motor (tuning)
}
