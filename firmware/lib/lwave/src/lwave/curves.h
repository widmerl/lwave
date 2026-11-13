// L-wave firmware — knob curves.
// Every pot is linear (B taper); these functions turn its calibrated position
// p (0..1) into the value printed on the panel. Each curve passes exactly
// through the panel's printed points.
#pragma once

namespace lw {

/// Straight line from lo (p = 0) to hi (p = 1).
float curveLinear(float p, float lo, float hi);

/// Two log segments: lo at p = 0, mid at p = 0.5, hi at p = 1.
/// Used for LFO period, LFO delay, reverb and delay time, resonance, filter frequency.
float curveLog3(float p, float lo, float mid, float hi);

/// "Log to 0": a log-shaped curve shifted so that p = 0 gives exactly 0.
/// v(p) = a * (b^p - 1), fitted through (pMid, vMid) and (1, vHi).
struct LogTo0 {
  float a = 0.0f;  ///< Scale factor.
  float b = 1.0f;  ///< Base.
  /// Fits the curve through (pMid, vMid) and (1, vHi). Requires 0 < pMid < 1 and vHi > vMid > 0.
  static LogTo0 fit(float pMid, float vMid, float vHi);
  /// Evaluates the curve at p (clamped to 0..1).
  float operator()(float p) const;
};

/// Level slider curve: linear in dB from dbLo (p = fadeEnd) to dbHi (p = 1);
/// below fadeEnd the gain fades linearly to silence. Returns linear gain.
float curveDbTo0(float p, float dbLo, float dbHi, float fadeEnd);

/// dB to linear amplitude.
float dbToGain(float db);

/// Linear amplitude to dB (floored at -120 dB).
float gainToDb(float gain);

/// Clamps x to [lo, hi].
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

}  // namespace lw
