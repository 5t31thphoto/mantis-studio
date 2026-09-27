// ============================================================
//  MANTIS STUDIO — haptic subwoofer (see sub.h)
// ============================================================
#include "app.h"
#include "sub.h"
#include <string.h>
#ifndef HOST
#include <Preferences.h>
#endif

namespace sub {

uint8_t mode = FULL;

// ---------- the motor rail ----------
static const int MAXSTEP = 40;
static int s_mvMin = 1800, s_mvMax = 3300, s_n = 16;   // AXP192 LDO3 default: 1.8-3.3 V in 100 mV steps
static float s_hz[MAXSTEP];                              // measured/modelled rotor speed per step (0 = won't spin)
static bool s_tuned = false, s_busy = false;
static int s_mvNow = -1;
static uint32_t s_lastWrite = 0;
#ifdef HOST
int g_subMv = 0;
#endif

static void railMv(int mv) {
  if (mv == s_mvNow) return;
  s_mvNow = mv; s_lastWrite = millis();
#ifndef HOST
  switch (M5.Power.getType()) {
    case m5::Power_Class::pmic_t::pmic_axp192: M5.Power.Axp192.setLDO3(mv); break;
    case m5::Power_Class::pmic_t::pmic_axp2101: M5.Power.Axp2101.setDLDO1(mv); break;
    default: M5.Power.setVibration(mv ? (uint8_t)clampf(mv * 255.f / 3300.f, 1.f, 255.f) : 0); break;
  }
#else
  g_subMv = mv;
#endif
}
static inline int stepMv(int i) { return s_mvMin + i * 100; }
static void defaultModel() {
  // coin ERM: ~200 Hz (12k rpm) at its rated 3.0 V, roughly proportional to (V - ~1.0 V)
  for (int i = 0; i < s_n; i++) {
    float v = stepMv(i) / 1000.f, f = 200.f * (v - 1.0f) / 2.0f;
    s_hz[i] = (v < 1.3f || f < 45.f) ? 0.f : f;
  }
}
static int firstUsable() { for (int i = 0; i < s_n; i++) if (s_hz[i] > 0) return i; return s_n - 1; }
float bandLo() { return s_hz[firstUsable()]; }
float bandHi() { return s_hz[s_n - 1]; }
bool tuned() { return s_tuned; }
void setBusy(bool b) { s_busy = b; if (b) railMv(0); }

// ---------- events from the audio render (SPSC ring) ----------
enum : uint8_t { E_ON = 1, E_OFF, E_KICK };
struct Ev { uint32_t at; uint8_t type, note, legato, pad; };
static Ev s_ring[64];
static volatile uint8_t s_head = 0, s_tail = 0;
static void push(uint8_t t, uint8_t note, uint32_t at, bool leg) {
  uint8_t h = s_head, nh = (uint8_t)((h + 1) & 63);
  if (nh == s_tail) return;                                // full: drop (never block the audio)
  s_ring[h] = {at, t, note, (uint8_t)leg, 0};
  s_head = nh;
}
void evBassOn(uint8_t note, uint32_t atMs, bool legato) { if (mode == FULL) push(E_ON, note, atMs, legato); }
void evBassOff(uint32_t atMs) { push(E_OFF, 0, atMs, false); }
void evKick(uint32_t atMs) { if (mode != OFF) push(E_KICK, 0, atMs, false); }

// ---------- engine state (main thread) ----------
static bool s_on = false;
static float s_hzCur = 0, s_hzTarget = 0;
static uint32_t s_overUntil = 0, s_kickAt = 0, s_kickUntil = 0, s_tapUntil = 0, s_ditherAt = 0, s_prevMs = 0;
static uint8_t s_tapLevel = 0;
static float s_sd = 0;                                     // sigma-delta accumulator
static bool s_hi = false;
static const uint32_t LEAD_MS = 8;                         // start the rotor a hair early: it has mass

static float foldToBand(float f) {
  float lo = bandLo(), hi = bandHi();
  if (lo <= 0 || hi <= lo) return 0;
  while (f < lo * 0.999f) f *= 2.f;                        // octaves keep it in tune
  while (f > hi * 1.001f) f *= 0.5f;
  if (f < lo * 0.999f) f = lo;                             // band narrower than an octave (shouldn't happen)
  return f;
}
static inline float mtof(int n) { return 440.f * powf(2.f, (n - 69) / 12.f); }

void tap(uint8_t level, uint16_t ms) {
  if (level >= s_tapLevel || millis() >= s_tapUntil) { s_tapLevel = level; s_tapUntil = millis() + ms; }
}

float nowHz() { return (s_mvNow > 0 && (s_on || millis() < s_kickUntil)) ? s_hzCur : 0.f; }

void service() {
  if (s_busy) return;
  uint32_t now = millis();
  float dt = (now - s_prevMs) / 1000.f; if (dt > 0.05f) dt = 0.05f; s_prevMs = now;
  // take events that are due (slightly early, so the rotor is up to speed when the note is heard)
  while (s_tail != s_head) {
    const Ev &e = s_ring[s_tail];
    if ((int32_t)(e.at - (now + LEAD_MS)) > 0) break;
    if (e.type == E_ON) {
      s_hzTarget = foldToBand(mtof(e.note));
      if (!s_on || !e.legato) { s_hzCur = s_hzTarget; s_overUntil = now + (s_on ? 6 : 14); }   // kick-start from rest
      s_on = s_hzTarget > 0;
    } else if (e.type == E_OFF) {
      s_on = false;
    } else if (e.type == E_KICK) {
      s_kickAt = now; s_kickUntil = now + 95;
    }
    s_tail = (uint8_t)((s_tail + 1) & 63);
  }
  if (mode == OFF) { s_on = false; s_kickUntil = 0; }
  // glide in log-pitch (follows the synth's legato glide)
  if (s_on && s_hzCur > 0 && s_hzTarget > 0) s_hzCur *= powf(s_hzTarget / s_hzCur, clampf(dt * 18.f, 0.f, 1.f));

  int mv = 0;
  if (now < s_kickUntil) {
    // kick: a punch at full rail, then the body of the drum on the lowest pitch we can play
    uint32_t e = now - s_kickAt;
    if (e < 24) mv = s_mvMax;
    else { mv = stepMv(firstUsable()); s_hzCur = s_on ? s_hzCur : bandLo(); }
  } else if (s_on) {
    if (now < s_overUntil) mv = s_mvMax;
    else {
      // pitch -> rail: pick the two steps around the target speed and dither between them
      int lo = firstUsable();
      int i = lo;
      while (i < s_n - 1 && s_hz[i + 1] < s_hzCur) i++;
      int j = i < s_n - 1 ? i + 1 : i;
      float span = s_hz[j] - s_hz[i];
      float frac = span > 0.5f ? clampf((s_hzCur - s_hz[i]) / span, 0.f, 1.f) : 0.f;
      if (now - s_ditherAt >= 5) {                          // 200 Hz decisions; the rotor's inertia averages them
        s_ditherAt = now;
        s_sd += frac;
        s_hi = s_sd >= 1.f;
        if (s_hi) s_sd -= 1.f;
      }
      mv = stepMv(s_hi ? j : i);
    }
  } else if (now < s_tapUntil) {
    mv = s_mvMin + (int)((s_mvMax - s_mvMin) * (s_tapLevel / 255.f));
    mv = (mv / 100) * 100;
    if (stepMv(firstUsable()) > mv) mv = stepMv(firstUsable());
  } else s_tapLevel = 0;
  if (mv != s_mvNow && (mv == 0 || now - s_lastWrite >= 2)) railMv(mv);
}

// ---------- self-tuning with the IMU ----------
// Measure the rotor's real speed at every rail step: set the step, let it settle, sample the
// accelerometer as fast as I2C allows with its low-pass bypassed, and count zero crossings.
#ifndef HOST
static const uint8_t IMU = 0x68;
static int16_t s_smp[3][1400];
static float measureHz(int ms) {
  int n = 0;
  uint32_t t0 = micros(), tEnd = t0 + ms * 1000u;
  static uint32_t ts[1400];
  while (n < 1400 && (int32_t)(micros() - tEnd) < 0) {
    uint8_t b[6];
    if (!M5.In_I2C.readRegister(IMU, 0x3B, b, 6, 400000)) break;
    ts[n] = micros() - t0;
    for (int a = 0; a < 3; a++) s_smp[a][n] = (int16_t)((b[a * 2] << 8) | b[a * 2 + 1]);
    n++;
  }
  if (n < 200) return 0;
  int best = 0; float bestVar = 0, mean[3];
  for (int a = 0; a < 3; a++) {
    double s = 0, s2 = 0;
    for (int i = 0; i < n; i++) { s += s_smp[a][i]; s2 += (double)s_smp[a][i] * s_smp[a][i]; }
    mean[a] = (float)(s / n);
    float var = (float)(s2 / n - (s / n) * (s / n));
    if (var > bestVar) { bestVar = var; best = a; }
  }
  float sd = sqrtf(bestVar);
  if (sd < 30.f) return 0;                                  // not spinning (or too weak to feel)
  float h = sd * 0.3f;
  bool up = s_smp[best][0] - mean[best] > 0;
  int cnt = 0; float tFirst = 0, tLast = 0;
  for (int i = 1; i < n; i++) {
    float v = s_smp[best][i] - mean[best];
    if (!up && v > h) {
      up = true;
      float t = ts[i] / 1e6f;
      if (cnt == 0) tFirst = t;
      tLast = t; cnt++;
    } else if (up && v < -h) up = false;
  }
  if (cnt < 6) return 0;
  float f = (cnt - 1) / (tLast - tFirst);
  return (f >= 30.f && f <= 380.f) ? f : 0.f;
}
#endif

bool tune(TuneProgress cb) {
#ifdef HOST
  (void)cb; return false;
#else
  uint8_t who = 0;
  if (!M5.In_I2C.readRegister(IMU, 0x75, &who, 1, 400000) || who != 0x19) return false;   // MPU6886 only
  uint8_t c2 = 0;
  M5.In_I2C.readRegister(IMU, 0x1D, &c2, 1, 400000);
  M5.In_I2C.writeRegister8(IMU, 0x1D, (uint8_t)(c2 | 0x08), 400000);   // ACCEL_FCHOICE_B: bypass the low-pass, 4 kHz
  s_busy = true;
  float hz[MAXSTEP];
  railMv(s_mvMax); delay(350);                              // spin up from the top, then walk down
  for (int i = s_n - 1; i >= 0; i--) {
    railMv(stepMv(i)); delay(i == s_n - 1 ? 60 : 200);
    hz[i] = measureHz(260);
    if (cb) cb((float)(s_n - i) / s_n, hz[i]);
  }
  railMv(0);
  M5.In_I2C.writeRegister8(IMU, 0x1D, c2, 400000);
  s_busy = false;
  // clean up: speed must rise with voltage; drop outliers, fill single gaps
  int valid = 0;
  for (int i = 1; i < s_n - 1; i++)
    if (hz[i] == 0 && hz[i - 1] > 0 && hz[i + 1] > 0) hz[i] = 0.5f * (hz[i - 1] + hz[i + 1]);
  for (int i = s_n - 2; i >= 0; i--) if (hz[i] > 0 && hz[i + 1] > 0 && hz[i] >= hz[i + 1]) hz[i] = 0;
  float lo = 1e9f, hi = 0;
  for (int i = 0; i < s_n; i++) if (hz[i] > 0) { valid++; lo = fminf(lo, hz[i]); hi = fmaxf(hi, hz[i]); }
  if (valid < 5 || hi < lo * 1.8f) return false;           // not trustworthy: keep what we had
  for (int i = 0; i < s_n; i++) s_hz[i] = hz[i];
  s_tuned = true;
  Preferences p;
  if (p.begin("mstudio", false)) { p.putBytes("subhz", s_hz, sizeof(float) * s_n); p.putInt("subn", s_n); p.end(); }
  return true;
#endif
}

void begin() {
#ifndef HOST
  if (M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp2101) { s_mvMin = 800; s_mvMax = 3300; }   // DLDO1 reaches much lower
#endif
  s_n = (s_mvMax - s_mvMin) / 100 + 1;
  if (s_n > MAXSTEP) s_n = MAXSTEP;
  defaultModel();
#ifndef HOST
  Preferences p;
  if (p.begin("mstudio", true)) {
    if (p.getInt("subn", 0) == s_n && p.getBytes("subhz", s_hz, sizeof(float) * s_n) == sizeof(float) * s_n) s_tuned = true;
    else defaultModel();
    p.end();
  }
#endif
  railMv(0);
}

}  // namespace sub
