// L-wave firmware — one synth voice: oscillators, mix, filter, envelope, LFO.
#pragma once
#include <stdint.h>
#include "lwave/dsp/envelope.h"
#include "lwave/dsp/filters.h"
#include "lwave/dsp/lfo.h"
#include "lwave/engine_types.h"

namespace lw {

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

  bool active_ = false, released_ = false, fading_ = false, hasPending_ = false;
  uint8_t key_ = 0;
  float note_ = 60.0f, velocity_ = 1.0f, depth_ = 1.0f, depthGain_ = 1.0f;
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
