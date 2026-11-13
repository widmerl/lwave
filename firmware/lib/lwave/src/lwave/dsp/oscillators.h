// L-wave firmware — oscillator waveforms.
// All take a phase t in 0..1 and the phase increment dt (frequency / sample rate).
// Saw and pulse use PolyBLEP to suppress aliasing.
#pragma once
#include "lwave/dsp/basics.h"

namespace lw {

/// PolyBLEP correction for a unit step at phase 0.
inline float polyBlep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

/// Band-limited rising saw, -1..1.
inline float oscSaw(float t, float dt) { return 2.0f * t - 1.0f - polyBlep(t, dt); }

/// Band-limited pulse with positive width w (0..1), DC removed so its level
/// stays centred. At w = 0 or 1 the wave disappears (silence).
inline float oscPulse(float t, float dt, float w) {
  if (w <= dt || w >= 1.0f - dt) return 0.0f;
  float y = (t < w) ? 1.0f : -1.0f;
  y += polyBlep(t, dt);
  y -= polyBlep(wrap01(t - w + 1.0f), dt);
  return y - (2.0f * w - 1.0f);
}

/// Triangle, -1..1 (naive; its harmonics fall fast enough that aliasing is low).
inline float oscTri(float t) { return 4.0f * fabsf(t - 0.5f) - 1.0f; }

/// Sine, -1..1.
inline float oscSine(float t) { return fastSin01(t); }

}  // namespace lw
