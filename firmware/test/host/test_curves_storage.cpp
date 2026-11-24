// Tests: knob curves, control mapping, settings, storage format, handoff.
#include <string.h>
#include "lwave/control_mapper.h"
#include "lwave/curves.h"
#include "lwave/handoff.h"
#include "lwave/hw_map.h"
#include "lwave/settings.h"
#include "lwave/storage_format.h"
#include "test.h"

using namespace lw;

TEST(curve_log3_hits_printed_points) {
  CHECK_NEAR(curveLog3(0.0f, 0.05f, 1.0f, 20.0f), 0.05, 1e-6);
  CHECK_NEAR(curveLog3(0.5f, 0.05f, 1.0f, 20.0f), 1.0, 1e-5);
  CHECK_NEAR(curveLog3(1.0f, 0.05f, 1.0f, 20.0f), 20.0, 1e-4);
  CHECK_NEAR(curveLog3(0.5f, 20.0f, 640.0f, 20000.0f), 640.0, 1e-2);
}

TEST(curve_log_to_zero_envelope_and_detune) {
  CHECK_NEAR(timeKnobCurve(0.0f), 0.01, 1e-6);
  CHECK_NEAR(timeKnobCurve(0.5f), 0.3162, 1e-3);   // geometric middle
  CHECK_NEAR(timeKnobCurve(1.0f), 10.0, 1e-3);
  CHECK_NEAR(timeKnobCurve(1.0f / 3.0f), 0.1, 1e-4);  // 0.1 s tick
  CHECK_NEAR(timeKnobCurve(2.0f / 3.0f), 1.0, 1e-3);  // 1 s tick
  CHECK_NEAR(detuneCurve(0.0f), 0.0, 1e-9);
  CHECK_NEAR(detuneCurve(0.7f), 1.0 / 3.0, 1e-4);
  CHECK_NEAR(detuneCurve(1.0f), 1.0, 1e-4);
}

TEST(curve_level_sliders) {
  CHECK_NEAR(curveDbTo0(0.0f, -24.0f, 12.0f, 0.05f), 0.0, 1e-9);
  CHECK_NEAR(gainToDb(curveDbTo0(0.5f, -24.0f, 12.0f, 0.05f)), -6.0, 1e-3);
  CHECK_NEAR(gainToDb(curveDbTo0(1.0f, -24.0f, 12.0f, 0.05f)), 12.0, 1e-3);
  // Monotonic over the whole travel.
  float prev = -1.0f;
  for (int i = 0; i <= 100; ++i) {
    float g = curveDbTo0(i / 100.0f, -24.0f, 12.0f, 0.05f);
    CHECK(g >= prev);
    prev = g;
  }
}

TEST(mapper_switches_and_ranges) {
  PanelState panel;
  for (float& p : panel.pot) p = 0.5f;
  panel.sw[static_cast<int>(Switch::FilterType)] = 6;  // position 7 = LP24
  panel.sw[static_cast<int>(Switch::LfoWave)] = 0;     // Rise linear
  panel.sw[static_cast<int>(Switch::LfoDest)] = 5;     // Volume & Pitch
  EngineParams p;
  mapControls(panel, Settings(), 20.0f, p);
  CHECK(p.filterType == FilterType::LP24);
  CHECK(p.lfoWave == LfoWave::RiseLinear);
  CHECK(p.lfoDest == LfoDest::VolumePitch);
  CHECK_NEAR(p.tuneCents, 0.0, 1e-4);
  CHECK_NEAR(p.delayFeedback, 0.45, 1e-5);
  CHECK_NEAR(p.lfoDelay, 0.3162, 1e-3);
  CHECK_NEAR(p.lfoPeriod, 0.3162, 1e-3);
  CHECK_NEAR(p.delayTime, 0.3162, 1e-3);
  panel.pot[static_cast<int>(Pot::DelayTime)] = 1.0f;
  mapControls(panel, Settings(), 20.0f, p);
  CHECK_NEAR(p.delayTime, 10.0, 1e-3);  // the knob ends at 10 s even with a longer buffer
  // Without PSRAM the delay knob ends at the buffer length.
  panel.pot[static_cast<int>(Pot::DelayTime)] = 1.0f;
  mapControls(panel, Settings(), 2.5f, p);
  CHECK_NEAR(p.delayTime, 2.5, 1e-3);
}

TEST(octave_switch_shift) {
  CHECK(octaveShiftSemitones(3) == 0);    // panel position 4
  CHECK(octaveShiftSemitones(0) == -36);  // position 1
  CHECK(octaveShiftSemitones(7) == 48);   // position 8
}

TEST(hw_map_tables_consistent) {
  // Every mux channel is used at most once.
  int used[kNumMuxes][16] = {};
  for (const KeyInfo& k : kKeys) used[k.mux][k.channel]++;
  for (const AnalogSource& s : kPotSources) used[s.mux][s.channel]++;
  for (const AnalogSource& s : kSwitchSources)
    if (s.mux != kDirect) used[s.mux][s.channel]++;
  int total = 0;
  for (auto& m : used)
    for (int c : m) { CHECK(c <= 1); total += c; }
  CHECK(total == kNumMuxes * 16);  // all 80 channels are wired
  CHECK(kKeys[keyIndexByName("D5")].note == 60);
  CHECK(kKeys[keyIndexByName("F4")].note == 50);
}

TEST(settings_set_and_clamp) {
  Settings s;
  CHECK(setSetting(s, "key_note_on", 0.4f));
  CHECK_NEAR(s.keyNoteOn, 0.4, 1e-6);
  CHECK(setSetting(s, "voices", 99.0f));
  CHECK_NEAR(s.voices, (double)kMaxVoices, 1e-6);
  CHECK(!setSetting(s, "no_such_thing", 1.0f));
}

TEST(storage_roundtrip_and_corruption) {
  CalibrationData cal;
  cal.setDefaults();
  cal.keyRest[5] = 1234;
  cal.keyValid[5] = 1;
  Settings s;
  s.velRangeDb = 18.0f;
  PersistentBlob blob;
  packBlob(cal, s, blob);
  CalibrationData cal2;
  Settings s2;
  CHECK(unpackBlob(blob, cal2, s2));
  CHECK(cal2.keyRest[5] == 1234 && cal2.keyValid[5] == 1);
  CHECK_NEAR(s2.velRangeDb, 18.0, 1e-6);
  reinterpret_cast<uint8_t*>(&blob)[40] ^= 0x55;
  CHECK(!unpackBlob(blob, cal2, s2));
  CHECK(sizeof(PersistentBlob) < 4000);  // fits the Teensy 4.1 EEPROM (4284 bytes)
}

TEST(handoff_triple_buffer_and_queue) {
  TripleBuffer<int> tb;
  tb.writeBuffer() = 1;
  tb.publish();
  tb.writeBuffer() = 2;
  tb.publish();
  CHECK(tb.read() == 2);
  CHECK(tb.read() == 2);
  SpscQueue<int, 4> q;
  CHECK(q.push(1) && q.push(2) && q.push(3));
  CHECK(!q.push(4));  // capacity N - 1
  int v = 0;
  CHECK(q.pop(v) && v == 1);
}

TEST(detune_label_shows_chorus_with_one_copy) {
  EngineParams e;
  e.detuneAmount = 1.0f / 3.0f;  // knob at 70 %
  e.detuneKnob = 0.7f;
  char buf[32];
  e.detuneCopies = 3;
  formatPotValue(e, Pot::Detune, buf, sizeof(buf));
  CHECK(strcmp(buf, "-7.7 / +12.5 c") == 0);
  e.detuneCopies = 2;
  formatPotValue(e, Pot::Detune, buf, sizeof(buf));
  CHECK(strcmp(buf, "+25.0 c") == 0);
  e.detuneCopies = 1;
  formatPotValue(e, Pot::Detune, buf, sizeof(buf));
  CHECK(strcmp(buf, "chorus 70 %") == 0);
}

TEST(setting_names_are_unique) {
  for (int i = 0; i < kNumSettings; ++i)
    for (int j = i + 1; j < kNumSettings; ++j) CHECK(strcmp(kSettingInfos[i].name, kSettingInfos[j].name) != 0);
}
