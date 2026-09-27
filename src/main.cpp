// ============================================================
//  MANTIS STUDIO — a tiny, performance-first music workstation · Core2
//  Screens (A / C): DRUM  LEAD  BASS  AUDIO  MIX  MANTIS
//  B: context (record when something is armed, otherwise play / stop)
//  hold B: tap tempo.   Touch is the instrument.
// ============================================================
#include "app.h"
#include "audio.h"
#include "sub.h"
#include "fx.h"
#include "mantis_splash.h"
#include <string.h>

M5Canvas *g_cv = nullptr;
float g_t = 0, g_dt = 0.033f, g_hue = 150;
float g_level = 0, g_peak = 0, g_lookX = 0, g_lookY = 0;
int16_t g_mic[MIC_N];
bool g_shakeKick = false;
static float g_ax = 0, g_ay = 0, g_az = 1, g_shake = 0;
static uint32_t g_shakeAt = 0;

// ---------------- haptics: everything goes through the haptic subwoofer (sub.cpp) ----------------
void hap(uint8_t level, uint16_t ms) { sub::tap(level, ms); }
void kickSubHaptic() { sub::evKick(millis()); }
static void hapService() { sub::service(); }

// ---------------- frame pipeline (render core 1, LCD push core 0) ----------------
static M5Canvas s_fbA(&M5.Display), s_fbB(&M5.Display);
static M5Canvas *s_fb[2] = {&s_fbA, &s_fbB};
static int s_cur = 0;
static bool s_double = false;
static TaskHandle_t s_dispTask = nullptr;
static SemaphoreHandle_t s_dispIdle = nullptr;
static volatile int s_pushIdx = 0;
static void displayTask(void *) {
  for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); s_fb[s_pushIdx]->pushSprite(&M5.Display, 0, 0); xSemaphoreGive(s_dispIdle); }
}
static void pollInput();
static void present() {
  if (!s_double) { s_fb[0]->pushSprite(&M5.Display, 0, 0); pollInput(); return; }
  while (xSemaphoreTake(s_dispIdle, 0) != pdTRUE) { pollInput(); vTaskDelay(1); }
  s_pushIdx = s_cur; xTaskNotifyGive(s_dispTask); s_cur ^= 1; g_cv = s_fb[s_cur];
}
#ifdef HOST
M5Canvas *hostLastFrame() { return s_fb[s_pushIdx]; }
#endif

// ---------------- state ----------------
enum Screen : uint8_t { S_DRUM = 0, S_LEAD, S_BASS, S_AUDIO, S_MIX, S_MANTIS, S_COUNT };
static const char *SCR_NAME[] = {"DRUM", "LEAD", "BASS", "AUDIO", "MIX", "MANTIS"};
static const uint16_t SCR_COL[] = {0x07F0, 0xFD20, 0x3E7F, 0xF8B2, 0xC618, 0x87E0};
static uint8_t s_scr = S_DRUM;
static const int HDR = 22, FTR = 14, CY0 = HDR, CY1 = H - FTR;

static const int8_t SCALES[5][8] = {{0, 2, 4, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 8, 10, -1}, {0, 2, 3, 5, 7, 9, 10, -1},
                                    {0, 3, 5, 7, 10, -1, -1, -1}, {0, 3, 5, 6, 7, 10, -1, -1}};
static const char *SCALE_N[] = {"major", "minor", "dorian", "pent", "blues"};
static const char *KEY_N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *LS_N[] = {"pluck", "pad", "chip"};
static const char *BS_N[] = {"electro", "chip", "dub"};
static const char *PM_N[] = {"note", "chord", "arp"};
static const char *PAD_N[] = {"HAT", "OPEN", "KICK", "SNARE"};
static int s_key = 9, s_scale = 1, s_play = 0, s_bassOct = 2, s_slot = 1;
static bool s_padArmed[4], s_clipArmed = false, s_tempo = false;
static uint32_t s_taps[8]; static int s_tapN = 0;
static char s_toast[28]; static uint32_t s_toastUntil = 0;
static void toast(const char *t) { strncpy(s_toast, t, 27); s_toast[27] = 0; s_toastUntil = millis() + 1400; }

static int scaleLen() { int n = 0; while (n < 8 && SCALES[s_scale][n] >= 0) n++; return n; }
static int degNote(int base, int deg) { int n = scaleLen(); return base + 12 * (deg / n) + SCALES[s_scale][deg % n]; }
static int trackOf(uint8_t s) { return s == S_DRUM ? T_DRUM : s == S_LEAD ? T_LEAD : s == S_BASS ? T_BASS : s == S_AUDIO ? T_AUDIO : -1; }

// ---------------- widgets ----------------
struct Rect { int x, y, w, h; bool in(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
static void chip(const Rect &r, const char *label, uint16_t col, bool on, float fill = 0) {
  canvas.fillRoundRect(r.x, r.y, r.w, r.h, 5, on ? col : rgb565(28, 26, 44));
  if (fill > 0) canvas.fillRoundRect(r.x, r.y, (int)(r.w * fill), r.h, 5, rgb565(255, 90, 90));
  canvas.drawRoundRect(r.x, r.y, r.w, r.h, 5, col);
  canvas.setTextSize(1); canvas.setTextColor(on ? rgb565(10, 10, 20) : rgb565(230, 230, 240));
  int tw = (int)strlen(label) * 6;
  canvas.setCursor(r.x + (r.w - tw) / 2, r.y + (r.h - 7) / 2); canvas.print(label);
}
static const Rect R_REC = {214, 3, 40, 16}, R_CLR = {258, 3, 58, 16};
static const Rect R_CH[5] = {{4, 25, 44, 18}, {52, 25, 76, 18}, {132, 25, 66, 18}, {202, 25, 74, 18}, {280, 25, 36, 18}};
static const int KY0 = 46, KY1 = 192, ROLL0 = 195;
static const Rect R_BARS = {12, 172, 76, 20}, R_AMUTE = {232, 172, 76, 20};
static const Rect R_SUB = {256, 156, 60, 22}, R_TUNE = {256, 182, 60, 22};
static const Rect R_SLOT = {256, 26, 60, 22}, R_SAVE = {256, 54, 60, 26}, R_LOAD = {256, 86, 60, 26}, R_BM = {256, 120, 28, 24}, R_BP = {288, 120, 28, 24};

// ---------------- hold tracking (chips that need a deliberate hold) ----------------
static int s_holdId = -1; static uint32_t s_holdAt = 0; static bool s_holdDone = false;
static float holdFrac(int id) { return (s_holdId == id && !s_holdDone) ? clampf((millis() - s_holdAt) / 1000.f, 0, 1) : 0.f; }

// ---------------- keyboard slots (multi-touch) ----------------
struct Slot { int key; uint8_t notes[3]; int n; int x0, y0; uint32_t t0; };
static Slot s_sl[2] = {{-1}, {-1}};
static void arpUpdate() {
  uint8_t all[4]; int n = 0;
  for (auto &s : s_sl) if (s.key >= 0) for (int i = 0; i < s.n && n < 4; i++) all[n++] = s.notes[i];
  aud::arp(all, n);
}
static int keyAt(int x, int y) {
  if (y < KY0 || y >= KY1) return -1;
  int row = (y - KY0) * 2 / (KY1 - KY0);
  return (1 - row) * 8 + x * 8 / W;
}
static void keyPress(int slot, int key) {
  Slot &s = s_sl[slot];
  bool lead = s_scr == S_LEAD;
  int base = lead ? 57 + s_key % 12 : 12 * (s_bassOct + 1) + s_key % 12;
  s.key = key; s.n = 0;
  s.notes[s.n++] = (uint8_t)degNote(base, key);
  if (lead && s_play >= 1) { s.notes[s.n++] = (uint8_t)degNote(base, key + 2); s.notes[s.n++] = (uint8_t)degNote(base, key + 4); }
  if (lead && s_play == 2) { arpUpdate(); return; }
  for (int i = 0; i < s.n; i++) aud::noteOn(lead ? T_LEAD : T_BASS, s.notes[i]);
  hap(35, 8);
}
static void keyRelease(int slot) {
  Slot &s = s_sl[slot];
  if (s.key < 0) return;
  bool lead = s_scr == S_LEAD;
  s.key = -1;
  if (lead && s_play == 2) { arpUpdate(); return; }
  for (int i = 0; i < s.n; i++) aud::noteOff(lead ? T_LEAD : T_BASS, s.notes[i]);
}
static void releaseAll() { for (int i = 0; i < 2; i++) keyRelease(i); aud::arp(nullptr, 0); }

// ---------------- pads (DRUM) ----------------
static int s_padHold[2] = {-1, -1}; static uint32_t s_padHoldAt[2];
static int padAt(int x, int y) { if (y < CY0 || y >= CY1) return -1; return ((y - CY0) * 2 / (CY1 - CY0)) * 2 + (x < W / 2 ? 0 : 1); }

// ---------------- screen switching / B ----------------
static void setScreen(int dir) {
  releaseAll();
  s_scr = (uint8_t)((s_scr + dir + S_COUNT) % S_COUNT);
  s_tempo = false; aud::metronome(false);
  for (auto &a : s_padArmed) a = false;
  s_clipArmed = false;
  hap(80, 18);
}
static void bShort() {
  if (s_tempo) { s_tempo = false; aud::metronome(false); hap(100, 30); return; }
  for (int i = 0; i < 4; i++) if (s_padArmed[i]) { s_padArmed[i] = false; aud::recStart(i); return; }
  if (s_clipArmed) { s_clipArmed = false; aud::clipRec(aud::clipBarsSel); return; }
  if (aud::playing()) aud::stop(); else aud::play();
  hap(90, 20);
}
static void tempoTap() {
  uint32_t now = millis();
  if (s_tapN && now - s_taps[s_tapN - 1] > 2000) s_tapN = 0;
  if (s_tapN == 8) { memmove(s_taps, s_taps + 1, 28); s_tapN = 7; }
  s_taps[s_tapN++] = now;
  if (s_tapN >= 2) {
    float sum = 0; int c = 0;
    for (int i = 1; i < s_tapN; i++) { float d = (float)(s_taps[i] - s_taps[i - 1]); if (d > 250 && d < 1500) { sum += d; c++; } }
    if (c) aud::setBpm(60000.f / (sum / c));
  }
  aud::metronomeSync(); hap(70, 15);
}

// ---------------- tap / hold handlers per screen ----------------
static void onTap(int x, int y) {
  int t = trackOf(s_scr);
  if (t >= 0 && t != T_AUDIO && R_REC.in(x, y)) { aud::rec[t] = !aud::rec[t]; hap(60, 15); return; }
  if (t >= 0 && R_CLR.in(x, y)) { s_holdId = 100; s_holdAt = millis(); s_holdDone = false; return; }
  if (y < HDR) return;
  if (s_tempo) { tempoTap(); return; }
  switch (s_scr) {
    case S_LEAD: case S_BASS:
      if (R_CH[0].in(x, y)) s_key = (s_key + 1) % 12;
      else if (R_CH[1].in(x, y)) s_scale = (s_scale + 1) % 5;
      else if (R_CH[2].in(x, y)) { if (s_scr == S_LEAD) { releaseAll(); s_play = (s_play + 1) % 3; } else s_bassOct = s_bassOct % 3 + 1; }
      else if (R_CH[3].in(x, y)) { if (s_scr == S_LEAD) aud::leadSound = (aud::leadSound + 1) % aud::LS_COUNT; else aud::bassSound = (aud::bassSound + 1) % aud::BS_COUNT; }
      else return;
      hap(50, 10);
      break;
    case S_AUDIO:
      if (R_BARS.in(x, y)) { aud::clipBarsSel = aud::clipBarsSel >= 4 ? 1 : aud::clipBarsSel * 2; hap(50, 10); }
      else if (R_AMUTE.in(x, y)) { aud::mute[T_AUDIO] = !aud::mute[T_AUDIO]; hap(50, 10); }
      else if ((x - 160) * (x - 160) + (y - 190) * (y - 190) < 34 * 34) { s_clipArmed = !s_clipArmed; hap(90, 25); if (s_clipArmed) toast("armed: B to record"); }
      break;
    case S_MIX:
      if (R_SLOT.in(x, y)) { s_slot = s_slot % 4 + 1; hap(50, 10); }
      else if (R_SAVE.in(x, y)) { s_holdId = 101; s_holdAt = millis(); s_holdDone = false; }
      else if (R_LOAD.in(x, y)) { s_holdId = 102; s_holdAt = millis(); s_holdDone = false; }
      else if (R_SUB.in(x, y)) { sub::mode = (sub::mode + 1) % sub::MODE_COUNT; hap(120, 30); }
      else if (R_TUNE.in(x, y)) { s_holdId = 103; s_holdAt = millis(); s_holdDone = false; }
      else if (R_BM.in(x, y)) aud::setBpm(aud::bpm() - 1);
      else if (R_BP.in(x, y)) aud::setBpm(aud::bpm() + 1);
      else if (y < 46 && x < 250) { int i = x / 50; if (i < 4) { aud::mute[i] = !aud::mute[i]; s_holdId = 110 + i; s_holdAt = millis(); s_holdDone = false; } }
      break;
    case S_MANTIS: mantisTap(x, y); break;
    default: break;
  }
}
static void present();
static void tuneProgress(float u, float hz) {
  canvas.fillSprite(rgb565(6, 4, 14));
  canvas.setTextSize(2); canvas.setTextColor(rgb565(120, 255, 170));
  canvas.setCursor(40, 60); canvas.print("tuning the sub");
  canvas.setTextSize(1); canvas.setTextColor(rgb565(200, 200, 220));
  canvas.setCursor(40, 92); canvas.print("leave the Core2 still on a table");
  canvas.fillRect(40, 120, (int)(240 * u), 8, rgb565(0, 115, 115));
  canvas.drawRect(40, 120, 240, 8, rgb565(93, 0, 93));
  canvas.setCursor(40, 142);
  if (hz > 0) canvas.printf("rotor: %.0f Hz", hz); else canvas.print("rotor: too slow to spin");
  present();
}
static void tuneSub() {
  bool was = aud::playing();
  if (was) aud::stop();
  delay(120);
  bool ok = sub::tune(tuneProgress);
  char b[40];
  if (ok) snprintf(b, 40, "sub tuned %.0f-%.0f Hz", sub::bandLo(), sub::bandHi()); else snprintf(b, 40, "couldn't measure - kept");
  toast(b);
  if (was) aud::play();
}
static void holdFire(int id) {
  if (id == 100) {
    int t = trackOf(s_scr);
    aud::clearTrack(t);
    if (t == T_DRUM) for (int p = 0; p < 4; p++) aud::clearUser(p);
    toast("track cleared");
  } else if (id == 101) toast(aud::save(s_slot) ? "project saved" : "no SD card");
  else if (id == 102) toast(aud::load(s_slot) ? "project loaded" : (aud::sdOk() ? "empty slot" : "no SD card"));
  else if (id == 103) { tuneSub(); return; }
  else if (id >= 110) { aud::mute[id - 110] = !aud::mute[id - 110]; aud::clearTrack(id - 110); toast("track cleared"); }
  hap(160, 60);
}

// ---------------- input ----------------
static uint32_t s_bDown = 0; static bool s_bHeld = false;
static void pollInput() {
  M5.update();
  hapService();
  uint32_t now = millis();
  if (M5.BtnA.wasPressed()) setScreen(-1);
  if (M5.BtnC.wasPressed()) setScreen(1);
  if (M5.BtnB.wasPressed()) { s_bDown = now; s_bHeld = false; }
  if (M5.BtnB.isPressed() && s_bDown && !s_bHeld && now - s_bDown > 650) { s_bHeld = true; s_tempo = true; s_tapN = 0; aud::metronome(true); hap(150, 50); }
  if (M5.BtnB.wasReleased()) { if (!s_bHeld && s_bDown) bShort(); s_bDown = 0; }
  bool anyBtn = M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnC.isPressed();

  int n = M5.Touch.getCount();
  if (n == 0) {
    for (int i = 0; i < 2; i++) { keyRelease(i); s_padHold[i] = -1; }
    s_holdId = -1;
  }
  for (int i = 0; i < n && i < 2; i++) {
    auto td = M5.Touch.getDetail(i);
    if (td.y >= CY1) { if (td.wasReleased()) { keyRelease(i); s_padHold[i] = -1; } continue; }   // footer / hardware buttons
    if (td.wasReleased()) { keyRelease(i); s_padHold[i] = -1; if (s_holdId >= 0 && !s_holdDone) s_holdId = -1; continue; }
    bool keys = (s_scr == S_LEAD || s_scr == S_BASS) && !s_tempo;
    if (td.wasPressed()) {
      if (anyBtn) continue;
      if (s_scr == S_DRUM && !s_tempo && td.y >= CY0) {
        int id = padAt(td.x, td.y);
        if (id < 0) continue;
        if (aud::recStage() == aud::REC_COUNT || aud::recStage() == aud::REC_CAPTURE) continue;
        if (s_padArmed[id]) { s_padArmed[id] = false; if (aud::userSample(id)) { aud::clearUser(id); toast("slot cleared"); } hap(80, 40); continue; }
        aud::padHit(id); s_padHold[i] = id; s_padHoldAt[i] = now;
        continue;
      }
      if (keys && keyAt(td.x, td.y) >= 0) { s_sl[i].x0 = td.x; keyPress(i, keyAt(td.x, td.y)); continue; }
      onTap(td.x, td.y);
    } else if (td.isPressed()) {
      if (keys && s_sl[i].key >= 0) {                          // glide across keys
        int k = keyAt(td.x, td.y);
        if (k >= 0 && k != s_sl[i].key) { keyRelease(i); keyPress(i, k); }
      }
      if (s_scr == S_DRUM && s_padHold[i] >= 0 && now - s_padHoldAt[i] > 1000) {
        if (padAt(td.x, td.y) == s_padHold[i]) {
          for (int p = 0; p < 4; p++) s_padArmed[p] = p == s_padHold[i];
          toast("armed: B to sample"); hap(160, 60);
        }
        s_padHold[i] = -1;
      }
      if (s_scr == S_MIX && td.y > 50 && td.y < 212 && td.x < 250) {   // faders
        int st = td.x / 50; float v = clampf((200.f - td.y) / 150.f, 0.f, 1.f);
        if (st < 4) aud::vol[st] = v; else aud::master = v;
      }
      if (s_holdId >= 0 && !s_holdDone && now - s_holdAt > 1000) { s_holdDone = true; holdFire(s_holdId); }
    }
  }
}

// ---------------- sensors ----------------
static void sampleImu() {
  if (!M5.Imu.update()) return;
  auto d = M5.Imu.getImuData();
  g_ax = g_ax * 0.7f + d.accel.x * 0.3f; g_ay = g_ay * 0.7f + d.accel.y * 0.3f; g_az = g_az * 0.7f + d.accel.z * 0.3f;
  float tx = -g_ax, ty = g_ay;                                  // IMU X = screen right, Y = screen up
  g_lookX += (tx - g_lookX) * 0.15f; g_lookY += (ty - g_lookY) * 0.15f;
  g_lookX = clampf(g_lookX, -1.1f, 1.1f); g_lookY = clampf(g_lookY, -1.1f, 1.1f);
  float mag = sqrtf(d.accel.x * d.accel.x + d.accel.y * d.accel.y + d.accel.z * d.accel.z);
  g_shake = g_shake * 0.8f + fabsf(mag - 1.f) * 0.2f;
  if (g_shake > 0.5f && millis() - g_shakeAt > 350) { g_shakeAt = millis(); g_shakeKick = true; }
}

// ============================================================
//  drawing
// ============================================================
static void drawHeader() {
  bool play = aud::playing();
  float ph = aud::loopPhase() * 16.f;
  int beat = (int)ph; float bf = ph - beat;
  if (s_tempo && !play) { float bp = fmodf(aud::beatPos, 4.f); beat = (int)bp; bf = bp - beat; }
  float flash = (play || s_tempo) ? (1.f - bf) * (1.f - bf) * ((beat & 3) == 0 ? 1.f : 0.5f) : 0.f;
  canvas.fillRect(0, 0, W, HDR, rgb565((uint8_t)(12 + flash * 90), (uint8_t)(8 + flash * 40), (uint8_t)(24 + flash * 60)));
  if (play) canvas.fillTriangle(6, 5, 6, 17, 16, 11, rgb565(120, 255, 140));
  else canvas.fillRect(6, 6, 10, 10, rgb565(200, 200, 210));
  canvas.setTextSize(1); canvas.setTextColor(rgb565(240, 240, 255));
  canvas.setCursor(22, 8); canvas.printf("%d.%d", play ? beat / 4 + 1 : 1, play ? (beat & 3) + 1 : 1);
  for (int k = 0; k < 4; k++) {
    bool cur = (play || s_tempo) && (beat & 3) == k;
    canvas.fillCircle(52 + k * 12, 11, cur ? 5 : 3, cur ? (k == 0 ? rgb565(255, 80, 80) : rgb565(255, 230, 110)) : rgb565(70, 70, 100));
  }
  canvas.setTextColor(SCR_COL[s_scr]); canvas.setCursor(106, 8); canvas.print(SCR_NAME[s_scr]);
  canvas.setTextColor(rgb565(190, 190, 210)); canvas.setCursor(152, 8); canvas.printf("%dbpm", (int)(aud::bpm() + 0.5f));
  int t = trackOf(s_scr);
  if (t >= 0) {
    if (t != T_AUDIO) chip(R_REC, "REC", rgb565(255, 70, 70), aud::rec[t] && ((millis() / 400) & 1 || !play));
    chip(R_CLR, "hold CLR", rgb565(160, 160, 190), false, holdFrac(100));
  }
  // loop position
  if (play) canvas.fillRect(0, HDR - 2, (int)(aud::loopPhase() * W), 2, SCR_COL[s_scr]);
}
static void drawFooter() {
  canvas.fillRect(0, CY1, W, FTR, rgb565(8, 6, 16));
  canvas.setTextSize(1);
  canvas.setTextColor(rgb565(130, 130, 160));
  canvas.setCursor(6, CY1 + 4); canvas.print(SCR_NAME[(s_scr + S_COUNT - 1) % S_COUNT]);
  const char *b = s_tempo ? "done" : (s_padArmed[0] || s_padArmed[1] || s_padArmed[2] || s_padArmed[3] || s_clipArmed) ? "RECORD" : (aud::playing() ? "stop" : "play");
  canvas.setTextColor(SCR_COL[s_scr]); canvas.setCursor(W / 2 - (int)strlen(b) * 3, CY1 + 4); canvas.print(b);
  canvas.setTextColor(rgb565(130, 130, 160));
  const char *nx = SCR_NAME[(s_scr + 1) % S_COUNT];
  canvas.setCursor(W - 6 - (int)strlen(nx) * 6, CY1 + 4); canvas.print(nx);
}

static void drawDrum() {
  uint32_t now = millis();
  static const uint16_t base[4] = {rgb565(0, 150, 140), rgb565(170, 30, 150), rgb565(70, 190, 35), rgb565(40, 80, 170)};
  int st = aud::step(), ph = (CY1 - CY0) / 2;
  for (int i = 0; i < 4; i++) {
    int px = (i % 2) * (W / 2), py = CY0 + (i / 2) * ph;
    uint32_t age = now - aud::padFlashMs[i];
    float hf = aud::padFlashMs[i] && age < 170 ? 1.f - age / 170.f : 0.f;
    uint16_t c = base[i];
    uint8_t r = ((c >> 11) & 31) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    c = rgb565((uint8_t)fminf(255, r + hf * 140), (uint8_t)fminf(255, g + hf * 140), (uint8_t)fminf(255, b + hf * 140));
    if (s_padArmed[i] && ((now / 180) & 1)) c = rgb565(255, 230, 90);
    int in = 4 - (int)(hf * 3);
    canvas.fillRoundRect(px + in, py + in, W / 2 - 2 * in, ph - 2 * in, 10, c);
    canvas.drawRoundRect(px + in, py + in, W / 2 - 2 * in, ph - 2 * in, 10, rgb565(255, 255, 255));
    canvas.setTextSize(2); canvas.setTextColor(rgb565(12, 10, 24));
    canvas.setCursor(px + W / 4 - (int)strlen(PAD_N[i]) * 6, py + 10); canvas.print(PAD_N[i]);
    canvas.setTextSize(1);
    if (aud::userSample(i)) { canvas.setCursor(px + 12, py + 10); canvas.print("SMP"); }
    for (int s = 0; s < 64; s++) {
      int bx = px + 16 + (s & 15) * 8, by = py + 36 + (s >> 4) * 13;
      bool on = aud::stepOn(i, s * 2) || aud::stepOn(i, s * 2 + 1);
      uint16_t dc = (st >= 0 && (st >> 1) == s) ? rgb565(255, 220, 60) : on ? rgb565(255, 255, 255) : ((s & 3) == 0 ? rgb565(20, 20, 40) : rgb565(40, 40, 70));
      canvas.fillRect(bx, by, on ? 5 : 3, on ? 5 : 3, dc);
    }
    for (int sl = 0; sl < 2; sl++)
      if (s_padHold[sl] == i && now - s_padHoldAt[sl] > 200)
        canvas.fillRect(px + 12, py + ph - 12, (int)((W / 2 - 24) * clampf((now - s_padHoldAt[sl] - 200) / 800.f, 0, 1)), 3, rgb565(255, 230, 90));
  }
  aud::RecStage rs = aud::recStage();
  if (rs != aud::REC_IDLE) {
    float u = aud::recProgress();
    canvas.fillRoundRect(60, 80, 200, 80, 12, rs == aud::REC_CAPTURE ? rgb565(120, 10, 20) : rgb565(20, 12, 40));
    canvas.setTextSize(3); canvas.setTextColor(rgb565(255, 240, 200));
    if (rs == aud::REC_COUNT) { canvas.setCursor(150, 98); canvas.printf("%d", 3 - (int)(u * 3.f)); }
    else if (rs == aud::REC_CAPTURE) { canvas.setCursor(124, 94); canvas.print("REC"); canvas.fillRect(80, 136, (int)(160 * u), 5, rgb565(255, 80, 80)); }
    else { canvas.setTextSize(2); canvas.setCursor(rs == aud::REC_OK ? 124 : 106, 110); canvas.print(rs == aud::REC_OK ? "GOT IT" : "too quiet"); }
    canvas.setTextSize(1); canvas.setCursor(140, 148); canvas.print(PAD_N[aud::recPad()]);
  }
}

static void drawRoll(int trk, uint16_t col) {
  canvas.fillRect(0, ROLL0, W, CY1 - ROLL0, rgb565(10, 8, 20));
  for (int b = 1; b < 4; b++) canvas.drawFastVLine(b * W / 4, ROLL0, CY1 - ROLL0, rgb565(40, 40, 60));
  int n; const aud::Ev *e = aud::events(trk, n);
  int lo = trk == T_LEAD ? 55 : 22, span = 44;
  for (int i = 0; i < n; i++) {
    int x = e[i].step * W / aud::STEPS, w = e[i].len * W / aud::STEPS; if (w < 2) w = 2;
    int y = CY1 - 3 - (e[i].note - lo) * (CY1 - ROLL0 - 5) / span;
    canvas.fillRect(x, clampf(y, ROLL0 + 1, CY1 - 3), w, 2, col);
  }
  if (aud::playing()) canvas.drawFastVLine((int)(aud::loopPhase() * W), ROLL0, CY1 - ROLL0, rgb565(255, 255, 255));
}
static void drawKeys() {
  bool lead = s_scr == S_LEAD;
  uint16_t col = SCR_COL[s_scr];
  char kb[8]; snprintf(kb, 8, "key %s", KEY_N[s_key]);
  chip(R_CH[0], kb, col, false);
  chip(R_CH[1], SCALE_N[s_scale], col, false);
  char b2[12]; if (lead) snprintf(b2, 12, "%s", PM_N[s_play]); else snprintf(b2, 12, "oct %d", s_bassOct);
  chip(R_CH[2], b2, col, lead && s_play > 0);
  chip(R_CH[3], lead ? LS_N[aud::leadSound] : BS_N[aud::bassSound], col, false);
  // tone (tilt) meter
  canvas.drawRoundRect(R_CH[4].x, R_CH[4].y, R_CH[4].w, R_CH[4].h, 5, col);
  canvas.fillRect(R_CH[4].x + 3, R_CH[4].y + 12, (int)((R_CH[4].w - 6) * aud::leadTone), 3, col);
  canvas.setCursor(R_CH[4].x + 6, R_CH[4].y + 3); canvas.setTextColor(rgb565(200, 200, 220)); canvas.print("tilt");
  int len = scaleLen();
  float kw = W / 8.f, kh = (KY1 - KY0) / 2.f;
  for (int k = 0; k < 16; k++) {
    int row = k / 8, colI = k % 8;
    int x = (int)(colI * kw), y = (int)(KY0 + (1 - row) * kh);
    bool held = (s_sl[0].key == k) || (s_sl[1].key == k);
    bool tonic = (k % len) == 0;
    float hue = (float)((SCALES[s_scale][k % len] + s_key) % 12) * 30.f;
    uint16_t c = held ? rgb565(255, 255, 255) : hsv565(hue, tonic ? 0.85f : 0.55f, tonic ? 0.75f : 0.45f);
    canvas.fillRoundRect(x + 2, y + 2, (int)kw - 4, (int)kh - 4, 8, c);
    if (held) canvas.drawRoundRect(x + 1, y + 1, (int)kw - 2, (int)kh - 2, 8, col);
    int note = degNote(0, k) % 12;
    canvas.setTextColor(held ? rgb565(20, 20, 30) : rgb565(240, 240, 250));
    canvas.setCursor(x + (int)kw / 2 - 6, y + (int)kh - 16); canvas.print(KEY_N[(note + s_key) % 12]);
  }
  drawRoll(lead ? T_LEAD : T_BASS, col);
}
static void drawAudio() {
  uint16_t col = SCR_COL[S_AUDIO];
  canvas.fillRoundRect(8, 30, 304, 130, 8, rgb565(14, 10, 26));
  for (int b = 1; b < 4; b++) canvas.drawFastVLine(8 + b * 76, 30, 130, rgb565(40, 36, 60));
  const int8_t *pk = aud::clipPeaks();
  if (aud::hasClip()) {
    for (int c = 0; c < 152; c++) {
      int v = pk[c * 160 / 152] * 60 / 127;
      canvas.drawFastVLine(8 + c * 2, 95 - v, 2 * v + 1, aud::mute[T_AUDIO] ? rgb565(90, 90, 110) : col);
    }
  } else { canvas.setTextColor(rgb565(140, 130, 170)); canvas.setCursor(98, 90); canvas.print("no clip yet: arm + B"); }
  if (aud::playing()) canvas.drawFastVLine(8 + (int)(aud::loopPhase() * 304), 30, 130, rgb565(255, 255, 255));
  char bb[12]; snprintf(bb, 12, "bars %d", aud::clipBarsSel);
  chip(R_BARS, bb, col, false);
  chip(R_AMUTE, "mute", col, aud::mute[T_AUDIO]);
  bool blink = s_clipArmed && ((millis() / 200) & 1);
  canvas.fillCircle(160, 190, 28, rgb565(40, 10, 20));
  canvas.fillCircle(160, 190, 22, blink ? rgb565(255, 230, 90) : rgb565(220, 30, 50));
  canvas.drawCircle(160, 190, 28, rgb565(255, 120, 140));
  aud::RecStage cs = aud::clipStage();
  if (cs != aud::REC_IDLE) {
    float u = aud::clipProgress();
    canvas.fillRoundRect(40, 44, 240, 100, 14, cs == aud::REC_CAPTURE ? rgb565(110, 10, 24) : rgb565(20, 12, 40));
    canvas.setTextColor(rgb565(255, 240, 210));
    if (cs == aud::REC_COUNT) { canvas.setTextSize(4); canvas.setCursor(148, 64); canvas.printf("%d", 4 - (int)(u * 4.f)); }
    else if (cs == aud::REC_CAPTURE) {
      canvas.setTextSize(3); canvas.setCursor(124, 62); canvas.print("REC");
      int beats = aud::clipBars() * 4, cur = (int)(u * beats);
      for (int k = 0; k < beats && k < 16; k++) canvas.fillCircle(64 + k * 12 + (beats < 16 ? (16 - beats) * 6 : 0), 108, k == cur ? 5 : 3, k == cur ? rgb565(255, 230, 110) : rgb565(120, 60, 80));
      canvas.fillRect(60, 124, (int)(200 * u), 5, rgb565(255, 90, 90));
    } else { canvas.setTextSize(2); canvas.setCursor(cs == aud::REC_OK ? 124 : 106, 84); canvas.print(cs == aud::REC_OK ? "GOT IT" : "too quiet"); }
    canvas.setTextSize(1);
    if (cs == aud::REC_COUNT || cs == aud::REC_CAPTURE) { canvas.setCursor(76, 134); canvas.print("feel the beat: speaker off while recording"); }
  }
}
static void drawMix() {
  static const char *nm[5] = {"DRUM", "LEAD", "BASS", "AUDIO", "MASTER"};
  for (int i = 0; i < 5; i++) {
    int x = i * 50 + 4;
    uint16_t col = i < 4 ? SCR_COL[i] : rgb565(230, 230, 240);
    bool m = i < 4 && aud::mute[i];
    canvas.fillRoundRect(x, 26, 44, 18, 5, m ? rgb565(60, 20, 30) : col);
    canvas.setTextColor(m ? rgb565(255, 120, 120) : rgb565(10, 10, 20));
    canvas.setCursor(x + 22 - (int)strlen(nm[i]) * 3, 31); canvas.print(m ? "MUTE" : nm[i]);
    if (i < 4 && s_holdId == 110 + i) canvas.fillRect(x, 42, (int)(44 * holdFrac(110 + i)), 2, rgb565(255, 90, 90));
    float v = i < 4 ? aud::vol[i] : aud::master;
    canvas.fillRoundRect(x + 18, 50, 8, 152, 3, rgb565(30, 28, 46));
    int fy = 200 - (int)(v * 150.f);
    canvas.fillRoundRect(x + 6, fy - 6, 32, 12, 4, col);
    if (i < 4) {
      int mh = (int)(aud::meter[i] * 150.f * (m ? 0 : 1));
      canvas.fillRect(x + 30, 200 - mh, 5, mh, hsv565(120.f - aud::meter[i] * 120.f, 0.8f, 0.9f));
    }
  }
  char sb[10]; snprintf(sb, 10, "slot %d", s_slot);
  chip(R_SLOT, sb, rgb565(200, 200, 230), false);
  chip(R_SAVE, "hold SAVE", rgb565(120, 255, 160), false, holdFrac(101));
  chip(R_LOAD, "hold LOAD", rgb565(120, 200, 255), false, holdFrac(102));
  chip(R_BM, "-", rgb565(200, 200, 230), false); chip(R_BP, "+", rgb565(200, 200, 230), false);
  canvas.setTextColor(rgb565(200, 200, 220)); canvas.setCursor(262, 152); canvas.printf("%d bpm", (int)(aud::bpm() + 0.5f));
  static const char *SM[] = {"sub: off", "sub: kick", "sub: full"};
  chip(R_SUB, SM[sub::mode], rgb565(140, 255, 60), sub::mode != 0);
  chip(R_TUNE, "hold TUNE", rgb565(0, 170, 170), false, holdFrac(103));
  float hz = sub::nowHz();                                  // what the chassis is playing right now
  canvas.setCursor(258, 210);
  if (hz > 0) {
    int n = (int)lroundf(12.f * log2f(hz / 440.f)) + 69;
    canvas.setTextColor(rgb565(140, 255, 60)); canvas.printf("%s%d %3.0fHz", KEY_N[(n % 12 + 12) % 12], n / 12 - 1, hz);
  } else { canvas.setTextColor(rgb565(90, 90, 110)); canvas.printf(sub::tuned() ? "sub tuned" : "sub ~model"); }
}
static void drawTempo() {
  canvas.fillRoundRect(70, 80, 180, 70, 12, rgb565(18, 10, 34));
  canvas.drawRoundRect(70, 80, 180, 70, 12, SCR_COL[s_scr]);
  canvas.setTextSize(3); canvas.setTextColor(rgb565(255, 255, 255));
  canvas.setCursor(104, 90); canvas.printf("%3d", (int)(aud::bpm() + 0.5f));
  canvas.setTextSize(1); canvas.setCursor(166, 100); canvas.print("BPM");
  canvas.setCursor(84, 132); canvas.print("tap anywhere  -  B done");
}

static void splash() {
  M5Canvas &c = *s_fb[0];
  c.fillSprite(rgb565(6, 2, 14));
  int ox = (W - MANTIS_W) / 2, oy = 16;
  for (int y = 0; y < MANTIS_H; y++) for (int x = 0; x < MANTIS_W; x++) { uint16_t col = mantis_splash[y * MANTIS_W + x]; if (col) c.drawPixel(ox + x, oy + y, col); }
  c.setTextSize(2); c.setTextColor(rgb565(120, 255, 170));
  c.setCursor((W - 13 * 12) / 2, oy + MANTIS_H + 10); c.print("MANTIS STUDIO");
  c.pushSprite(&M5.Display, 0, 0);
  delay(700);
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true; cfg.internal_mic = true; cfg.internal_spk = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  for (int i = 0; i < 2; i++) { s_fb[i]->setColorDepth(16); s_fb[i]->setPsram(true); }
  s_fb[0]->createSprite(W, H);
  s_double = s_fb[1]->createSprite(W, H) != nullptr;
  g_cv = s_fb[0];
  splash();
  fx::begin();
  mantisBegin();
  sub::begin();
  aud::begin();
  if (s_double) {
    s_dispIdle = xSemaphoreCreateBinary(); xSemaphoreGive(s_dispIdle);
    xTaskCreatePinnedToCore(displayTask, "lcd", 4096, nullptr, 2, &s_dispTask, 0);
  }
  g_cv = s_fb[s_cur];
}

void loop() {
  static uint32_t last = micros();
  uint32_t now = micros();
  g_dt = clampf((now - last) / 1e6f, 0.004f, 0.06f); last = now;
  pollInput();
  aud::service(g_dt);
  sampleImu();
  aud::leadTone = clampf(0.5f + g_lookX * 0.5f - g_lookY * 0.2f, 0.f, 1.f);
  g_level = aud::level; g_peak = aud::peak;
  g_t += g_dt; g_hue = fmodf(g_hue + g_dt * 4.f, 360.f);

  if (s_scr == S_MANTIS) mantisDraw(false);
  else {
    canvas.fillSprite(rgb565(10, 8, 20));
    switch (s_scr) {
      case S_DRUM: drawDrum(); break;
      case S_LEAD: case S_BASS: drawKeys(); break;
      case S_AUDIO: drawAudio(); break;
      default: drawMix(); break;
    }
  }
  if (s_tempo) drawTempo();
  if (millis() < s_toastUntil) {
    int tw = (int)strlen(s_toast) * 6;
    canvas.fillRoundRect(W / 2 - tw / 2 - 8, 104, tw + 16, 20, 6, rgb565(10, 10, 20));
    canvas.setTextColor(rgb565(255, 255, 200)); canvas.setCursor(W / 2 - tw / 2, 110); canvas.print(s_toast);
  }
  drawHeader();
  drawFooter();
  g_shakeKick = false;
  present();
}
