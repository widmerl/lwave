// L-wave firmware — the shared effects chain: overdrive, bitcrusher, delay,
// reverb, output limiter, plus the cue beeper and output meter.
#pragma once
#include <stdint.h>
#include "lwave/dsp/basics.h"

namespace lw {

/// Juno-style chorus for the mono voice mix: two taps of a short delay line
/// swing in opposite directions on one triangle LFO (like the left and right
/// outputs of a Juno chorus, summed to mono), a 9 kHz low-pass dulls them like
/// a bucket-brigade chip, and they are mixed with the dry signal. Active when
/// detune_copies = 1; the detune knob sets the amount.
class Chorus {
 public:
  /// Delay-line length in samples (power of two, > 5.5 ms at 96 kHz).
  static constexpr int kBufferSize = 1024;
  /// Prepares for the sample rate and clears the delay line.
  void init(float sampleRate);
  /// Amount 0..1. 0 is an exact bypass. The wet level reaches 50/50 at 40 %;
  /// the delay swing grows from ±0.6 to ±2 ms around 3.5 ms and the LFO rate
  /// from 0.45 to 0.9 Hz.
  void set(float amount);
  /// Processes one sample.
  float process(float x);
 private:
  float tap(float delaySamples) const;
  float buf_[kBufferSize] = {};
  int write_ = 0;
  float sr_ = 44100.0f, phase_ = 0.0f, inc_ = 0.0f, centre_ = 0.0f, swing_ = 0.0f;
  float dry_ = 1.0f, wet_ = 0.0f, lp_ = 0.0f, lpCoef_ = 0.0f;
  bool active_ = false;
};

/// Soft-clipping overdrive. Amount 0 is an exact bypass; 1 drives the input 30×.
/// Output is scaled by 1/sqrt(drive) so loudness stays roughly even.
class Overdrive {
 public:
  /// Sets the amount 0..1 (call per block).
  void setAmount(float amount);
  /// Processes one sample.
  float process(float x) const { return bypass_ ? x : fastTanh(x * drive_) * makeup_; }
 private:
  bool bypass_ = true;
  float drive_ = 1.0f, makeup_ = 1.0f;
};

/// Bitcrusher: lowers the sample rate first and the resolution later.
/// 0 = bypass; 100 % = 3 bits at 2 kHz. The quantiser follows the signal's own
/// level (a peak follower), so quiet notes keep their tone instead of vanishing.
class Bitcrusher {
 public:
  /// Sets the sample rate (call once).
  void init(float sampleRate);
  /// Sets the amount 0..1 (call per block or sub-block).
  void setAmount(float amount);
  /// Processes one sample.
  float process(float x);
  /// Lowest sample rate (at 100 %), Hz.
  static constexpr float kMinRateHz = 2000.0f;
  /// Fewest bits (at 100 %).
  static constexpr float kMinBits = 3.0f;
 private:
  bool bypass_ = true;
  float sr_ = 44100.0f, rateDiv_ = 1.0f, levels_ = 32768.0f, counter_ = 0.0f, held_ = 0.0f;
  float env_ = 0.0f, envRelease_ = 0.9999f;
};

/// Tape-style delay on an external 16-bit buffer. Changing the time glides the
/// read position (pitch bends instead of clicks). The feedback path has a fixed
/// 5 kHz low-pass.
class TapeDelay {
 public:
  /// Uses `buffer` (len samples) as the delay line; clears it.
  void init(int16_t* buffer, uint32_t len, float sampleRate);
  /// Longest delay time the buffer allows, in seconds.
  float maxSeconds() const;
  /// Sets time (s), feedback (0..0.9) and mix (0..1; 1 = echo as loud as the
  /// original, an equal-power 50/50 mix). Call per block.
  void set(float timeS, float feedback, float mix);
  /// Processes one sample.
  float process(float x);
 private:
  int16_t* buf_ = nullptr;
  uint32_t len_ = 0, w_ = 0;
  float sr_ = 44100.0f, target_ = 1000.0f, current_ = 1000.0f, glide_ = 0.0001f;
  float fb_ = 0.0f, dry_ = 1.0f, wet_ = 0.0f, lp_ = 0.0f, lpCoef_ = 0.5f;
  bool active_ = false;
};

/// Mono Freeverb-style reverb: fixed 20 ms pre-delay, 8 damped combs, 4 all-passes.
/// The time knob sets the decay time to -60 dB.
class Reverb {
 public:
  /// Sizes the delay lines for the sample rate and clears them.
  void init(float sampleRate);
  /// Sets decay time (s) and mix (0..1; 1 = reverb as loud as the dry sound, an
  /// equal-power 50/50 mix). Call per block.
  void set(float timeS, float mix);
  /// Processes one sample.
  float process(float x);
 private:
  static constexpr int kCombs = 8, kAllpasses = 4;
  static constexpr int kPoolSize = 15000;  // enough for 48 kHz
  float pool_[kPoolSize] = {};
  float* comb_[kCombs] = {};
  int combLen_[kCombs] = {}, combPos_[kCombs] = {};
  float combFb_[kCombs] = {}, combLp_[kCombs] = {};
  float* ap_[kAllpasses] = {};
  int apLen_[kAllpasses] = {}, apPos_[kAllpasses] = {};
  float* pre_ = nullptr;
  int preLen_ = 1, prePos_ = 0;
  float sr_ = 44100.0f, dry_ = 1.0f, wet_ = 0.0f;
  bool active_ = false;
};

/// Output limiter: fast peak follower with smooth gain, then a soft clip above 0.9.
class Limiter {
 public:
  /// Sets up the time constants.
  void init(float sampleRate);
  /// Processes one sample.
  float process(float x);
  /// Gain reduction currently applied, in dB (0 = not limiting).
  float reductionDb() const;
 private:
  float env_ = 0.0f, gain_ = 1.0f, attack_ = 0.5f, release_ = 0.0001f;
};

/// Beeps for calibration and status cues, mixed after the limiter at a fixed level.
class CueSynth {
 public:
  /// Sets the sample rate.
  void init(float sampleRate) { sr_ = sampleRate; }
  /// Starts a beep (up to 4 at once; the oldest is replaced when full).
  void play(float freqHz, float durationS, float level);
  /// Adds the running beeps to `out` (n samples).
  void render(float* out, int n);
 private:
  struct Beep { float phase = 0, inc = 0, level = 0; int remaining = 0, total = 0; };
  Beep beeps_[4];
  int next_ = 0;
  float sr_ = 44100.0f;
};

/// Output peak meter with an adjustable fall, read by the LED meter.
class PeakMeter {
 public:
  /// Updates with one block of output samples; between peaks the level falls
  /// by fallDbPerS decibels per second.
  void update(const float* x, int n, float sampleRate, float fallDbPerS);
  /// Current level in dBFS.
  float levelDb() const { return levelDb_; }
 private:
  float levelDb_ = -120.0f;
};

}  // namespace lw
