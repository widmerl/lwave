// Tests: the controller end to end with a fake platform (boot modes, playing
// a key, console commands, LEDs, storage).
#include <string.h>
#include <string>
#include <vector>
#include "lwave/controller.h"
#include "lwave/hw_map.h"
#include "test.h"

using namespace lw;

namespace {
struct FakeOut : TextOut {
  std::string text;
  void write(const char* s, size_t n) override { text.append(s, n); }
};

struct FakePlatform : Platform {
  uint32_t now = 0;
  bool leds[kNumLeds] = {};
  bool haveBlob = false;
  PersistentBlob blob{};
  FakeOut o;
  uint32_t micros() override { return now; }
  void setLed(int i, bool on) override { leds[i] = on; }
  bool loadBlob(PersistentBlob& b) override { if (!haveBlob) return false; b = blob; return true; }
  bool saveBlob(const PersistentBlob& b) override { blob = b; haveBlob = true; return true; }
  void reboot() override {}
  float cpu = 12.5f;
  float audioCpuPercent() override { return cpu; }
  const char* describe() override { return "test rig"; }
  TextOut& out() override { return o; }
};

std::vector<int16_t> gDelay(44100);

// A calibrated panel where every key rests at 1000 and bottoms at 3000.
CalibrationData idealCal() {
  CalibrationData c;
  c.setDefaults();
  for (int k = 0; k < kNumKeys; ++k) { c.keyRest[k] = 1000; c.keyBottom[k] = 3000; c.keyValid[k] = 1; }
  for (int p = 0; p < kNumPots; ++p) c.potValid[p] = 1;
  for (int s = 0; s < kNumSwitches; ++s) c.switchValid[s] = 1;
  return c;
}

ScanFrame frame(uint32_t t, int pressedKey = -1, uint16_t pressedValue = 3000) {
  ScanFrame f{};
  for (int k = 0; k < kNumKeys; ++k) { f.keys[k] = 1000; f.keyTimeUs[k] = t; }
  if (pressedKey >= 0) f.keys[pressedKey] = pressedValue;
  for (int p = 0; p < kNumPots; ++p) f.pots[p] = 2048;
  f.pots[static_cast<int>(Pot::LevelSaw)] = 4075;
  f.pots[static_cast<int>(Pot::Volume)] = 4075;
  f.pots[static_cast<int>(Pot::Attack)] = 20;
  f.pots[static_cast<int>(Pot::Sustain)] = 4075;
  f.pots[static_cast<int>(Pot::Release)] = 20;  // shortest release (50 ms)
  for (int s = 0; s < kNumSwitches; ++s) f.switches[s] = 4095;  // position 1
  f.switches[static_cast<int>(Switch::Octave)] = (uint16_t)(4095 * 4 / 7);  // position 4
  f.switches[static_cast<int>(Switch::FilterType)] = 0;                      // position 8 = off
  f.battery = 2400;
  f.timeUs = t;
  return f;
}

struct World {
  FakePlatform pf;
  Engine engine;
  Controller ctl;
  explicit World(bool calibrated) {
    if (calibrated) { packBlob(idealCal(), Settings(), pf.blob); pf.haveBlob = true; }
    engine.init(44100.0f, gDelay.data(), (uint32_t)gDelay.size());
    ctl.begin(pf, engine);
  }
  // Runs frames at 2 kHz for `ms`, rendering audio alongside; returns output peak.
  float run(float ms, int key = -1, uint16_t value = 3000) {
    float peak = 0, buf[kBlockSize];
    int frames = (int)(ms * 2.0f);
    for (int i = 0; i < frames; ++i) {
      pf.now += 500;
      ctl.onFrame(frame(pf.now, key, value));
      if (i % 6 == 0) {  // ~128 samples per 3 ms
        engine.process(buf, kBlockSize);
        for (float v : buf) peak = fmaxf(peak, fabsf(v));
      }
    }
    return peak;
  }
};
}  // namespace

TEST(controller_boot_modes) {
  {
    World w(true);
    w.ctl.boot(frame(0));
    CHECK(w.ctl.mode() == Mode::Normal);
  }
  {
    World w(true);
    w.ctl.boot(frame(0, keyIndexByName("D5")));
    CHECK(w.ctl.mode() == Mode::Calibration);
  }
  {
    World w(true);
    w.ctl.boot(frame(0, keyIndexByName("F4")));
    CHECK(w.ctl.mode() == Mode::Debug);
  }
  {
    World w(false);  // uncalibrated: boot key found via the median
    ScanFrame f = frame(0);
    for (int k = 0; k < kNumKeys; ++k) f.keys[k] = 2048;
    f.keys[keyIndexByName("D5")] = 3600;
    w.ctl.boot(f);
    CHECK(w.ctl.mode() == Mode::Calibration);
  }
}

TEST(controller_plays_a_key) {
  World w(true);
  w.ctl.boot(frame(0));
  CHECK(w.run(30) < 1e-6f);
  int key = keyIndexByName("D5");
  float playing = w.run(200, key);
  CHECK(playing > 0.05f);
  CHECK(w.engine.activeVoices() == 1);
  CHECK(w.pf.leds[0]);  // meter shows output
  w.run(600);           // released
  CHECK(w.engine.activeVoices() == 0);
}

TEST(controller_console_set_save_and_get) {
  World w(true);
  w.ctl.boot(frame(0));
  w.ctl.onLine("set key_note_on 0.45");
  CHECK_NEAR(w.ctl.settings().keyNoteOn, 0.45, 1e-6);
  w.ctl.onLine("save");
  CalibrationData c;
  Settings s;
  CHECK(unpackBlob(w.pf.blob, c, s));
  CHECK_NEAR(s.keyNoteOn, 0.45, 1e-6);
  w.pf.o.text.clear();
  w.ctl.onLine("get key_note_on");
  CHECK(w.pf.o.text.find("0.45") != std::string::npos);
  w.ctl.onLine("bogus");
  CHECK(w.pf.o.text.find("unknown command") != std::string::npos);
}

TEST(controller_debug_stream_and_events) {
  World w(true);
  w.ctl.boot(frame(0, keyIndexByName("F4")));
  w.pf.o.text.clear();
  w.run(40);
  CHECK(w.pf.o.text.find("STATS") != std::string::npos);
  w.run(100);                               // F4 released
  w.run(100, keyIndexByName("A1"));         // play A1
  CHECK(w.pf.o.text.find("EV on  A1") != std::string::npos);
  w.ctl.onLine("r");
  w.pf.o.text.clear();
  w.run(40);
  CHECK(w.pf.o.text.find("RAW K:") != std::string::npos);
}

TEST(controller_low_battery_led) {
  World w(true);
  w.ctl.boot(frame(0));
  w.run(10);
  CHECK(!w.pf.leds[8]);
  w.ctl.onLine("set batt_low_v 4.1");  // 2400 counts ≈ 3.87 V at divider 2
  w.run(8000);
  CHECK(w.pf.leds[8]);
}

TEST(controller_monitor_line_format) {
  World w(true);
  w.ctl.boot(frame(0));
  w.ctl.onLine("m");
  w.pf.o.text.clear();
  w.run(100, keyIndexByName("D5"));
  size_t at = w.pf.o.text.rfind("MON ");
  CHECK(at != std::string::npos);
  std::string line = w.pf.o.text.substr(at, w.pf.o.text.find('\n', at) - at);
  auto field = [&](const char* name) {
    std::string key = std::string(" ") + name + "=";
    size_t p = line.find(key);
    if (p == std::string::npos) return std::string();
    p += key.size();
    return line.substr(p, line.find(' ', p) - p);
  };
  CHECK(field("k").size() == 96);
  CHECK(field("n").size() == 48);
  CHECK(field("p").size() == 90);
  CHECK(field("s").size() == 4);
  CHECK(field("rk").size() == 144);
  CHECK(field("rp").size() == 90);
  CHECK(field("rs").size() == 12);
  CHECK(field("rb").size() == 3);
  CHECK(field("led").size() == 9);
  int d5 = keyIndexByName("D5");
  CHECK(field("n")[d5] == '1');
  CHECK(field("k").substr(2 * d5, 2) == "ff");
  CHECK(field("s") == "1148");  // dest 1, wave 1, octave 4, filter 8
  CHECK(line.size() < 720);
}

TEST(controller_cpu_guard_caps_and_restores_polyphony) {
  World w(true);
  w.ctl.boot(frame(w.pf.now));
  for (int k = 10; k < 20; ++k) {  // ten held notes
    NoteEvent n; n.type = NoteEvent::Type::On; n.key = (uint8_t)k; n.note = 50.0f + k; n.velocity = 1.0f;
    w.engine.post(n);
  }
  w.run(50);
  CHECK(w.engine.activeVoices() == 10);
  CHECK(w.ctl.voiceCap() == kMaxVoices);
  w.pf.cpu = 95.0f;  // overloaded
  w.run(400);
  CHECK(w.ctl.voiceCap() == kMinVoiceCap);
  CHECK(w.engine.activeVoices() == kMinVoiceCap);
  w.pf.cpu = 40.0f;  // load fine again: one voice back every 0.5 s
  w.run(2100);
  CHECK(w.ctl.voiceCap() >= kMinVoiceCap + 3 && w.ctl.voiceCap() <= kMinVoiceCap + 5);
  w.run(12000);
  CHECK(w.ctl.voiceCap() == kMaxVoices);
}
