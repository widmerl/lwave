// Tests: sound engine behaviour (levels, envelopes, filters, LFO, stealing).
#include <math.h>
#include <vector>
#include "lwave/curves.h"
#include "lwave/engine.h"
#include "test.h"

using namespace lw;

namespace {
constexpr float kSr = 44100.0f;
std::vector<int16_t> gDelayMem(3 * 44100);

struct Rig {
  Engine e;
  Rig() { e.init(kSr, gDelayMem.data(), (uint32_t)gDelayMem.size()); }
  EngineParams& p() { return e.params().writeBuffer(); }
  void publish() { e.params().publish(); }
  void on(int key, float note, float vel = 1.0f, float depth = 1.0f) {
    NoteEvent n; n.type = NoteEvent::Type::On; n.key = (uint8_t)key; n.note = note; n.velocity = vel; n.depth = depth;
    e.post(n);
  }
  void off(int key, float depth = 1.0f) {
    NoteEvent n; n.type = NoteEvent::Type::Off; n.key = (uint8_t)key; n.depth = depth;
    e.post(n);
  }
  // Renders seconds of audio; returns peak and whether everything was finite.
  float run(float seconds, bool* finite = nullptr, std::vector<float>* keep = nullptr) {
    int n = (int)(seconds * kSr) / kBlockSize * kBlockSize;
    float buf[kBlockSize], peak = 0;
    bool ok = true;
    for (int i = 0; i < n; i += kBlockSize) {
      e.process(buf, kBlockSize);
      for (float v : buf) { if (!std::isfinite(v)) ok = false; peak = fmaxf(peak, fabsf(v)); if (keep) keep->push_back(v); }
    }
    if (finite) *finite = ok;
    return peak;
  }
};

void basePatch(EngineParams& p) {
  p = EngineParams();
  for (float& d : p.keyDepth) d = 1.0f;
  p.levelSaw = 0.0f;
  p.attack = 0.0f; p.decay = 0.1f; p.sustain = 1.0f; p.release = 0.05f;
  p.depthMode = 0;
}
}  // namespace

TEST(engine_silent_without_notes) {
  Rig r;
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.publish();
  CHECK(r.run(0.2f) < 1e-6f);
}

TEST(engine_gain_staging_one_slider_full) {
  // One note, one slider at +12 dB, master at +12 dB → just below full scale.
  Rig r;
  basePatch(r.p());
  r.p().levelSine = dbToGain(12.0f);
  r.p().volume = dbToGain(12.0f);
  r.publish();
  r.on(0, 60.0f);
  float peak = r.run(0.5f);
  CHECK(peak > 0.8f && peak < 0.95f);
}

TEST(engine_attack_time) {
  Rig r;
  basePatch(r.p());
  r.p().levelSine = 1.0f;
  r.p().attack = 0.1f;
  r.publish();
  r.on(0, 69.0f);
  r.run(0.05f);
  float half = r.e.voice(0).envLevel();
  r.run(0.06f);
  float full = r.e.voice(0).envLevel();
  CHECK(half > 0.3f && half < 0.8f);
  CHECK(full > 0.95f);
}

TEST(engine_release_to_idle) {
  Rig r;
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.p().release = 0.1f;
  r.publish();
  r.on(0, 60.0f);
  r.run(0.1f);
  CHECK(r.e.activeVoices() == 1);
  r.off(0);
  r.run(0.3f);
  CHECK(r.e.activeVoices() == 0);
}

TEST(engine_all_filters_and_lfo_shapes_finite_and_bounded) {
  for (int ft = 0; ft < 8; ++ft)
    for (int w = 0; w < 8; ++w) {
      Rig r;
      basePatch(r.p());
      EngineParams& p = r.p();
      p.levelSaw = p.levelSquare = p.levelSub1 = p.levelPink = dbToGain(12.0f);
      p.volume = dbToGain(12.0f);
      p.filterType = static_cast<FilterType>(ft);
      p.filterFreq = 800.0f; p.filterQ = 20.0f; p.filterEnv = 1.0f; p.filterTrack = 1.0f;
      p.lfoWave = static_cast<LfoWave>(w);
      p.lfoDest = static_cast<LfoDest>((ft + w) % 8);
      p.lfoStrength = 1.0f; p.lfoPeriod = 0.2f; p.lfoDelay = 0.05f;
      p.drive = 1.0f; p.crush = 0.5f; p.delayMix = 0.5f; p.delayFeedback = 0.9f; p.reverbMix = 0.5f; p.reverbTime = 20.0f;
      r.publish();
      for (int k = 0; k < 6; ++k) r.on(k, 40.0f + 7.0f * k);
      bool finite = false;
      float peak = r.run(0.6f, &finite);
      CHECK(finite);
      CHECK(peak <= 1.0f);
      CHECK(peak > 0.01f || ft == 0);  // comb can be quiet with these settings
    }
}

TEST(engine_voice_stealing_respects_limit) {
  Rig r;
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.p().voices = 4;
  r.publish();
  for (int k = 0; k < 8; ++k) r.on(k, 50.0f + k);
  r.run(0.1f);
  CHECK(r.e.activeVoices() == 4);
  // The newest four notes are the ones sounding.
  int newest = 0;
  for (int i = 0; i < kMaxVoices; ++i)
    if (r.e.voice(i).active() && r.e.voice(i).key() >= 4) ++newest;
  CHECK(newest == 4);
}

TEST(engine_same_pitch_on_two_keys_adds_in_phase) {
  Rig r;
  basePatch(r.p());
  r.p().levelSine = 1.0f;
  r.publish();
  r.on(0, 65.0f);
  float one = r.run(0.2f);
  r.on(40, 65.0f);  // E7 also plays F4
  r.run(0.05f);
  float two = r.run(0.2f);
  CHECK(two > 1.8f * one && two < 2.2f * one);
}

TEST(engine_pwm_extremes_are_silent) {
  Rig r;
  basePatch(r.p());
  r.p().levelSquare = 1.0f;
  r.p().pwm = 0.0f;
  r.publish();
  r.on(0, 60.0f);
  CHECK(r.run(0.2f) < 1e-4f);
}

TEST(engine_velocity_scales_level) {
  Rig loud, soft;
  for (Rig* r : {&loud, &soft}) { basePatch(r->p()); r->p().levelSine = 1.0f; r->publish(); }
  loud.on(0, 60.0f, 1.0f);
  soft.on(0, 60.0f, 0.0f);
  float a = loud.run(0.3f), b = soft.run(0.3f);
  CHECK_NEAR(gainToDb(b / a), -24.0, 0.5);
}

TEST(engine_lfo_start_high_settles_into_tune) {
  // Start-high on pitch: the note begins sharp and settles. Compare zero
  // crossings early and late.
  Rig r;
  basePatch(r.p());
  EngineParams& p = r.p();
  p.levelSine = 1.0f;
  p.lfoWave = LfoWave::StartHigh; p.lfoDest = LfoDest::Pitch; p.lfoStrength = 0.5f;  // ±3 semitones
  p.lfoPeriod = 0.5f; p.lfoDelay = 0.05f;
  r.publish();
  r.on(0, 69.0f);
  std::vector<float> x;
  r.run(1.2f, nullptr, &x);
  auto crossings = [&](size_t a, size_t b) { int c = 0; for (size_t i = a + 1; i < b; ++i) c += (x[i - 1] < 0) != (x[i] < 0); return c; };
  size_t w = (size_t)(0.1f * kSr);
  int early = crossings((size_t)(0.06f * kSr), (size_t)(0.06f * kSr) + w);
  int late = crossings(x.size() - w, x.size());
  CHECK(early > late);
  CHECK_NEAR(late, 88, 3);  // 440 Hz → 88 crossings per 0.1 s
}

TEST(engine_bitcrusher_keeps_quiet_notes_audible) {
  // At 100 % the crusher works relative to the signal level: a soft note keeps its level.
  float clean = 0, crushed = 0;
  for (int pass = 0; pass < 2; ++pass) {
    Rig r;
    basePatch(r.p());
    r.p().levelSaw = 1.0f;
    r.p().crush = pass ? 1.0f : 0.0f;
    r.publish();
    r.on(0, 48.0f, 0.0f);  // softest velocity: -24 dB
    std::vector<float> x;
    r.run(0.4f, nullptr, &x);
    double s = 0;
    for (size_t i = x.size() / 2; i < x.size(); ++i) s += x[i] * x[i];
    (pass ? crushed : clean) = (float)sqrt(s / (x.size() / 2));
  }
  CHECK(crushed > 0.25f * clean);
  CHECK(crushed < 4.0f * clean);
}

TEST(engine_bitcrusher_full_is_3_bits_at_2khz) {
  Rig r;
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.p().crush = 1.0f;
  r.publish();
  r.on(0, 60.0f);
  std::vector<float> x;
  r.run(0.5f, nullptr, &x);
  // Count sample-and-hold steps in the second half: about 2000 per second.
  int steps = 0;
  size_t a = x.size() / 2;
  for (size_t i = a + 1; i < x.size(); ++i) steps += x[i] != x[i - 1];
  float rate = steps / ((x.size() - a) / kSr);
  CHECK(rate > 1500.0f && rate < 2100.0f);
}

TEST(lfo_rise_keeps_rising_while_held) {
  Lfo lfo;
  lfo.trigger();
  float v = 0;
  for (int i = 0; i < 300; ++i) v = lfo.advance(0.01f, LfoWave::RiseLinear, 1.0f, 0.0f);  // 3 s, period 1 s
  CHECK_NEAR(v, 3.0, 0.02);
  lfo.trigger();
  for (int i = 0; i < 150; ++i) v = lfo.advance(0.01f, LfoWave::SinkLinear, 1.0f, 0.0f);
  CHECK_NEAR(v, -1.5, 0.02);
}

TEST(engine_switches_change_only_new_notes) {
  Rig r;
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.p().filterType = FilterType::Off;
  r.publish();
  r.on(0, 60.0f);
  r.run(0.05f);
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.p().filterType = FilterType::LP24;
  r.publish();
  r.on(1, 64.0f);
  r.run(0.05f);
  int off = -1, lp = -1;
  for (int i = 0; i < kMaxVoices; ++i) {
    if (!r.e.voice(i).active()) continue;
    if (r.e.voice(i).key() == 0) off = (int)r.e.voice(i).filterType();
    if (r.e.voice(i).key() == 1) lp = (int)r.e.voice(i).filterType();
  }
  CHECK(off == (int)FilterType::Off);
  CHECK(lp == (int)FilterType::LP24);
}

TEST(engine_formant_level_close_to_unfiltered) {
  float rms[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    Rig r;
    basePatch(r.p());
    r.p().levelSaw = 1.0f;
    r.p().filterType = pass ? FilterType::Formant : FilterType::Off;
    r.p().filterFreq = 120.0f;  // vowel E
    r.p().filterQ = 1.0f;       // resonance at centre
    r.publish();
    r.on(0, 48.0f);  // C3
    std::vector<float> x;
    r.run(0.4f, nullptr, &x);
    double s = 0;
    for (size_t i = x.size() / 2; i < x.size(); ++i) s += x[i] * x[i];
    rms[pass] = (float)sqrt(s / (x.size() / 2));
  }
  float db = gainToDb(rms[1] / rms[0]);
  CHECK(db > -6.0f && db < 3.0f);
}

TEST(engine_lower_voice_limit_fades_oldest_tails_first) {
  Rig r;
  basePatch(r.p());
  r.p().levelSaw = 1.0f;
  r.p().release = 5.0f;
  r.p().voices = 8;
  r.publish();
  for (int k = 0; k < 6; ++k) { r.on(k, 50.0f + k); r.run(0.01f); }
  r.off(0);
  r.off(1);  // keys 0 and 1 are now long release tails
  r.run(0.05f);
  CHECK(r.e.activeVoices() == 6);
  r.p().voices = 3;
  r.publish();
  r.run(0.05f);
  CHECK(r.e.activeVoices() == 3);
  bool keySounding[6] = {};
  for (int i = 0; i < kMaxVoices; ++i)
    if (r.e.voice(i).active()) keySounding[r.e.voice(i).key()] = true;
  // Tails 0 and 1 go first, then the oldest held note (key 2).
  CHECK(!keySounding[0] && !keySounding[1] && !keySounding[2]);
  CHECK(keySounding[3] && keySounding[4] && keySounding[5]);
}

TEST(meter_falls_at_set_speed) {
  PeakMeter m;
  float loud[kBlockSize], quiet[kBlockSize] = {};
  for (float& v : loud) v = 1.0f;
  m.update(loud, kBlockSize, kSr, 40.0f);
  CHECK_NEAR(m.levelDb(), 0.0, 1e-3);
  int blocks = (int)(0.15f * kSr / kBlockSize);  // one 6 dB LED step at 40 dB/s
  for (int i = 0; i < blocks; ++i) m.update(quiet, kBlockSize, kSr, 40.0f);
  CHECK_NEAR(m.levelDb(), -6.0, 0.2);
}

namespace {
// Renders a sine at A4 with the given detune mode and amount (0..1 of the full-knob spread).
std::vector<float> detuneRender(int copies, float amount, float seconds, float knob = 0.0f) {
  Rig r;
  basePatch(r.p());
  r.p().levelSine = 1.0f;
  r.p().detuneCopies = copies;
  r.p().detuneAmount = amount;
  r.p().detuneKnob = knob;
  r.p().reverbMix = 0.0f;
  r.p().delayMix = 0.0f;
  r.publish();
  r.on(0, 69.0f);
  std::vector<float> x;
  r.run(seconds, nullptr, &x);
  return x;
}
// Hann-windowed amplitude of frequency f (Hz) in x[from..].
double toneAmp(const std::vector<float>& x, size_t from, double f) {
  const size_t n = x.size() - from;
  double re = 0, im = 0;
  for (size_t i = 0; i < n; ++i) {
    double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
    double ph = 2.0 * M_PI * f * i / kSr;
    re += w * x[from + i] * cos(ph);
    im += w * x[from + i] * sin(ph);
  }
  return sqrt(re * re + im * im);
}
double centsAbove(double f, double c) { return f * pow(2.0, c / 1200.0); }
}  // namespace

TEST(detune_modes_same_level_without_detune) {
  float peak[3];
  for (int c = 1; c <= 3; ++c) {
    std::vector<float> x = detuneRender(c, 0.0f, 0.3f);
    float pk = 0;
    for (size_t i = x.size() / 2; i < x.size(); ++i) pk = fmaxf(pk, fabsf(x[i]));
    peak[c - 1] = pk;
  }
  CHECK_NEAR(gainToDb(peak[1] / peak[0]), 0.0, 0.1);
  CHECK_NEAR(gainToDb(peak[2] / peak[0]), 0.0, 0.1);
}

TEST(detune_full_knob_copy_pitches_and_levels) {
  // 3 copies at full knob: -23.17 c and +37.5 c around A4, each side at -6 dB.
  std::vector<float> three = detuneRender(3, 1.0f, 3.0f);
  size_t from = (size_t)(0.2f * kSr);
  double c = toneAmp(three, from, 440.0);
  double lo = toneAmp(three, from, centsAbove(440.0, -23.17));
  double hi = toneAmp(three, from, centsAbove(440.0, 37.5));
  double gap = toneAmp(three, from, centsAbove(440.0, 15.0));  // between the copies: no tone
  CHECK_NEAR(gainToDb((float)(lo / c)), -6.0, 0.5);
  CHECK_NEAR(gainToDb((float)(hi / c)), -6.0, 0.5);
  CHECK(gap < 0.05 * c);
  // 2 copies at full knob: +75 c at -6 dB, nothing below.
  std::vector<float> two = detuneRender(2, 1.0f, 3.0f);
  double m = toneAmp(two, from, 440.0);
  double up = toneAmp(two, from, centsAbove(440.0, 75.0));
  double below = toneAmp(two, from, centsAbove(440.0, -23.17));
  CHECK_NEAR(gainToDb((float)(up / m)), -6.0, 0.5);
  CHECK(below < 0.05 * m);
}

TEST(chorus_bypass_at_zero_and_level_kept_when_on) {
  Chorus ch;
  ch.init(kSr);
  uint32_t seed = 1;
  auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 8388608.0f - 1.0f; };
  for (int i = 0; i < 1000; ++i) { float x = noise(); CHECK(ch.process(x) == x); }
  ch.set(1.0f);
  double in = 0, out = 0;
  bool differs = false;
  for (int i = 0; i < (int)kSr; ++i) {
    float x = noise(), y = ch.process(x);
    if (i > 1000) { in += x * x; out += y * y; differs |= fabsf(y - x) > 1e-3f; }
  }
  CHECK(differs);
  CHECK_NEAR(10.0 * log10(out / in), 0.0, 3.0);
}

TEST(chorus_only_with_one_copy) {
  // With 3 copies and no detune the knob must not add chorus; with 1 copy it does.
  std::vector<float> three = detuneRender(3, 0.0f, 0.5f, 1.0f);
  std::vector<float> one = detuneRender(1, 0.0f, 0.5f, 1.0f);
  std::vector<float> plain = detuneRender(1, 0.0f, 0.5f, 0.0f);
  double d3 = 0, d1 = 0;
  for (size_t i = three.size() / 2; i < three.size(); ++i) {
    d3 += fabs(three[i] - plain[i]);
    d1 += fabs(one[i] - plain[i]);
  }
  CHECK(d3 < 1e-3);
  CHECK(d1 > 1.0);
}

TEST(random_start_phase_varies_between_notes) {
  for (int mode = 0; mode < 2; ++mode) {
    Rig r;
    basePatch(r.p());
    r.p().levelSaw = 1.0f;
    r.p().oscRandomPhase = mode == 1;
    r.p().reverbMix = 0.0f;
    r.p().delayMix = 0.0f;
    r.publish();
    std::vector<float> a, b;
    r.run(0.3f);  // let the master volume smoother settle
    r.on(0, 60.0f);
    r.run(0.02f, nullptr, &a);
    r.off(0);
    r.run(1.0f);
    r.on(0, 60.0f);
    r.run(0.02f, nullptr, &b);
    double diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff += fabs(a[i] - b[i]);
    if (mode == 0) CHECK(diff < 1e-3);
    else CHECK(diff > 0.1);
  }
}

TEST(delay_and_reverb_full_mix_is_equal_level) {
  // Knob 100 %: dry and wet both at the equal-power 50/50 point (0.707 each).
  TapeDelay d;
  std::vector<int16_t> mem(4410);
  d.init(mem.data(), (uint32_t)mem.size(), kSr);
  d.set(0.01f, 0.0f, 1.0f);
  for (int i = 0; i < (int)kSr; ++i) d.process(0.0f);  // let the delay time glide to 10 ms
  // A small DC step: before the echo arrives only the dry part comes out; once it
  // arrives the delayed part adds the same amount again.
  float dryOnly = 0.0f, both = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    float y = d.process(0.1f);
    if (i == 10) dryOnly = y;
    if (i == 1500) both = y;
  }
  CHECK_NEAR(dryOnly, 0.1f * 0.7071f, 1e-3);
  CHECK_NEAR(both - dryOnly, dryOnly, 0.003);
  static Reverb r;  // large: keep it off the stack
  r.init(kSr);
  r.set(1.0f, 1.0f);
  CHECK_NEAR(r.process(1.0f), 0.7071, 2e-3);  // dry part of the first sample
}
