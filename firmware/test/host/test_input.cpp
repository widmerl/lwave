// Tests: key processing (velocity, thresholds, depth freeze), pots, switches,
// battery, boot detection, calibration routine.
#include <initializer_list>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lwave/calibrator.h"
#include "lwave/hw_map.h"
#include "lwave/input.h"
#include "test.h"

using namespace lw;

namespace {
// Presses key 0 linearly from 0 to `peak` over pressMs, holds, then releases
// linearly over releaseMs; sampled every 0.5 ms. Collects events.
struct PressResult { int ons = 0, offs = 0; float velocity = -1, frozen = -1; };
PressResult press(float pressMs, float peak, float holdMs, float releaseMs, const Settings& s) {
  KeyProcessor kp;
  kp.reset();
  PressResult r;
  float total = pressMs + holdMs + releaseMs + 20.0f;
  for (float t = 0; t < total; t += 0.5f) {
    float x;
    if (t < pressMs) x = peak * t / pressMs;
    else if (t < pressMs + holdMs) x = peak;
    else if (t < pressMs + holdMs + releaseMs) x = peak * (1.0f - (t - pressMs - holdMs) / releaseMs);
    else x = 0.0f;
    KeyEvent ev;
    if (kp.update(0, x, (uint32_t)(t * 1000.0f), s, ev)) {
      if (ev.on) { ++r.ons; r.velocity = ev.velocity; }
      else { ++r.offs; r.frozen = ev.depth; }
    }
  }
  return r;
}
}  // namespace

TEST(keys_velocity_faster_is_louder) {
  Settings s;
  PressResult fast = press(2.0f, 1.0f, 50, 10, s);
  PressResult slow = press(60.0f, 1.0f, 50, 10, s);
  CHECK(fast.ons == 1 && slow.ons == 1);
  CHECK(fast.offs == 1 && slow.offs == 1);
  CHECK(fast.velocity > slow.velocity);
  CHECK(fast.velocity > 0.6f);
}

TEST(keys_half_press_plays_nothing) {
  Settings s;  // note-on at 30 %
  PressResult r = press(10.0f, 0.25f, 50, 10, s);
  CHECK(r.ons == 0 && r.offs == 0);
}

TEST(keys_release_freezes_earlier_depth) {
  Settings s;  // freeze 20 ms before the note-off
  PressResult r = press(5.0f, 1.0f, 100, 60, s);
  CHECK(r.offs == 1);
  // Note-off happens at 10 % travel; 20 ms earlier the key was still ~43 % down.
  CHECK(r.frozen > 0.3f && r.frozen < 0.6f);
}

TEST(keys_velocity_mapping_ends) {
  Settings s;
  CHECK_NEAR(KeyProcessor::velocityFromMs(0.5f, s), 1.0, 1e-6);
  CHECK_NEAR(KeyProcessor::velocityFromMs(100.0f, s), 0.0, 1e-6);
  float mid = KeyProcessor::velocityFromMs(6.32f, s);  // geometric middle of 1..40 ms
  CHECK_NEAR(mid, 0.5, 0.02);
}

TEST(keys_reversed_polarity_calibration) {
  CalibrationData cal;
  cal.setDefaults();
  cal.keyRest[3] = 3000;
  cal.keyBottom[3] = 1000;  // value falls when pressed
  CHECK_NEAR(cal.keyTravel(3, 3000), 0.0, 1e-6);
  CHECK_NEAR(cal.keyTravel(3, 2000), 0.5, 1e-6);
  CHECK_NEAR(cal.keyTravel(3, 500), 1.0, 1e-6);
}

TEST(pot_filter_deadband_and_ends) {
  Settings s;
  PotFilter f;
  f.update(0.5f, 0.0005f, s);
  for (int i = 0; i < 2000; ++i) f.update(0.5f + ((i & 1) ? 0.001f : -0.001f), 0.0005f, s);  // noise below dead band
  CHECK_NEAR(f.value(), 0.5, 0.0021);
  for (int i = 0; i < 4000; ++i) f.update(1.0f, 0.0005f, s);
  CHECK_NEAR(f.value(), 1.0, 1e-6);
}

TEST(switch_debounce) {
  Settings s;  // 20 ms
  CalibrationData cal;
  cal.setDefaults();
  SwitchDecoder d;
  uint16_t pos3 = cal.switchLevel[0][2], pos4 = cal.switchLevel[0][3];
  CHECK(d.update(0, pos3, 0, cal, s) == 2);
  CHECK(d.update(0, pos4, 1000, cal, s) == 2);     // not yet stable
  CHECK(d.update(0, pos4, 15000, cal, s) == 2);
  CHECK(d.update(0, pos4, 22000, cal, s) == 3);    // stable for 21 ms
  CHECK(d.update(0, pos3 + 40, 23000, cal, s) == 3);
}

TEST(battery_no_warning_while_the_divider_settles) {
  Settings s;  // divider 2.0, low 3.4 V
  auto raw = [](float v) { return (uint16_t)(v / 2.0f / 3.3f * 4095.0f); };
  // A full battery whose divider capacitor charges over the first 0.3 s.
  BatteryMonitor good;
  bool everLow = false;
  for (float t = 0; t < 6.0f; t += 0.0005f) {
    float v = 3.9f * (1.0f - expf(-t / 0.06f));
    good.update(raw(v), 0.0005f, s);
    everLow |= good.low() || good.critical();
  }
  CHECK(!everLow);
  CHECK_NEAR(good.volts(), 3.9, 0.02);
  // A really low battery: warned only after 1 s, and right then.
  BatteryMonitor weak;
  float firstLow = -1;
  for (float t = 0; t < 6.0f; t += 0.0005f) {
    weak.update(raw(3.1f), 0.0005f, s);
    if (firstLow < 0 && weak.critical()) firstLow = t;
  }
  CHECK(firstLow >= 0.99f && firstLow < 1.1f);
}

TEST(battery_low_and_hysteresis) {
  Settings s;  // divider 2.0, low 3.4 V
  BatteryMonitor b;
  auto raw = [](float v) { return (uint16_t)(v / 2.0f / 3.3f * 4095.0f); };
  b.update(raw(3.9f), 0.01f, s);
  CHECK(!b.low());
  for (int i = 0; i < 2000; ++i) b.update(raw(3.3f), 0.01f, s);
  CHECK(b.low() && !b.critical());
  for (int i = 0; i < 2000; ++i) b.update(raw(3.42f), 0.01f, s);
  CHECK(b.low());  // within hysteresis
  for (int i = 0; i < 2000; ++i) b.update(raw(3.6f), 0.01f, s);
  CHECK(!b.low());
}

TEST(boot_detects_held_key_without_calibration) {
  uint16_t raw[kNumKeys];
  for (int k = 0; k < kNumKeys; ++k) raw[k] = (uint16_t)(2000 + (k % 7) * 20);  // rest spread
  int c4 = keyIndexByName(kCalibrationKey);
  raw[c4] = 3400;
  CalibrationData cal;
  cal.setDefaults();
  bool held[kNumKeys];
  detectHeldKeys(raw, cal, held);
  int count = 0;
  for (bool h : held) count += h;
  CHECK(held[c4] && count == 1);
}

namespace {
ScanFrame restFrame(uint32_t t) {
  ScanFrame f{};
  for (int k = 0; k < kNumKeys; ++k) { f.keys[k] = (uint16_t)(1900 + k); f.keyTimeUs[k] = t; }
  for (int p = 0; p < kNumPots; ++p) f.pots[p] = 2000;
  for (int s = 0; s < kNumSwitches; ++s) f.switches[s] = 1800;
  f.battery = 2400;
  f.timeUs = t;
  return f;
}
}  // namespace

TEST(calibration_full_sweep) {
  Calibrator c;
  CalibrationData prev;
  prev.setDefaults();
  uint32_t t = 0;
  c.start(prev, t);
  const uint32_t dt = 500;  // 2 kHz scans
  for (int i = 0; i < 6000; ++i, t += dt) c.update(restFrame(t));  // release + rest
  CHECK(c.phase() == Calibrator::Phase::Sweep);
  // Press every key (value falls for odd keys: reversed magnet).
  for (int k = 0; k < kNumKeys; ++k) {
    for (int i = 0; i < 40; ++i, t += dt) {
      ScanFrame f = restFrame(t);
      f.keys[k] = (uint16_t)(k & 1 ? 1900 + k - 1500 : 1900 + k + 1500);
      c.update(f);
    }
    for (int i = 0; i < 10; ++i, t += dt) c.update(restFrame(t));
  }
  CHECK(c.keysDone() == kNumKeys);
  // Sweep every pot to both ends.
  for (int p = 0; p < kNumPots; ++p)
    for (int v : {10, 4085})
      for (int i = 0; i < 5; ++i, t += dt) {
        ScanFrame f = restFrame(t);
        for (int q = 0; q < p; ++q) f.pots[q] = 4085;
        f.pots[p] = (uint16_t)v;
        c.update(f);
      }
  CHECK(c.potsDone() == kNumPots);
  // Turn every switch through its 8 positions (75 ms at each).
  for (int s = 0; s < kNumSwitches; ++s)
    for (int pos = 0; pos < 8; ++pos)
      for (int i = 0; i < 150; ++i, t += dt) {
        ScanFrame f = restFrame(t);
        for (int q = 0; q < kNumPots; ++q) f.pots[q] = 4085;
        f.switches[s] = (uint16_t)(4095 - pos * 585);
        c.update(f);
      }
  CHECK(c.phase() == Calibrator::Phase::Done);
  const CalibrationData& r = c.result();
  CHECK(r.complete());
  CHECK(abs((int)r.keyRest[0] - 1900) <= 1 && r.keyBottom[0] == 3400);
  CHECK(r.keyBottom[1] == 1901 - 1500);
  CHECK(r.switchLevel[2][0] > r.switchLevel[2][7]);  // position 1 is the high end
}

TEST(calibration_finish_early_by_holding_c4) {
  Calibrator c;
  CalibrationData prev;
  prev.setDefaults();
  uint32_t t = 0;
  c.start(prev, t);
  for (int i = 0; i < 6000; ++i, t += 500) c.update(restFrame(t));
  int c4 = keyIndexByName(kCalibrationKey);
  for (int i = 0; i < 6200; ++i, t += 500) {  // 3.1 s
    ScanFrame f = restFrame(t);
    f.keys[c4] = 3500;
    c.update(f);
  }
  CHECK(c.phase() == Calibrator::Phase::Done);
  CHECK(c.result().keyValid[0] == 0);  // nothing else was calibrated
}

TEST(keys_fast_release_keeps_full_depth) {
  // Lifting the key in 15 ms must not dip the level: depth stays at the held value.
  Settings s;
  KeyProcessor kp;
  kp.reset();
  KeyEvent ev;
  float minDepthWhileOn = 1.0f, frozen = -1.0f;
  for (float t = 0; t < 200; t += 0.5f) {
    float x = t < 5 ? t / 5 : (t < 100 ? 1.0f : (t < 115 ? 1.0f - (t - 100) / 15 : 0.0f));
    if (kp.update(0, x, (uint32_t)(t * 1000), s, ev) && !ev.on) frozen = ev.depth;
    if (kp.on(0) && t > 20) minDepthWhileOn = fminf(minDepthWhileOn, kp.depth(0));
  }
  CHECK(minDepthWhileOn > 0.99f);
  CHECK(frozen > 0.99f);
}

TEST(calibration_switch_positions_tolerate_noise_and_skip_in_between) {
  Calibrator c;
  CalibrationData prev;
  prev.setDefaults();
  uint32_t t = 0;
  const uint32_t dt = 500;
  c.start(prev, t);
  for (int i = 0; i < 6000; ++i, t += dt) c.update(restFrame(t));
  CHECK(c.phase() == Calibrator::Phase::Sweep);
  Cue cues[16];
  while (c.drainCues(cues, 16) > 0) {}
  uint32_t seed = 7;
  auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return (int)(seed >> 27) - 16; };  // -16..15
  auto hold = [&](int level, int frames) {
    for (int i = 0; i < frames; ++i, t += dt) {
      ScanFrame f = restFrame(t);
      int v = level + noise();
      f.switches[0] = (uint16_t)(v < 0 ? 0 : (v > 4095 ? 4095 : v));
      c.update(f);
    }
  };
  // The rest frames already held every switch at position 5 (1800 counts).
  CHECK(c.switchLevelsSeen(0) == 1 && c.switchPositionSeen(0, 4));
  // A reading halfway between positions 3 and 4, held for 100 ms: ignored.
  hold(4095 - (int)(2.5 * 585), 200);
  CHECK(c.switchLevelsSeen(0) == 1);
  // Positions 1..8 with noise, 75 ms each; every new position ticks.
  int ticks = 0;
  for (int pos = 0; pos < 8; ++pos) {
    hold(4095 - pos * 585, 150);
    ticks += c.drainCues(cues, 16);
    CHECK(c.switchPositionSeen(0, pos));
  }
  CHECK(c.switchDone(0));
  CHECK(ticks >= 7 + 2);  // 7 new positions plus the two-note chime
  // Going back to a known position adds nothing.
  hold(4095 - 3 * 585, 150);
  CHECK(c.switchLevelsSeen(0) == 8);
  char buf[400];
  c.describeMissing(buf, sizeof(buf));
  CHECK(strstr(buf, "lfo_dest") == nullptr);  // switch 0 is complete
  CHECK(strstr(buf, "octave missing 1 2 3 4 6 7 8") != nullptr);  // position 5 came from the rest frames
}
