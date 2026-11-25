// L-wave browser simulator — the firmware's Controller and Engine compiled to
// WebAssembly, driven by simulated scan frames.
//
// The page sets pot positions, switch positions and key presses; every 128
// samples this file builds about six ScanFrames (≈2 kHz, like the Teensy),
// animates key travel so velocity timing and key depth behave as on the
// hardware, runs Controller::onFrame() and renders Engine::process().
// The calibration is "ideal": keys rest at 1000 and bottom at 3000 counts,
// pots run 0..4095, switches sit at their nominal ladder voltages.
//
// Build: tools/sim/build.sh (clang --target=wasm32-wasi).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lwave/control_mapper.h"
#include "lwave/controller.h"
#include "lwave/curves.h"
#include "lwave/engine.h"
#include "lwave/hw_map.h"
#include "lwave/settings.h"

#define SIM_EXPORT(name) extern "C" __attribute__((export_name(#name)))

using namespace lw;

namespace {

// Like the DRV5056 keys on the first build: about 985 counts up, 2140 fully down.
constexpr uint16_t kKeyRest = 985, kKeyBottom = 2140;
constexpr uint32_t kDelaySamples = 1000000;  // 20.8 s at 48 kHz, as with PSRAM
constexpr int kOutCap = 65536;
constexpr int kMaxRender = 4096;

/// Console output collected for the page.
class SimOut : public TextOut {
 public:
  void write(const char* s, size_t n) override {
    if (len_ + (int)n > kOutCap) n = (size_t)(kOutCap - len_);  // page reads often; drop on overflow
    memcpy(buf_ + len_, s, n);
    len_ += (int)n;
  }
  const char* data() const { return buf_; }
  int size() const { return len_; }
  void clear() { len_ = 0; }
 private:
  char buf_[kOutCap];
  int len_ = 0;
};

/// Platform for the simulator: time from the audio clock, LEDs and storage in memory.
class SimPlatform : public Platform {
 public:
  uint32_t now = 0;
  float cpu = -1.0f;
  bool leds[kNumLeds] = {};
  PersistentBlob blob{};
  SimOut text;
  uint32_t micros() override { return now; }
  void setLed(int i, bool on) override { if (i >= 0 && i < kNumLeds) leds[i] = on; }
  bool loadBlob(PersistentBlob& b) override { b = blob; return true; }
  bool saveBlob(const PersistentBlob& b) override { blob = b; return true; }
  void reboot() override { text.print("(reboot ignored in the simulator)\r\n"); }
  float audioCpuPercent() override { return cpu; }
  const char* describe() override { return "browser simulator (WebAssembly)"; }
  TextOut& out() override { return text; }
};

struct KeyAnim {
  float travel = 0.0f;  // current 0..1
  float target = 0.0f;  // where it is heading
  float ratePerUs = 0;  // travel per microsecond
};

SimPlatform gPf;
Engine gEngine;
Controller gCtl;
int16_t gDelay[kDelaySamples];
ScanFrame gFrame;
KeyAnim gKeys[kNumKeys];
float gPot[kNumPots];
int gSwitch[kNumSwitches];
float gBatteryV = 3.9f;
bool gLineOut = false;
float gSr = 48000.0f;
double gTimeUs = 0.0;
float gOut[kMaxRender];
char gLine[128];
bool gReady = false;
float gLabelMaxDelay = 20.0f;
Settings gLabelSettings;  // settings for the labels when this instance is not running the synth
char gLabel[48];

CalibrationData idealCalibration() {
  CalibrationData c;
  c.setDefaults();
  for (int k = 0; k < kNumKeys; ++k) { c.keyRest[k] = kKeyRest; c.keyBottom[k] = kKeyBottom; c.keyValid[k] = 1; }
  for (int p = 0; p < kNumPots; ++p) { c.potMin[p] = 0; c.potMax[p] = kAdcMax; c.potValid[p] = 1; }
  for (int s = 0; s < kNumSwitches; ++s) c.switchValid[s] = 1;
  return c;
}

uint16_t switchRaw(int pos) { return (uint16_t)((kAdcMax * (kSwitchPositions - 1 - pos)) / (kSwitchPositions - 1)); }

void buildFrame(uint32_t t) {
  for (int k = 0; k < kNumKeys; ++k) {
    gFrame.keys[k] = (uint16_t)(kKeyRest + (kKeyBottom - kKeyRest) * gKeys[k].travel + 0.5f);
    gFrame.keyTimeUs[k] = t;
  }
  // The dials are wired so that their reading falls as they turn up (kPotReversed).
  for (int p = 0; p < kNumPots; ++p)
    gFrame.pots[p] = (uint16_t)((kPotReversed[p] ? 1.0f - gPot[p] : gPot[p]) * kAdcMax + 0.5f);
  for (int s = 0; s < kNumSwitches; ++s) gFrame.switches[s] = switchRaw(gSwitch[s]);
  float pinV = gBatteryV / gCtl.settings().battDivider;
  float raw = pinV / 3.3f * kAdcMax;
  gFrame.battery = (uint16_t)(raw < 0 ? 0 : (raw > kAdcMax ? kAdcMax : raw));
  gFrame.lineOut = gLineOut;
  gFrame.timeUs = t;
}

void advanceKeys(float dtUs) {
  for (KeyAnim& k : gKeys) {
    if (k.travel == k.target) continue;
    float step = k.ratePerUs * dtUs;
    if (k.travel < k.target) k.travel = fminf(k.target, k.travel + step);
    else k.travel = fmaxf(k.target, k.travel - step);
  }
}

}  // namespace

/// Starts the simulated synth at the page's audio sample rate. Panel defaults: see setDefaultPanel in the page.
SIM_EXPORT(sim_init) void sim_init(float sampleRate) {
  gSr = sampleRate;
  gTimeUs = 0.0;
  for (KeyAnim& k : gKeys) k = KeyAnim();
  packBlob(idealCalibration(), Settings(), gPf.blob);
  gEngine.init(sampleRate, gDelay, kDelaySamples);
  gCtl.begin(gPf, gEngine);
  buildFrame(0);
  gCtl.boot(gFrame);
  gReady = true;
}

/// Sets pot i (Pot order) to position 0..1.
SIM_EXPORT(sim_set_pot) void sim_set_pot(int i, float pos) {
  if (i >= 0 && i < kNumPots) gPot[i] = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
}

/// Sets switch i (Switch order) to position 0..7 (panel 1..8).
SIM_EXPORT(sim_set_switch) void sim_set_switch(int i, int pos) {
  if (i >= 0 && i < kNumSwitches) gSwitch[i] = pos & 7;
}

/// Presses (down = 1) or releases key k. A press reaches the bottom in fullMs;
/// a release takes fullMs if it is given (> 0), otherwise 15 ms.
SIM_EXPORT(sim_key) void sim_key(int k, int down, float fullMs) {
  if (k < 0 || k >= kNumKeys) return;
  KeyAnim& a = gKeys[k];
  a.target = down ? 1.0f : 0.0f;
  float ms = down ? (fullMs < 0.2f ? 0.2f : fullMs) : (fullMs > 0.0f ? fullMs : 15.0f);
  a.ratePerUs = 1.0f / (ms * 1000.0f);
}

/// Moves key k to a held depth (0..1) over 20 ms, e.g. from MIDI aftertouch.
SIM_EXPORT(sim_key_depth) void sim_key_depth(int k, float depth) {
  if (k < 0 || k >= kNumKeys || gKeys[k].target <= 0.0f) return;
  float d = depth < 0.35f ? 0.35f : (depth > 1 ? 1 : depth);  // stays above note-off
  gKeys[k].target = d;
  gKeys[k].ratePerUs = 1.0f / 20000.0f;
}

/// Full-press time (ms) that produces velocity v (0..1) with the current settings.
SIM_EXPORT(sim_press_ms) float sim_press_ms(float v) {
  const Settings& s = gCtl.settings();
  v = v < 0 ? 0 : (v > 1 ? 1 : v);
  float ms = s.velFastMs * powf(s.velSlowMs / s.velFastMs, 1.0f - v);  // start → note-on time
  float span = s.keyNoteOn - s.keyVelStart;
  return ms / (span > 0.01f ? span : 0.01f);
}

/// Simulated battery voltage and line-out plug.
SIM_EXPORT(sim_set_battery) void sim_set_battery(float volts) { gBatteryV = volts; }
SIM_EXPORT(sim_set_lineout) void sim_set_lineout(int on) { gLineOut = on != 0; }
/// Audio load measured by the page, shown in the stats view.
SIM_EXPORT(sim_set_cpu) void sim_set_cpu(float pct) { gPf.cpu = pct; }

/// Renders n samples (n ≤ 4096); returns a pointer to them.
SIM_EXPORT(sim_render) float* sim_render(int n) {
  if (!gReady) return gOut;
  if (n > kMaxRender) n = kMaxRender;
  for (int done = 0; done < n; done += kBlockSize) {
    int chunk = n - done < kBlockSize ? n - done : kBlockSize;
    double blockUs = chunk * 1e6 / gSr;
    int frames = (int)(blockUs / 480.0 + 0.5);  // ~2 kHz scanning
    if (frames < 1) frames = 1;
    double dt = blockUs / frames;
    for (int f = 0; f < frames; ++f) {
      advanceKeys((float)dt);
      gTimeUs += dt;
      gPf.now = (uint32_t)(uint64_t)gTimeUs;
      buildFrame(gPf.now);
      gCtl.onFrame(gFrame);
    }
    gEngine.process(gOut + done, chunk);
  }
  return gOut;
}

/// Console input: write a line into sim_line_buffer(), then call sim_line().
SIM_EXPORT(sim_line_buffer) char* sim_line_buffer() { return gLine; }
SIM_EXPORT(sim_line) void sim_line() {
  gLine[sizeof(gLine) - 1] = 0;
  gCtl.onLine(gLine);
}

/// Console output collected since the last sim_out_clear().
SIM_EXPORT(sim_out_ptr) const char* sim_out_ptr() { return gPf.text.data(); }
SIM_EXPORT(sim_out_len) int sim_out_len() { return gPf.text.size(); }
SIM_EXPORT(sim_out_clear) void sim_out_clear() { gPf.text.clear(); }

/// State for the page.
SIM_EXPORT(sim_led) int sim_led(int i) { return (i >= 0 && i < kNumLeds && gPf.leds[i]) ? 1 : 0; }
SIM_EXPORT(sim_key_travel) float sim_key_travel(int k) { return (k >= 0 && k < kNumKeys) ? gKeys[k].travel : 0.0f; }
SIM_EXPORT(sim_note_on) int sim_note_on(int k) { return (k >= 0 && k < kNumKeys && gCtl.noteOn(k)) ? 1 : 0; }
SIM_EXPORT(sim_voices) int sim_voices() { return gEngine.activeVoices(); }
SIM_EXPORT(sim_level_db) float sim_level_db() { return gEngine.outputLevelDb(); }
SIM_EXPORT(sim_limit_db) float sim_limit_db() { return gEngine.limiterReductionDb(); }
SIM_EXPORT(sim_mode) int sim_mode() { return static_cast<int>(gCtl.mode()); }
SIM_EXPORT(sim_octave_shift) int sim_octave_shift() {
  return octaveShiftSemitones(gCtl.panel().sw[static_cast<int>(Switch::Octave)]);
}

/// Engine-unit value of pot `pot` at position `pos` (seconds, Hz, cents, dB, Q or 0..1),
/// using the same curves as the firmware. Works without sim_init() for the monitor.
SIM_EXPORT(map_value) float map_value(int pot, float pos) {
  if (pot < 0 || pot >= kNumPots) return 0.0f;
  static PanelState panel;
  static EngineParams e;
  for (float& v : panel.pot) v = 0.0f;
  panel.pot[pot] = pos;
  float maxDelay = gReady ? gEngine.maxDelaySeconds() : gLabelMaxDelay;
  mapControls(panel, gReady ? gCtl.settings() : gLabelSettings, maxDelay, e);
  return potEngineValue(e, static_cast<Pot>(pot));
}

/// The firmware's own text for pot `pot` at position `pos` (as the 'p' debug view
/// prints it, e.g. "+25.0 c" or "chorus 60 %"). Returns a NUL-terminated string.
SIM_EXPORT(format_pot) const char* format_pot(int pot, float pos) {
  gLabel[0] = 0;
  if (pot < 0 || pot >= kNumPots) return gLabel;
  static PanelState panel;
  static EngineParams e;
  for (float& v : panel.pot) v = 0.0f;
  panel.pot[pot] = pos;
  float maxDelay = gReady ? gEngine.maxDelaySeconds() : gLabelMaxDelay;
  mapControls(panel, gReady ? gCtl.settings() : gLabelSettings, maxDelay, e);
  formatPotValue(e, static_cast<Pot>(pot), gLabel, (int)sizeof(gLabel));
  return gLabel;
}

/// Applies a console line from sim_line_buffer() to the label settings when it is
/// "set <name> <value>" or "defaults" (so labels follow settings typed for the synth).
SIM_EXPORT(label_line) void label_line() {
  gLine[sizeof(gLine) - 1] = 0;
  if (strcmp(gLine, "defaults") == 0) { gLabelSettings = Settings(); return; }
  if (strncmp(gLine, "set ", 4) != 0) return;
  char name[40];
  const char* p = gLine + 4;
  int n = 0;
  while (*p && *p != ' ' && n < (int)sizeof(name) - 1) name[n++] = *p++;
  name[n] = 0;
  if (*p == ' ') setSetting(gLabelSettings, name, (float)atof(p + 1));
}

/// Longest delay of the connected synth (from its `info`), for map_value() labels in the monitor.
SIM_EXPORT(set_label_max_delay) void set_label_max_delay(float s) { gLabelMaxDelay = s > 0.06f ? s : 0.06f; }

/// Table access for the page: key net name, MIDI note (octave position 4).
SIM_EXPORT(key_note) int key_note(int k) { return (k >= 0 && k < kNumKeys) ? kKeys[k].note : 0; }
