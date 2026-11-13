// L-wave firmware — LFO with four one-shot shapes and four repeating shapes.
#pragma once
#include <math.h>
#include <stdint.h>
#include "lwave/dsp/basics.h"

namespace lw {

/// LFO waveform, in the order of the waveform switch positions 1..8.
enum class LfoWave : uint8_t { RiseLinear, SinkLinear, StartHigh, StartLow, Square, Saw, Triangle, Sine };

/// LFO destination, in the order of the destination switch positions 1..8.
enum class LfoDest : uint8_t { Bitcrush, Delay, Detune, Pwm, FilterFreq, VolumePitch, Pitch, Volume };

/// True for the four shapes that run once per note instead of repeating.
inline bool isOneShot(LfoWave w) { return static_cast<uint8_t>(w) < 4; }

/// LFO, output -1..1.
///  * Repeating shapes (Square, Saw, Triangle, Sine): frequency = 1 / period;
///    the depth fades in linearly over the delay time.
///  * Rise / Sink linear: hold 0 during the delay, then move linearly by 1 per
///    period (reaching ±1, the set strength, after one period) and keep going
///    for as long as the note is held; each destination clamps at its own end of
///    travel. Capped at ±64 periods.
///  * Start high / Start low: begin at +1 / -1 and decay exponentially to 0
///    (to 1 % after one period), e.g. pitch starting out of tune and settling.
///    They hold their start value during the delay.
class Lfo {
 public:
  /// Largest value Rise / Sink linear reach (in periods).
  static constexpr float kRampCap = 64.0f;
  /// Restarts the shape and the delay (called on note-on).
  void trigger() { time_ = 0.0f; phase_ = 0.0f; }
  /// Restarts only the delay / fade-in, keeping the phase (used by the free-running global LFO).
  void restartDelay() { time_ = 0.0f; }

  /// Advances by dtSeconds and returns the value.
  float advance(float dtSeconds, LfoWave wave, float periodS, float delayS) {
    time_ += dtSeconds;
    if (periodS < 0.001f) periodS = 0.001f;
    if (!isOneShot(wave)) {
      phase_ = wrap01(phase_ + dtSeconds / periodS);
      float v;
      switch (wave) {
        case LfoWave::Square: v = phase_ < 0.5f ? 1.0f : -1.0f; break;
        case LfoWave::Saw: v = 2.0f * phase_ - 1.0f; break;
        case LfoWave::Triangle: v = 1.0f - 4.0f * fabsf(phase_ - 0.5f); break;
        default: v = fastSin01(phase_); break;
      }
      float fade = delayS > 0.0f ? time_ / delayS : 1.0f;
      return fade < 1.0f ? v * fade : v;
    }
    float t = time_ - delayS;
    switch (wave) {
      case LfoWave::RiseLinear: return t <= 0.0f ? 0.0f : fminf(t / periodS, kRampCap);
      case LfoWave::SinkLinear: return t <= 0.0f ? 0.0f : -fminf(t / periodS, kRampCap);
      case LfoWave::StartHigh: return t <= 0.0f ? 1.0f : expf(-4.6052f * t / periodS);
      default: return t <= 0.0f ? -1.0f : -expf(-4.6052f * t / periodS);  // StartLow
    }
  }

 private:
  float time_ = 0.0f;
  float phase_ = 0.0f;
};

}  // namespace lw
