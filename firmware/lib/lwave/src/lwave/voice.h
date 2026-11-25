// L-wave firmware — one synth voice: oscillators, mix, filter, envelope, LFO.
#pragma once
#include <stdint.h>
#include "lwave/dsp/envelope.h"
#include "lwave/dsp/filters.h"
#include "lwave/dsp/lfo.h"
#include "lwave/engine_types.h"

namespace lw {

/// What the Velocity knob (0..1) gives strike speed and key depth, blended
/// smoothly between four points: 0 % speed sets the volume, the Attack knob the
/// attack; 33 % speed also scales the attack time; 66 % speed only scales the
/// attack time; 100 % key depth is the volume, rising no faster than 0 to full
/// in the Attack knob's time.
struct VelocityKnobWeights {
  float speedToLevel;   ///< 1 = strike speed sets the volume (vel_range_db), 0 = no effect.
  float speedToAttack;  ///< 1 = strike speed scales the attack time by vel_attack_span^±1.
  float depthIsLevel;   ///< 1 = key depth is the volume from the note-on point; the Attack knob only limits how fast it rises.
};

/// Weights for a Velocity knob position 0..1.
VelocityKnobWeights velocityKnobWeights(float knob);

/// One voice. Plays one key at a time; the engine assigns keys to voices.
/// The rotary-switch settings (filter type, LFO waveform, LFO destination) are
/// captured when the note starts, like the octave, so turning a switch only
/// affects new notes.
class Voice {
 public:
  /// Number of oscillator phases: the centre (main) copy, the low and the high
  /// detuned copies (each shared by saw, square, triangle and sine, which run at
  /// the same pitch), sub 1, sub 2.
  static constexpr int kNumOsc = 5;

  /// Starts a note now (the voice must be idle or its fade-out finished). Switch settings come from p.
  void start(uint8_t key, float note, float velocity, float depth, uint32_t age, const EngineParams& p);
  /// Starts a note after a 3 ms fade-out of the current one (voice stealing).
  void steal(uint8_t key, float note, float velocity, float depth, uint32_t age, const EngineParams& p,
             float sampleRate);
  /// Releases the note; `frozenDepth` is the key depth kept for the release tail.
  void release(float frozenDepth);
  /// Copies oscillator phases from another voice (same pitch on two keys stays in phase).
  void copyPhases(const Voice& other);
  /// Silences the voice immediately.
  void kill();
  /// Fades the voice out over 3 ms and frees it (polyphony limit lowered).
  /// A stolen note waiting to start is dropped.
  void fadeOut(float sampleRate);

  /// Sets the envelope times for this block (uses the voice's own velocity).
  void prepareBlock(const EngineParams& p, float sampleRate);
  /// Renders n samples (multiple of kSubBlock) and adds them to out. noise holds n pink-noise samples.
  void render(const EngineParams& p, const float* noise, float* out, int n, float sampleRate);

  bool active() const { return active_; }      ///< Playing or fading out.
  bool released() const { return released_; }  ///< Key released (envelope in release).
  bool stealing() const { return fading_; }    ///< Fading out (before a stolen start, or to free the voice).
  bool fadingOut() const { return fading_ && !hasPending_; }  ///< Fading out to free the voice.
  uint8_t key() const { return key_; }         ///< Key index of the current note.
  float note() const { return note_; }         ///< MIDI note of the current note.
  uint32_t age() const { return age_; }        ///< Start order, for stealing the oldest.
  float envLevel() const { return env_.level(); } ///< Envelope level, for tests and debug.
  FilterType filterType() const { return filterType_; } ///< Filter type captured at note-on.

  /// Largest pitch offset any LFO destination can apply, in semitones.
  static constexpr float kMaxLfoSemitones = 48.0f;
  /// Largest volume gain the LFO can apply (+12 dB).
  static constexpr float kMaxLfoGain = 4.0f;
  /// Largest detune the LFO can reach, as a multiple of the full-knob spread.
  static constexpr float kMaxLfoDetune = 4.0f;

 private:
  // Settings captured at note-on: the three switches and the start-phase mode.
  struct Switches { FilterType filter; LfoWave wave; LfoDest dest; bool randomPhase; };
  struct Pending { uint8_t key; float note, velocity, depth; uint32_t age; bool released; float frozen; Switches sw; };
  void begin(uint8_t key, float note, float velocity, float depth, uint32_t age, const Switches& sw);
  /// Level factor from key depth for the current knob and settings (before smoothing).
  float depthLevel(const EngineParams& p) const;
  /// Depth-driven level 0..1 the key asks for (Velocity knob 100 %), before the Attack limit.
  float swellTarget(const EngineParams& p) const;

  bool active_ = false, released_ = false, fading_ = false, hasPending_ = false;
  uint8_t key_ = 0;
  float note_ = 60.0f, velocity_ = 1.0f, depth_ = 1.0f, depthGain_ = 1.0f;
  // Key still travelling down (depth-driven mode holds the envelope peak until it arrives).
  bool arrived_ = false;
  float maxDepth_ = 0.0f, sinceRise_ = 0.0f;
  // Depth-driven level after the Attack knob's rise limit (0 to 1 in no less than the attack time).
  float swellNow_ = 0.0f;
  uint32_t age_ = 0;
  float fadeGain_ = 1.0f, fadeStep_ = 0.0f;
  Pending pending_{};
  FilterType filterType_ = FilterType::Off;
  LfoWave lfoWave_ = LfoWave::Sine;
  LfoDest lfoDest_ = LfoDest::Pitch;
  Adsr env_;
  Lfo lfo_;
  float lfoSmooth_ = 0.0f, volModNow_ = 1.0f;
  VoiceFilter filter_;
  float phase_[kNumOsc] = {};
};

}  // namespace lw
