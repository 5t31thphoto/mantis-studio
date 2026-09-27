// ============================================================
//  MANTIS STUDIO — audio engine (see audio.h)
// ============================================================
#include "app.h"
#include "audio.h"
#include "sub.h"
#include <SD.h>
#include <SPI.h>
#include <string.h>

namespace aud {

static const int SR = 22050, BLK = 192, MSR = 16000;       // MSR: mic / sample rate
static const int REC_MAX = MSR * 8 / 10;
static const int CLIP_MAX = MSR * 24;                       // 24 s of clip
static const int LAT_SAMPLES = SR * 22 / 1000;              // what you hear lags the render
static const uint32_t SUB_LAT_MS = 20;                      // render -> ears (1 queued block + DMA)
static uint32_t s_blockMs = 0;                              // when the block being rendered will be heard
static int s_evOff = 0;                                     // sample offset inside that block
static inline uint32_t heardAt() { return s_blockMs + (uint32_t)(s_evOff * 1000 / SR); }

// ---------- exported ----------
bool rec[T_COUNT] = {true, true, true, true}, mute[T_COUNT];
float vol[T_COUNT] = {0.9f, 0.7f, 0.8f, 0.9f}, master = 0.9f, meter[T_COUNT];
uint32_t padFlashMs[PAD_COUNT];
uint8_t leadSound = LS_PLUCK, bassSound = BS_ELECTRO;
float leadTone = 0.5f;
int clipBarsSel = 2;
float level = 0, peak = 0, zcr = 0, onset = 0, beatPos = 0, beatConf = 0, bass = 0;

// ---------- ownership ----------
static SemaphoreHandle_t s_mux = nullptr;                  // guards render vs. main edits
static QueueHandle_t s_q = nullptr;
static volatile bool s_spk = false;
static bool s_mic = false, s_sdOk = false;

// ---------- transport (render-owned) ----------
static volatile bool s_play = false;
static volatile float s_bpm = 96.f;
static double s_pos = 0;                                    // samples into the loop
static double s_stepLen = SR * 60.0 / 96.0 / 8.0;
static volatile int s_step = -1;
static double s_free = 0;                                    // free-running clock (arp, click)
static volatile bool s_metro = false;
static double s_metT0 = 0;
static int64_t s_lastMet = -1;

// ---------- patterns ----------
static uint32_t s_pat[PAD_COUNT][STEPS / 32], s_sup[PAD_COUNT][STEPS / 32];
static Ev s_ev[2][MAXEV];                                    // [0] lead, [1] bass
static int s_nev[2];
static int16_t s_pend[2][128];                               // note -> pending event index
static double s_pendPos[2][128];

// ---------- voices ----------
struct PadV { const int16_t *d; int len; uint32_t pos; bool on; };
static PadV s_pv[PAD_COUNT];
static int16_t *s_smp[PAD_COUNT], *s_syn[PAD_COUNT];
static int s_len[PAD_COUNT], s_synLen[PAD_COUNT];
static bool s_user[PAD_COUNT];
static int16_t *s_grave[PAD_COUNT];
static uint32_t s_graveAt[PAD_COUNT];

struct SynV { uint8_t note; bool gate, live; float ph, ph2, inc, env, lp, lp2, bp, age; int seqOff; };
static SynV s_lv[6];
static SynV s_bv;
static float s_bassTarget = 0;
static uint8_t s_arpN = 0, s_arpNotes[4], s_arpIdx = 0, s_arpCur = 255;
static int64_t s_arpLast = -1;
struct Click { float ph, env, f; };
static Click s_click;

static int16_t *s_clip = nullptr;
static int s_clipLen = 0, s_clipBars = 0;
static int8_t s_peaks[160];

static float s_trkAcc[T_COUNT], s_mAcc = 0, s_onsetAcc = 0, s_hatAcc = 0, s_kickAcc = 0;
static uint32_t s_hitCnt[PAD_COUNT], s_seenCnt[PAD_COUNT];

// ---------- commands (main -> render) ----------
enum : uint8_t { C_PAD = 1, C_ON, C_OFF, C_CLEAR, C_PLAY, C_STOP, C_SFX };
struct Cmd { uint8_t t, a, b, c; };

static inline float mtof(int n) { return 440.f * powf(2.f, (n - 69) / 12.f); }
static inline float frnd(uint32_t &s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(int32_t)s / 2147483648.f; }

// ============================================================
//  kit synthesis (same SNES-flavoured kit as Synapse V11)
// ============================================================
static int16_t *palloc(int n) {
  int16_t *p = (int16_t *)heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : (int16_t *)malloc(n * 2);
}
static inline int16_t crush(float v, float g, int bits) {
  float s = tanhf(v * g) / tanhf(g);
  int32_t q = (int32_t)(s * 30000.f);
  return (int16_t)(q & ~((1 << (16 - bits)) - 1));
}
static void synthKit() {
  const float TAU = 6.2831853f;
  uint32_t r = 0x1234567;
  int n = MSR * 32 / 100; int16_t *k = palloc(n); float ph = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / MSR, f = 46.f + 150.f * expf(-t * 28.f) + 250.f * expf(-t * 260.f);
    ph += TAU * f / MSR;
    float env = expf(-t * 7.5f) * (t < 0.0015f ? t / 0.0015f : 1.f);
    k[i] = crush(sinf(ph) * env + (i < 40 ? frnd(r) * 0.5f * (1.f - i / 40.f) : 0.f), 2.2f, 12);
  }
  s_syn[PAD_KICK] = k; s_synLen[PAD_KICK] = n;
  n = MSR * 24 / 100; int16_t *s = palloc(n);
  float p1 = 0, p2 = 0, hx = 0, hy = 0, lz = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / MSR, bend = 1.f + 0.35f * expf(-t * 60.f);
    p1 += TAU * 190.f * bend / MSR; p2 += TAU * 335.f * bend / MSR;
    float tone = (sinf(p1) * 0.6f + sinf(p2) * 0.4f) * expf(-t * 26.f);
    float x = frnd(r); hy = 0.72f * (hy + x - hx); hx = x; lz += 0.55f * (hy - lz);
    s[i] = crush(tone * 0.7f + lz * expf(-t * 13.f) * 1.4f, 1.8f, 10);
  }
  s_syn[PAD_SNARE] = s; s_synLen[PAD_SNARE] = n;
  for (int open = 0; open < 2; open++) {
    n = open ? MSR * 42 / 100 : MSR * 7 / 100;
    int16_t *h = palloc(n);
    static const float fr[6] = {3120.f, 4680.f, 5270.f, 6340.f, 7010.f, 7650.f};
    float phs[6] = {0}, ax = 0, ay = 0, bx = 0, by = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / MSR, m = 0;
      for (int j = 0; j < 6; j++) { phs[j] += fr[j] / MSR; m += (fmodf(phs[j], 1.f) < 0.5f) ? 1.f : -1.f; }
      float x = m * 0.12f + frnd(r) * 0.55f;
      ay = 0.86f * (ay + x - ax); ax = x; by = 0.86f * (by + ay - bx); bx = ay;
      h[i] = crush(by * (open ? expf(-t * 6.5f) : expf(-t * 60.f)) * 1.3f, 1.4f, open ? 10 : 11);
    }
    s_syn[open ? PAD_HAT_O : PAD_HAT_C] = h; s_synLen[open ? PAD_HAT_O : PAD_HAT_C] = n;
  }
  for (int p = 0; p < PAD_COUNT; p++) { s_smp[p] = s_syn[p]; s_len[p] = s_synLen[p]; }
}

// ============================================================
//  voices
// ============================================================
static void padStart(int p) {
  if (p == PAD_HAT_C) s_pv[PAD_HAT_O].on = false;            // hat choke
  s_pv[p] = {s_smp[p], s_len[p], 0, true};
  s_hitCnt[p]++;
  static const float w[PAD_COUNT] = {0.3f, 0.5f, 1.f, 0.8f};
  s_onsetAcc = fmaxf(s_onsetAcc, w[p]);
  if (p <= PAD_HAT_O) s_hatAcc += 0.5f; else if (p == PAD_KICK) s_kickAcc = 1.f;
  if (p == PAD_KICK && !mute[T_DRUM] && vol[T_DRUM] > 0.05f) sub::evKick(heardAt());
}
static void leadStart(int note, int seqOff) {
  int best = 0; float bestScore = 1e9f;
  for (int i = 0; i < 6; i++) {
    float sc = s_lv[i].gate ? 10.f + s_lv[i].env : s_lv[i].env - s_lv[i].age * 0.001f;
    if (s_lv[i].note == note && s_lv[i].gate) { best = i; break; }
    if (sc < bestScore) { bestScore = sc; best = i; }
  }
  SynV &v = s_lv[best];
  v.note = (uint8_t)note; v.gate = true; v.inc = mtof(note) / SR; v.age = 0; v.seqOff = seqOff;
  if (v.env < 0.001f) { v.ph = 0; v.ph2 = 0.25f; }
}
static void leadStop(int note) { for (auto &v : s_lv) if (v.gate && v.note == note && v.seqOff < 0) v.gate = false; }
static void bassStart(int note, int seqOff) {
  bool wasOn = s_bv.gate && s_bv.env > 0.05f;
  s_bv.note = (uint8_t)note; s_bv.gate = true; s_bv.seqOff = seqOff; s_bv.age = 0;
  if (!mute[T_BASS] && vol[T_BASS] > 0.1f) sub::evBassOn((uint8_t)note, heardAt(), wasOn);
  s_bassTarget = mtof(note) / SR;
  if (!wasOn) s_bv.inc = s_bassTarget;                       // glide only when legato
}
static void bassStop(int note) { if (s_bv.gate && s_bv.note == note && s_bv.seqOff < 0) { s_bv.gate = false; sub::evBassOff(heardAt()); } }
static void clickStart(bool acc, float f = 0) { s_click = {0, 1.f, f > 0 ? f : (acc ? 1900.f : 1250.f)}; }

// ============================================================
//  sequencer (inside render)
// ============================================================
static inline bool bitOn(const uint32_t *w, int st) { return (w[st >> 5] >> (st & 31)) & 1; }
static void fireStep(int st) {
  s_step = st;
  for (int p = 0; p < PAD_COUNT; p++) {
    uint32_t bit = 1u << (st & 31);
    if ((s_pat[p][st >> 5] & bit) && !(s_sup[p][st >> 5] & bit)) padStart(p);
    s_sup[p][st >> 5] &= ~bit;
  }
  for (int t = 0; t < 2; t++) {
    // releases first, then new notes
    if (t == 0) { for (auto &v : s_lv) if (v.gate && v.seqOff == st) v.gate = false; }
    else if (s_bv.gate && s_bv.seqOff == st) { s_bv.gate = false; sub::evBassOff(heardAt()); }
    for (int i = 0; i < s_nev[t]; i++) {
      Ev &e = s_ev[t][i];
      if (e.step != st) continue;
      if (e.flags & 1) { e.flags &= ~1; continue; }        // just played live
      if (e.flags & 2) continue;                            // still being held (recording)
      int off = (st + e.len) % STEPS;
      if (t == 0) leadStart(e.note, off); else bassStart(e.note, off);
    }
  }
  if (s_metro && (st & 7) == 0) clickStart((st & 31) == 0);
}
static int quantize(double pos, bool &ahead) {
  double p = pos - LAT_SAMPLES;
  if (p < 0) p += s_stepLen * STEPS;
  double q = p / s_stepLen;
  int n = (int)floor(q + 0.5);
  ahead = n > (int)floor(pos / s_stepLen);                  // playhead hasn't reached it yet
  return ((n % STEPS) + STEPS) % STEPS;
}
static void recNoteOn(int t, int note) {
  if (!s_play || !rec[t == 0 ? T_LEAD : T_BASS] || s_nev[t] >= MAXEV) return;
  bool ahead; int st = quantize(s_pos, ahead);
  Ev &e = s_ev[t][s_nev[t]];
  e = {(uint8_t)st, 1, (uint8_t)note, (uint8_t)(2 | (ahead ? 1 : 0))};
  s_pend[t][note] = (int16_t)s_nev[t]; s_pendPos[t][note] = s_pos;
  s_nev[t]++;
}
static void recNoteOff(int t, int note) {
  int i = s_pend[t][note];
  if (i < 0 || i >= s_nev[t]) return;
  double d = s_pos - s_pendPos[t][note];
  if (d < 0) d += s_stepLen * STEPS;
  int len = (int)floor(d / s_stepLen + 0.5);
  s_ev[t][i].len = (uint8_t)(len < 1 ? 1 : (len > STEPS - 1 ? STEPS - 1 : len));
  s_ev[t][i].flags &= ~2;
  s_pend[t][note] = -1;
}
static void handle(const Cmd &c) {
  switch (c.t) {
    case C_PAD:
      padStart(c.a);
      if (s_play && rec[T_DRUM]) {
        bool ahead; int st = quantize(s_pos, ahead);
        s_pat[c.a][st >> 5] |= 1u << (st & 31);
        if (ahead) s_sup[c.a][st >> 5] |= 1u << (st & 31);
      }
      break;
    case C_ON:
      if (c.a == T_LEAD) leadStart(c.b, -1); else bassStart(c.b, -1);
      recNoteOn(c.a == T_LEAD ? 0 : 1, c.b);
      break;
    case C_OFF:
      if (c.a == T_LEAD) leadStop(c.b); else bassStop(c.b);
      recNoteOff(c.a == T_LEAD ? 0 : 1, c.b);
      break;
    case C_CLEAR:
      if (c.a == T_DRUM) { memset(s_pat, 0, sizeof(s_pat)); memset(s_sup, 0, sizeof(s_sup)); }
      else if (c.a == T_LEAD || c.a == T_BASS) { int t = c.a == T_LEAD ? 0 : 1; s_nev[t] = 0; for (auto &p : s_pend[t]) p = -1; }
      else { s_clipLen = 0; memset(s_peaks, 0, sizeof(s_peaks)); }
      break;
    case C_PLAY: s_pos = 0; s_play = true; fireStep(0); break;
    case C_STOP:
      s_play = false; s_step = -1;
      for (auto &v : s_lv) if (v.seqOff >= 0) v.gate = false;
      if (s_bv.seqOff >= 0 && s_bv.gate) { s_bv.gate = false; sub::evBassOff(heardAt()); }
      break;
    case C_SFX: clickStart(false, 2400.f + c.a * 10.f); break;
  }
}

// ============================================================
//  render
// ============================================================
static void synth(float *out, int n) {
  const uint32_t padInc = (uint32_t)((double)MSR / SR * 65536.0);
  float tone = leadTone;
  for (int i = 0; i < n; i++) {
    float d = 0, l = 0, b = 0, a = 0;
    // drums
    for (int p = 0; p < PAD_COUNT; p++) {
      PadV &v = s_pv[p];
      if (!v.on) continue;
      uint32_t idx = v.pos >> 16;
      if ((int)idx >= v.len - 1) { v.on = false; continue; }
      float fr = (v.pos & 0xFFFF) / 65536.f;
      d += (v.d[idx] + (v.d[idx + 1] - v.d[idx]) * fr) * (1.f / 32768.f);
      v.pos += padInc;
    }
    // lead
    for (auto &v : s_lv) {
      if (!v.gate && v.env < 0.0005f) { v.env = 0; continue; }
      v.age += 1.f;
      float x;
      if (leadSound == LS_PLUCK) {
        v.env = v.gate ? (v.age < 40 ? v.age / 40.f : v.env * 0.99985f) : v.env * 0.9985f;
        if (v.gate && v.age >= 40) v.env = fmaxf(v.env * 0.99975f, 0.f);
        v.ph += v.inc; if (v.ph >= 1.f) v.ph -= 1.f;
        float pw = 0.3f + 0.15f * sinf(v.age * 0.0003f);
        x = v.ph < pw ? 1.f : -1.f;
        float cut = 0.04f + tone * 0.35f + v.env * 0.3f;
        v.lp += cut * (x - v.lp); x = v.lp;
      } else if (leadSound == LS_PAD) {
        float target = v.gate ? 0.85f : 0.f;
        v.env += (target - v.env) * (v.gate ? 0.00025f : 0.00012f);
        v.ph += v.inc; if (v.ph >= 1.f) v.ph -= 1.f;
        v.ph2 += v.inc * 1.006f; if (v.ph2 >= 1.f) v.ph2 -= 1.f;
        x = (v.ph * 2.f - 1.f) * 0.5f + (v.ph2 * 2.f - 1.f) * 0.5f;
        float cut = 0.03f + tone * 0.25f;
        v.lp += cut * (x - v.lp); v.lp2 += cut * (v.lp - v.lp2); x = v.lp2;
      } else {
        v.env = v.gate ? (v.age < 20 ? 1.f : fmaxf(0.6f, v.env * 0.9997f)) : v.env * 0.997f;
        float vib = 1.f + 0.006f * sinf(v.age * 0.0017f) * fminf(1.f, v.age / 8000.f);
        v.ph += v.inc * vib; if (v.ph >= 1.f) v.ph -= 1.f;
        x = v.ph < 0.5f ? 0.6f : -0.6f;
      }
      l += x * v.env * 0.28f;
    }
    // bass (mono, glide)
    if (s_bv.gate || s_bv.env > 0.0005f) {
      SynV &v = s_bv;
      v.age += 1.f;
      v.inc += (s_bassTarget - v.inc) * 0.004f;
      v.ph += v.inc; if (v.ph >= 1.f) v.ph -= 1.f;
      float x;
      if (bassSound == BS_ELECTRO) {
        v.env = v.gate ? (v.age < 30 ? v.age / 30.f : fmaxf(0.7f, v.env * 0.9998f)) : v.env * 0.9985f;
        float saw = v.ph * 2.f - 1.f;
        float f = 0.05f + 0.35f * expf(-v.age * 0.00025f) + tone * 0.08f;
        v.lp += f * v.bp; float hp = saw - v.lp - 0.35f * v.bp; v.bp += f * hp;   // resonant SVF: squelch
        x = v.lp * 0.9f;
      } else if (bassSound == BS_CHIP) {
        v.env = v.gate ? 0.8f : v.env * 0.996f;
        x = v.ph < 0.25f ? 0.7f : -0.7f;
      } else {
        v.env = v.gate ? (v.age < 90 ? v.age / 90.f : fmaxf(0.75f, v.env * 0.99993f)) : v.env * 0.9992f;
        v.ph2 += v.inc * 0.5f; if (v.ph2 >= 1.f) v.ph2 -= 1.f;
        x = sinf(v.ph * 6.2831853f) * 0.6f + sinf(v.ph2 * 6.2831853f) * 0.6f;
        x = x * (1.5f - 0.5f * x * x * 0.4f);
      }
      b = x * v.env * 0.5f;
    }
    // audio clip, locked to the loop
    if (s_play && s_clipLen > 0) {
      int idx = (int)(s_pos * ((double)MSR / SR));
      if (idx < s_clipLen) a = s_clip[idx] * (1.f / 32768.f);
    }
    float mix = (mute[T_DRUM] ? 0 : d * vol[T_DRUM]) + (mute[T_LEAD] ? 0 : l * vol[T_LEAD]) +
                (mute[T_BASS] ? 0 : b * vol[T_BASS]) + (mute[T_AUDIO] ? 0 : a * vol[T_AUDIO]);
    if (s_click.env > 0.001f) {
      s_click.ph += s_click.f / SR; s_click.env *= 0.9965f;
      mix += sinf(s_click.ph * 6.2831853f) * s_click.env * 0.45f;
    }
    mix *= master;
    float y = mix * (27.f + mix * mix) / (27.f + 9.f * mix * mix);          // soft clip
    out[i] = y;
    s_trkAcc[0] += fabsf(d); s_trkAcc[1] += fabsf(l); s_trkAcc[2] += fabsf(b); s_trkAcc[3] += fabsf(a);
    s_mAcc += y * y;
  }
}

static void render(int16_t *dst) {
  static float buf[BLK];
  s_blockMs = millis() + SUB_LAT_MS; s_evOff = 0;
  Cmd c;
  while (xQueueReceive(s_q, &c, 0) == pdTRUE) handle(c);
  int i = 0;
  while (i < BLK) {
    int n = BLK - i;
    if (s_play) {
      double toNext = (floor(s_pos / s_stepLen) + 1.0) * s_stepLen - s_pos;
      int k = (int)ceil(toNext);
      if (k < 1) k = 1;
      if (k < n) n = k;
    }
    synth(buf + i, n);
    i += n;
    s_free += n;
    if (s_play) {
      int before = (int)floor(s_pos / s_stepLen);
      s_pos += n;
      double loopLen = s_stepLen * STEPS;
      if (s_pos >= loopLen) s_pos -= loopLen;
      int now = (int)floor(s_pos / s_stepLen);
      s_evOff = i;
      if (now != before) fireStep(now % STEPS);
    }
    // arpeggiator: 16ths, on the grid when playing
    if (s_arpN) {
      int64_t t16 = s_play ? (int64_t)floor(s_pos / (s_stepLen * 2)) + 1000000 : (int64_t)floor(s_free / (s_stepLen * 2));
      if (t16 != s_arpLast) {
        s_arpLast = t16;
        if (s_arpCur != 255) { leadStop(s_arpCur); recNoteOff(0, s_arpCur); }
        s_arpCur = s_arpNotes[s_arpIdx++ % s_arpN];
        leadStart(s_arpCur, -1); recNoteOn(0, s_arpCur);
      }
    } else if (s_arpCur != 255) { leadStop(s_arpCur); recNoteOff(0, s_arpCur); s_arpCur = 255; }
    if (s_metro && !s_play) {
      int64_t bt = (int64_t)floor((s_free - s_metT0) / (s_stepLen * 8));
      if (bt != s_lastMet && s_free >= s_metT0) { s_lastMet = bt; clickStart((bt & 3) == 0); }
    }
  }
  for (int k = 0; k < BLK; k++) {
    float v = buf[k] * 30000.f;
    dst[k] = (int16_t)(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : v));
  }
}

#ifdef HOST
void hostPump(int blocks, int16_t *out) { for (int b = 0; b < blocks; b++) if (s_spk) render(out + b * BLK); }
#endif
static void audioTask(void *) {
  static int16_t out[4][BLK];
  int ob = 0;
  for (;;) {
    if (!s_spk) { vTaskDelay(4); continue; }
    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (!s_spk || M5.Speaker.isPlaying(0) >= 2) { xSemaphoreGive(s_mux); vTaskDelay(1); continue; }
    render(out[ob]);
    M5.Speaker.playRaw(out[ob], BLK, SR, false, 1, 0, false);
    ob = (ob + 1) & 3;
    xSemaphoreGive(s_mux);
  }
}

// ============================================================
//  mic <-> speaker
// ============================================================
static void toSpeaker() {
  xSemaphoreTake(s_mux, portMAX_DELAY);
  if (s_mic) { M5.Mic.end(); s_mic = false; }
  if (!M5.Speaker.isEnabled()) M5.Speaker.begin();
  M5.Speaker.setVolume(220);
  s_spk = true;
  xSemaphoreGive(s_mux);
}
static void toMic() {
  xSemaphoreTake(s_mux, portMAX_DELAY);
  s_spk = false;
  if (M5.Speaker.isEnabled()) { M5.Speaker.stop(); M5.Speaker.end(); }
  if (!M5.Mic.isEnabled()) M5.Mic.begin();
  s_mic = true;
  xSemaphoreGive(s_mux);
}

// ============================================================
//  sampler (pads) + clip recorder: non-blocking state machines
// ============================================================
static RecStage s_rs = REC_IDLE, s_cs = REC_IDLE;
static int s_recPad = -1;
static uint32_t s_rt = 0, s_ct = 0;
static int16_t *s_recBuf = nullptr;
static uint32_t s_clipCountMs = 0, s_clipRecMs = 0;
static bool s_resume = false;

static int trimSample(int16_t *b, int n) {
  int pk = 1;
  for (int i = 0; i < n; i++) { int a = abs(b[i]); if (a > pk) pk = a; }
  if (pk < 900) return 0;
  int thr = pk / 10; if (thr < 350) thr = 350;
  int st = 0; while (st < n - 16 && abs(b[st]) < thr) st++;
  st -= MSR * 3 / 1000; if (st < 0) st = 0;
  int en = n - 1; while (en > st + 64 && abs(b[en]) < thr / 4) en--;
  en += MSR * 25 / 1000; if (en > n) en = n;
  int k = en - st; if (k < 64) return 0;
  memmove(b, b + st, k * 2);
  float g = fminf(6.f, 26000.f / pk);
  for (int i = 0; i < k; i++) {
    float v = b[i] * g;
    if (i < 16) v *= i / 16.f;
    if (i > k - MSR / 100) v *= (float)(k - i) / (MSR / 100);
    b[i] = (int16_t)clampf(v, -32767.f, 32767.f);
  }
  return k;
}
static void install(int p, int16_t *d, int n, bool user) {
  xSemaphoreTake(s_mux, portMAX_DELAY);
  int16_t *old = s_smp[p];
  s_pv[p].on = false;
  s_smp[p] = d; s_len[p] = n; s_user[p] = user;
  xSemaphoreGive(s_mux);
  if (old && old != s_syn[p]) { if (s_grave[p]) free(s_grave[p]); s_grave[p] = old; s_graveAt[p] = millis(); }
}
void recStart(int pad) { if (s_rs == REC_IDLE && s_cs == REC_IDLE) { s_recPad = pad; s_rs = REC_COUNT; s_rt = millis(); s_resume = s_play; } }
RecStage recStage() { return s_rs; }
int recPad() { return s_recPad; }
float recProgress() {
  uint32_t e = millis() - s_rt;
  return clampf(e / (s_rs == REC_CAPTURE ? 800.f : 900.f), 0.f, 1.f);
}
bool userSample(int p) { return s_user[p]; }
void clearUser(int p) { if (s_user[p]) install(p, s_syn[p], s_synLen[p], false); }

void clipRec(int bars) {
  if (s_rs != REC_IDLE || s_cs != REC_IDLE || !s_clip) return;
  s_clipBars = bars;
  s_cs = REC_COUNT; s_ct = millis();
  s_clipCountMs = (uint32_t)(4 * 60000.f / s_bpm);
  s_clipRecMs = (uint32_t)(bars * 4 * 60000.f / s_bpm);
}
RecStage clipStage() { return s_cs; }
float clipProgress() {
  uint32_t e = millis() - s_ct;
  if (s_cs == REC_COUNT) return clampf((float)e / s_clipCountMs, 0, 1);
  if (s_cs == REC_CAPTURE) return clampf((float)e / s_clipRecMs, 0, 1);
  return clampf(e / 900.f, 0, 1);
}
int clipBars() { return s_clipBars; }
bool hasClip() { return s_clipLen > 0; }
const int8_t *clipPeaks() { return s_peaks; }
static void computePeaks() {
  memset(s_peaks, 0, sizeof(s_peaks));
  if (!s_clipLen) return;
  int loopS = (int)(s_stepLen * STEPS * MSR / SR);
  for (int c = 0; c < 160; c++) {
    int a = c * loopS / 160, b = (c + 1) * loopS / 160, pk = 0;
    for (int i = a; i < b && i < s_clipLen; i += 4) { int v = abs(s_clip[i]); if (v > pk) pk = v; }
    s_peaks[c] = (int8_t)(pk >> 8);
  }
}

static void recService() {
  uint32_t now = millis();
  // --- pad sampler ---
  if (s_rs == REC_COUNT) {
    static int lastTick = -1;
    int tick = (int)((now - s_rt) / 300);
    if (tick != lastTick && tick < 3) { lastTick = tick; hap(90, 25); }
    if (s_spk) { if (s_play) { Cmd c{C_STOP}; xQueueSend(s_q, &c, 0); } toMic(); }
    if (now - s_rt >= 900) {
      lastTick = -1;
      if (!s_recBuf) s_recBuf = palloc(REC_MAX);
      M5.Mic.record(s_recBuf, REC_MAX, MSR);
      s_rs = REC_CAPTURE; s_rt = now; hap(170, 40);
    }
  } else if (s_rs == REC_CAPTURE) {
    if (now - s_rt >= 800 && M5.Mic.isRecording() == 0) {
      int n = trimSample(s_recBuf, REC_MAX);
      int16_t *keep = n > 0 ? palloc(n) : nullptr;
      if (keep) { memcpy(keep, s_recBuf, n * 2); install(s_recPad, keep, n, true); s_rs = REC_OK; hap(200, 60); }
      else { s_rs = REC_QUIET; hap(60, 120); }
      s_rt = now;
      toSpeaker();
      Cmd c{C_PAD, (uint8_t)s_recPad}; if (s_rs == REC_OK) xQueueSend(s_q, &c, 0);
      if (s_resume) { Cmd p{C_PLAY}; xQueueSend(s_q, &p, 0); }
    }
  } else if ((s_rs == REC_OK || s_rs == REC_QUIET) && now - s_rt > 900) s_rs = REC_IDLE;

  // --- clip recorder: 1 bar count-in, N bars capture, haptic metronome throughout ---
  if (s_cs == REC_COUNT || s_cs == REC_CAPTURE) {
    float beatMs = 60000.f / s_bpm;
    static int lastBeat = -1;
    int beat = (int)((now - s_ct) / beatMs);
    if (beat != lastBeat) { lastBeat = beat; hap((beat & 3) == 0 ? 220 : 120, (beat & 3) == 0 ? 60 : 30); }
    if (s_cs == REC_COUNT) {
      if (s_spk) { Cmd c{C_STOP}; xQueueSend(s_q, &c, 0); vTaskDelay(15); toMic(); }
      if (now - s_ct >= s_clipCountMs) {
        int n = (int)((uint64_t)s_clipRecMs * MSR / 1000);
        if (n > CLIP_MAX) n = CLIP_MAX;
        M5.Mic.record(s_clip, n, MSR);
        s_clipLen = 0;
        s_cs = REC_CAPTURE; s_ct = now; lastBeat = -1;
      }
    } else if (now - s_ct >= s_clipRecMs && M5.Mic.isRecording() == 0) {
      int n = (int)((uint64_t)s_clipRecMs * MSR / 1000);
      if (n > CLIP_MAX) n = CLIP_MAX;
      int pk = 1; for (int i = 0; i < n; i += 2) { int a = abs(s_clip[i]); if (a > pk) pk = a; }
      float g = fminf(4.f, 24000.f / pk);
      for (int i = 0; i < n; i++) s_clip[i] = (int16_t)clampf(s_clip[i] * g, -32767.f, 32767.f);
      for (int i = 0; i < 64 && i < n; i++) { s_clip[i] = (int16_t)(s_clip[i] * i / 64); s_clip[n - 1 - i] = (int16_t)(s_clip[n - 1 - i] * i / 64); }
      s_clipLen = n;
      computePeaks();
      s_cs = pk > 600 ? REC_OK : REC_QUIET;
      if (s_cs == REC_QUIET) s_clipLen = 0;
      s_ct = now; lastBeat = -1;
      toSpeaker();
      Cmd p{C_PLAY}; xQueueSend(s_q, &p, 0);
      hap(200, 80);
    }
  } else if ((s_cs == REC_OK || s_cs == REC_QUIET) && now - s_ct > 900) s_cs = REC_IDLE;
}

// ============================================================
//  public API
// ============================================================
bool playing() { return s_play; }
void play() { Cmd c{C_PLAY}; xQueueSend(s_q, &c, 0); }
void stop() { Cmd c{C_STOP}; xQueueSend(s_q, &c, 0); }
float bpm() { return s_bpm; }
void setBpm(float b) {
  b = clampf(b, 50.f, 200.f);
  xSemaphoreTake(s_mux, portMAX_DELAY);
  double nl = SR * 60.0 / b / 8.0;
  s_pos = s_pos / s_stepLen * nl;                          // keep the loop phase
  s_stepLen = nl; s_bpm = b;
  xSemaphoreGive(s_mux);
  computePeaks();
}
float loopPhase() { return s_play ? (float)(s_pos / (s_stepLen * STEPS)) : 0.f; }
int step() { return s_play ? s_step : -1; }
void metronome(bool on) { if (on && !s_metro) metronomeSync(); s_metro = on; }
void metronomeSync() { s_metT0 = s_free; s_lastMet = -1; }
void clearTrack(int t) { Cmd c{C_CLEAR, (uint8_t)t}; xQueueSend(s_q, &c, 0); }
void padHit(int pad) { Cmd c{C_PAD, (uint8_t)pad}; xQueueSend(s_q, &c, 0); }
bool stepOn(int pad, int st) { return bitOn(s_pat[pad], st); }
void noteOn(int trk, int note) { Cmd c{C_ON, (uint8_t)trk, (uint8_t)note}; xQueueSend(s_q, &c, 0); }
void noteOff(int trk, int note) { Cmd c{C_OFF, (uint8_t)trk, (uint8_t)note}; xQueueSend(s_q, &c, 0); }
void arp(const uint8_t *notes, int n) {
  xSemaphoreTake(s_mux, portMAX_DELAY);
  n = n > 4 ? 4 : n;
  bool same = n == s_arpN;
  for (int i = 0; i < n; i++) { if (s_arpNotes[i] != notes[i]) same = false; s_arpNotes[i] = notes[i]; }
  if (!same) { s_arpIdx = 0; s_arpLast = -1; }
  s_arpN = (uint8_t)n;
  xSemaphoreGive(s_mux);
}
const Ev *events(int trk, int &n) { int t = trk == T_LEAD ? 0 : 1; n = s_nev[t]; return s_ev[t]; }
bool speakerLive() { return s_spk; }
void sfx(float pitch) { Cmd c{C_SFX, (uint8_t)(pitch * 60.f)}; xQueueSend(s_q, &c, 0); }
bool sdOk() { return s_sdOk; }

// ---------- project files: /MANTIS/PROJECTS/<n>/ ----------
struct SongHdr { char magic[4]; uint16_t ver; float bpm; uint8_t lead, bass, bars, pad; float vol[T_COUNT]; uint8_t mute[T_COUNT]; int32_t nev[2], clipLen; };
static void pathOf(char *out, int slot, const char *f) { snprintf(out, 48, "/MANTIS/PROJECTS/%d/%s", slot, f); }
bool save(int slot) {
  if (!s_sdOk) return false;
  char p[48], d[40];
  snprintf(d, 40, "/MANTIS/PROJECTS/%d", slot);
  if (!SD.exists("/MANTIS")) SD.mkdir("/MANTIS");
  if (!SD.exists("/MANTIS/PROJECTS")) SD.mkdir("/MANTIS/PROJECTS");
  if (!SD.exists(d)) SD.mkdir(d);
  SongHdr h;
  memcpy(h.magic, "MSTU", 4); h.ver = 1; h.bpm = s_bpm; h.lead = leadSound; h.bass = bassSound; h.bars = (uint8_t)clipBarsSel; h.pad = 0;
  for (int t = 0; t < T_COUNT; t++) { h.vol[t] = vol[t]; h.mute[t] = mute[t]; }
  h.nev[0] = s_nev[0]; h.nev[1] = s_nev[1]; h.clipLen = s_clipLen;
  pathOf(p, slot, "song.bin");
  if (SD.exists(p)) SD.remove(p);
  File f = SD.open(p, FILE_WRITE);
  if (!f) return false;
  f.write((const uint8_t *)&h, sizeof(h));
  f.write((const uint8_t *)s_pat, sizeof(s_pat));
  f.write((const uint8_t *)s_ev, sizeof(s_ev));
  f.close();
  for (int k = 0; k < PAD_COUNT; k++) {
    char n[12]; snprintf(n, 12, "pad%d.raw", k); pathOf(p, slot, n);
    if (SD.exists(p)) SD.remove(p);
    if (s_user[k]) { File g = SD.open(p, FILE_WRITE); if (g) { g.write((const uint8_t *)s_smp[k], s_len[k] * 2); g.close(); } }
  }
  pathOf(p, slot, "clip.raw");
  if (SD.exists(p)) SD.remove(p);
  if (s_clipLen) { File g = SD.open(p, FILE_WRITE); if (g) { g.write((const uint8_t *)s_clip, s_clipLen * 2); g.close(); } }
  return true;
}
bool load(int slot) {
  if (!s_sdOk) return false;
  char p[48];
  pathOf(p, slot, "song.bin");
  File f = SD.open(p, FILE_READ);
  if (!f) return false;
  SongHdr h;
  bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && !memcmp(h.magic, "MSTU", 4);
  if (!ok) { f.close(); return false; }
  stop();
  vTaskDelay(20);
  xSemaphoreTake(s_mux, portMAX_DELAY);
  f.read((uint8_t *)s_pat, sizeof(s_pat));
  f.read((uint8_t *)s_ev, sizeof(s_ev));
  f.close();
  memset(s_sup, 0, sizeof(s_sup));
  s_nev[0] = h.nev[0] < MAXEV ? h.nev[0] : MAXEV; s_nev[1] = h.nev[1] < MAXEV ? h.nev[1] : MAXEV;
  for (int t = 0; t < 2; t++) for (int i = 0; i < s_nev[t]; i++) s_ev[t][i].flags = 0;
  leadSound = h.lead % LS_COUNT; bassSound = h.bass % BS_COUNT; clipBarsSel = h.bars ? h.bars : 2;
  for (int t = 0; t < T_COUNT; t++) { vol[t] = h.vol[t]; mute[t] = h.mute[t]; }
  float b = h.bpm >= 50.f && h.bpm <= 200.f ? h.bpm : 96.f;
  s_stepLen = SR * 60.0 / b / 8.0; s_bpm = b;
  pathOf(p, slot, "clip.raw");
  s_clipLen = 0;
  File g = SD.open(p, FILE_READ);
  if (g) { int n = (int)(g.size() / 2); if (n > CLIP_MAX) n = CLIP_MAX; g.read((uint8_t *)s_clip, n * 2); g.close(); s_clipLen = n; }
  xSemaphoreGive(s_mux);
  computePeaks();
  for (int k = 0; k < PAD_COUNT; k++) {
    char n[12]; snprintf(n, 12, "pad%d.raw", k); pathOf(p, slot, n);
    File q = SD.open(p, FILE_READ);
    if (q) {
      int len = (int)(q.size() / 2); if (len > REC_MAX) len = REC_MAX;
      int16_t *d = len > 32 ? palloc(len) : nullptr;
      if (d) { q.read((uint8_t *)d, len * 2); install(k, d, len, true); }
      q.close();
    } else clearUser(k);
  }
  play();
  return true;
}

void begin() {
  s_mux = xSemaphoreCreateMutex();
  s_q = xQueueCreate(48, sizeof(Cmd));
  for (auto &t : s_pend) for (auto &p : t) p = -1;
  for (auto &v : s_lv) v.seqOff = -1;
  s_bv.seqOff = -1;
  synthKit();
  s_clip = palloc(CLIP_MAX);
  auto sc = M5.Speaker.config();
  sc.dma_buf_len = 128;
  sc.dma_buf_count = 4;
  sc.task_priority = 4;
  M5.Speaker.config(sc);
  SPI.begin(18, 38, 23, -1);
  s_sdOk = SD.begin(4, SPI, 25000000);
  if (s_sdOk) {                                              // factory / user kit
    static const char *nm[PAD_COUNT] = {"hat_closed", "hat_open", "kick", "snare"};
    for (int k = 0; k < PAD_COUNT; k++) {
      char p[48]; snprintf(p, 48, "/MANTIS/SAMPLES/%s.raw", nm[k]);
      File q = SD.open(p, FILE_READ);
      if (!q) continue;
      int len = (int)(q.size() / 2); if (len > REC_MAX) len = REC_MAX;
      int16_t *d = len > 32 ? palloc(len) : nullptr;
      if (d) { q.read((uint8_t *)d, len * 2); s_smp[k] = d; s_len[k] = len; }
      q.close();
    }
  }
  toSpeaker();
  xTaskCreatePinnedToCore(audioTask, "mix", 6144, nullptr, 5, nullptr, 0);
}

void service(float dt) {
  uint32_t now = millis();
  for (int p = 0; p < PAD_COUNT; p++)
    if (s_grave[p] && now - s_graveAt[p] > 600) { free(s_grave[p]); s_grave[p] = nullptr; }
  recService();
  for (int p = 0; p < PAD_COUNT; p++)
    if (s_hitCnt[p] != s_seenCnt[p]) {
      s_seenCnt[p] = s_hitCnt[p]; padFlashMs[p] = now;
      if (p == PAD_SNARE) hap(70, 12);                  // kicks live in the haptic sub (timed to the audio)
    }
  // meters (render accumulates, we drain)
  float n = SR * dt; if (n < 1) n = 1;
  for (int t = 0; t < T_COUNT; t++) { float m = clampf(s_trkAcc[t] / n * 3.f, 0, 1); s_trkAcc[t] = 0; meter[t] = fmaxf(m, meter[t] * 0.85f); }
  float rms = sqrtf(s_mAcc / n); s_mAcc = 0;
  level = level * 0.6f + clampf(rms * 2.5f, 0, 1.4f) * 0.4f;
  peak = peak * 0.7f + clampf(rms * 3.5f, 0, 1.4f) * 0.3f;
  onset = s_onsetAcc; s_onsetAcc = 0;
  zcr = zcr * 0.8f + clampf(s_hatAcc, 0, 1) * 0.2f; s_hatAcc *= 0.5f;
  bass = bass * 0.7f + s_kickAcc * 0.3f; s_kickAcc *= 0.6f;
  if (s_play) { beatPos = (float)(s_pos / (s_stepLen * 8)); beatConf = 1.f; }
  else { beatPos += dt * s_bpm / 60.f; beatConf *= 0.97f; }
}

}  // namespace aud
