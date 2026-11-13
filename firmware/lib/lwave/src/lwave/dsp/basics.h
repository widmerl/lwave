// L-wave firmware — small DSP helpers shared by the engine.
#pragma once
#include <math.h>
#include <stdint.h>

namespace lw {

/// Size of the sine lookup table (one full cycle).
constexpr int kSineTableSize = 2048;

/// Fills the sine table. Call once at start-up before any audio runs.
void initDspTables();

/// The sine table (kSineTableSize + 1 entries; filled by initDspTables()).
extern float gSineTable[kSineTableSize + 1];

/// Sine of one cycle position t (0..1), from the lookup table with linear interpolation.
inline float fastSin01(float t) {
  float pos = t * (float)kSineTableSize;
  int i = (int)pos;
  if (i < 0) i = 0;
  if (i >= kSineTableSize) i = kSineTableSize - 1;
  float frac = pos - (float)i;
  return gSineTable[i] + (gSineTable[i + 1] - gSineTable[i]) * frac;
}

/// Wraps a phase into 0..1.
inline float wrap01(float t) { return t - floorf(t); }

/// Rational tanh approximation; exact to about 2 % and saturates at ±1.
inline float fastTanh(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/// 2^x.
inline float exp2fast(float x) { return exp2f(x); }

/// Converts MIDI note (may be fractional) to frequency in Hz (A4 = 69 = 440 Hz).
inline float midiToHz(float note) { return 440.0f * exp2f((note - 69.0f) * (1.0f / 12.0f)); }

/// One-pole coefficient for a time constant of `seconds` at `sampleRate` (per-sample update).
inline float onePoleCoef(float seconds, float sampleRate) {
  if (seconds <= 0.0f) return 1.0f;
  return 1.0f - expf(-1.0f / (seconds * sampleRate));
}

/// Simple one-pole smoother: y += (target - y) * coef.
struct OnePole {
  float y = 0.0f;     ///< Current output.
  float coef = 1.0f;  ///< Update coefficient (1 = no smoothing).
  /// Sets the time constant.
  void setTime(float seconds, float sampleRate) { coef = onePoleCoef(seconds, sampleRate); }
  /// Moves one step toward `target` and returns the new value.
  float process(float target) { y += (target - y) * coef; return y; }
};

/// xorshift32 white-noise source.
struct WhiteNoise {
  uint32_t state = 0x12345678u;  ///< Generator state (never 0).
  /// Next sample, uniform in -1..1.
  float next() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return (int32_t)state * (1.0f / 2147483648.0f);
  }
};

/// Pink noise (Paul Kellet's economy filter on white noise), roughly ±1 peak.
struct PinkNoise {
  WhiteNoise white;           ///< Underlying white source.
  float b0 = 0, b1 = 0, b2 = 0;
  /// Next sample.
  float next() {
    float w = white.next();
    b0 = 0.99765f * b0 + w * 0.0990460f;
    b1 = 0.96300f * b1 + w * 0.2965164f;
    b2 = 0.57000f * b2 + w * 1.0526913f;
    return (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
  }
};

}  // namespace lw
