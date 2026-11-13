// L-wave firmware — the per-voice filter and its building blocks.
#pragma once
#include <stdint.h>
#include "lwave/config.h"

namespace lw {

/// Filter type, in the order of the filter switch positions 1..8.
enum class FilterType : uint8_t { Comb, Formant, Notch, BP, HP, LP12, LP24, Off };

/// Topology-preserving state-variable filter (Zavalishin / Simper). Stable up to near Nyquist.
class Svf {
 public:
  /// Sets cutoff (Hz), quality factor and sample rate. Call at control rate.
  void set(float cutoffHz, float q, float sampleRate);
  /// Processes one sample; afterwards lp, bp, hp hold the three outputs.
  /// Inline: it runs once per sample per voice (three times for the formant filter).
  void process(float x) {
    float v3 = x - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    lp = v2;
    bp = v1;
    hp = x - k * v1 - v2;
  }
  /// Clears the state.
  void reset() { ic1 = ic2 = 0.0f; }
  float lp = 0, bp = 0, hp = 0;  ///< Outputs of the last process() call.
  float k = 1.0f;                ///< Damping = 1/Q (bp * k has unity peak gain).
 private:
  float a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
};

/// Four-pole ladder low-pass (zero-delay feedback, soft-saturated input).
class Ladder {
 public:
  /// Sets cutoff (Hz) and resonance 0..1 (1 = edge of self-oscillation).
  void set(float cutoffHz, float res01, float sampleRate);
  /// Filters one sample.
  float process(float x);
  /// Clears the state.
  void reset() { s[0] = s[1] = s[2] = s[3] = 0.0f; }
 private:
  float G = 0, beta = 1, k = 0, s[4] = {0, 0, 0, 0};
};

/// Feedback comb filter tuned to a pitch: y[n] = x[n] + fb * y[n - fs/f].
class Comb {
 public:
  /// Sets the comb pitch (Hz) and resonance 0..1 (feedback up to 0.98).
  void set(float pitchHz, float res01, float sampleRate);
  /// Filters one sample.
  float process(float x);
  /// Clears the delay line.
  void reset();
 private:
  float buf[kCombBufferSize] = {};
  int w = 0;
  float delay = 100.0f, fb = 0.0f, outGain = 1.0f;
};

/// Three parallel band-passes at vowel formants; the position sweeps A-E-I-O-U.
/// Bandwidths follow real voices (80 / 90 / 120 Hz at resonance 50 %).
class Formant {
 public:
  /// Sets the vowel position 0..4 (A, E, I, O, U) and resonance 0..1 (formant sharpness).
  void set(float vowel, float res01, float sampleRate);
  /// Filters one sample.
  float process(float x);
  /// Clears the state.
  void reset() { for (auto& f : bands) f.reset(); }
 private:
  Svf bands[3];
  float gains[3] = {1, 0, 0};  ///< Band-pass output gains, including damping k and makeup.
};

/// The filter inside each voice: one of the eight types, chosen by the filter switch.
class VoiceFilter {
 public:
  /// Selects the type; resets the state of a type that was not running.
  void setType(FilterType t);
  /// Sets cutoff (Hz), Q (0.05..20 from the knob) at control rate.
  void set(float cutoffHz, float q, float sampleRate);
  /// Filters one sample.
  float process(float x);
  /// Clears all state.
  void reset();
  /// Current type.
  FilterType type() const { return type_; }

 private:
  FilterType type_ = FilterType::Off;
  Svf svf_;
  Ladder ladder_;
  Comb comb_;
  Formant formant_;
};

/// Maps the resonance knob value (Q 0.05..20) to 0..1 on a log scale (Q = 1 → 0.5).
float resonanceNorm(float q);

}  // namespace lw
