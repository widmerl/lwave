// L-wave firmware — ADSR envelope with analog-style exponential curves.
#pragma once
#include <math.h>
#include <stdint.h>

namespace lw {

/// ADSR envelope, 0..1. Attack is an RC curve aimed past 1 (like an analog
/// envelope) so it reaches 1 in the attack time. Decay and release fall
/// exponentially and reach -60 dB of the distance in their time. Releasing
/// during the attack decays from the current level. While "hold" is set the
/// decay waits at the peak (used while a key is still travelling down).
class Adsr {
 public:
  /// Envelope stage.
  enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };

  /// Sets the stage times in seconds. Cheap enough to call once per audio block.
  void setTimes(float attackS, float decayS, float releaseS, float sampleRate) {
    attackCoef_ = attackS < 0.0005f ? 1.0f : 1.0f - expf(-kAttackShape / (attackS * sampleRate));
    decayCoef_ = decayS < 0.0005f ? 1.0f : 1.0f - expf(-kSixtyDb / (decayS * sampleRate));
    releaseCoef_ = releaseS < 0.0005f ? 1.0f : 1.0f - expf(-kSixtyDb / (releaseS * sampleRate));
  }
  /// Sets the sustain level 0..1. Takes effect smoothly.
  void setSustain(float level) { sustain_ = level; }
  /// While true, the envelope stays at its peak instead of starting the decay.
  void setHold(bool hold) { hold_ = hold; }
  /// Starts the attack from the current level.
  void noteOn() { stage_ = Stage::Attack; }
  /// Starts the release from the current level.
  void noteOff() { if (stage_ != Stage::Idle) stage_ = Stage::Release; }
  /// Jumps to silence.
  void reset() { stage_ = Stage::Idle; level_ = 0.0f; }

  /// Advances one sample and returns the level.
  float process() {
    switch (stage_) {
      case Stage::Attack:
        level_ += (kAttackTarget - level_) * attackCoef_;
        if (level_ >= 1.0f) { level_ = 1.0f; stage_ = Stage::Decay; }
        break;
      case Stage::Decay:
        if (hold_) break;  // key still on its way down
        level_ += (sustain_ - level_) * decayCoef_;
        if (fabsf(level_ - sustain_) < 1e-4f) stage_ = Stage::Sustain;
        break;
      case Stage::Sustain:
        level_ += (sustain_ - level_) * decayCoef_;
        break;
      case Stage::Release:
        level_ -= level_ * releaseCoef_;
        if (level_ < 1e-5f) { level_ = 0.0f; stage_ = Stage::Idle; }
        break;
      case Stage::Idle:
        break;
    }
    return level_;
  }

  /// Current level.
  float level() const { return level_; }
  /// Current stage.
  Stage stage() const { return stage_; }
  /// True unless idle.
  bool active() const { return stage_ != Stage::Idle; }

 private:
  static constexpr float kAttackTarget = 1.3f;
  static constexpr float kAttackShape = 1.4663f;  // ln(1.3 / 0.3): reaches 1.0 in the attack time
  static constexpr float kSixtyDb = 6.9078f;      // ln(1000)
  Stage stage_ = Stage::Idle;
  float level_ = 0.0f, sustain_ = 1.0f;
  bool hold_ = false;
  float attackCoef_ = 1.0f, decayCoef_ = 1.0f, releaseCoef_ = 1.0f;
};

}  // namespace lw
