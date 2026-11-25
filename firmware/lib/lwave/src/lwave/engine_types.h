// L-wave firmware — what the control side sends to the sound engine:
// a parameter snapshot (engine units) and note events.
#pragma once
#include <stdint.h>
#include "lwave/config.h"
#include "lwave/dsp/filters.h"
#include "lwave/dsp/lfo.h"

namespace lw {

/// Everything the engine needs from the panel, already converted to engine
/// units by ControlMapper. Published as a whole through a TripleBuffer.
struct EngineParams {
  // LFO
  LfoWave lfoWave = LfoWave::Sine;   ///< Waveform switch.
  LfoDest lfoDest = LfoDest::Pitch;  ///< Destination switch.
  float lfoPeriod = 1.0f;            ///< Seconds per cycle (or ramp time for one-shots).
  float lfoDelay = 0.05f;            ///< Seconds: fade-in time (repeating) or start delay (one-shot).
  float lfoStrength = 0.0f;          ///< 0..1.
  // Envelope and velocity
  float velocityKnob = 0.0f;         ///< 0..1: what strike speed and key depth control (see velocityKnobWeights()).
  float attack = 0.005f;             ///< Seconds.
  float decay = 0.3f;                ///< Seconds.
  float sustain = 0.8f;              ///< 0..1.
  float release = 0.3f;              ///< Seconds.
  // Effects
  float reverbTime = 1.0f;           ///< Seconds to -60 dB.
  float reverbMix = 0.0f;            ///< 0..1.
  float delayTime = 0.3f;            ///< Seconds.
  float delayFeedback = 0.0f;        ///< 0..0.9.
  float delayMix = 0.0f;             ///< 0..1.
  float drive = 0.0f;                ///< Overdrive 0..1.
  float crush = 0.0f;                ///< Bitcrusher 0..1.
  // Filter
  FilterType filterType = FilterType::Off;  ///< Filter switch.
  float filterFreq = 20000.0f;       ///< Hz.
  float filterQ = 0.7f;              ///< 0.05..20.
  float filterEnv = 0.0f;            ///< 0..1.
  float filterTrack = 0.0f;          ///< 0..1 (1 = cutoff follows the note, pivot C4).
  // Oscillators
  float tuneCents = 0.0f;            ///< -50..50.
  float pwm = 0.5f;                  ///< Pulse width 0..1.
  float detuneAmount = 0.0f;         ///< Detune knob through its curve, 0..1 (1/3 at 70 %); scales the copy offsets below.
  float detuneKnob = 0.0f;           ///< Detune knob position 0..1 (chorus amount when detuneCopies = 1).
  float levelPink = 0.0f;            ///< Linear gains from the level sliders.
  float levelSquare = 0.0f;
  float levelSaw = 1.0f;
  float levelTri = 0.0f;
  float levelSine = 0.0f;
  float levelSub1 = 0.0f;
  float levelSub2 = 0.0f;
  float volume = 1.0f;               ///< Master volume, linear gain.
  // From Settings
  int voices = 10;                   ///< Polyphony in use.
  float velRangeDb = 24.0f;          ///< See Settings::velRangeDb.
  float velAttackSpan = 5.0f;        ///< See Settings::velAttackSpan.
  int depthMode = 1;                 ///< See Settings::depthMode.
  float swellCurve = 2.0f;           ///< See Settings::swellCurve.
  float swellStart = 0.3f;           ///< Key travel where depth-driven level starts: the note-on point (noteOnPoint()).
  float filterEnvOct = 6.0f;         ///< See Settings::filterEnvOct.
  float meterFallDbS = 40.0f;        ///< See Settings::meterFallDbS.
  int detuneCopies = 2;              ///< See Settings::detuneCopies (1..3).
  float detuneSideGain = 0.5012f;    ///< Linear gain from Settings::detuneSidesDb.
  float detuneLowC = 23.17f;         ///< See Settings::detuneLowC.
  float detuneHighC = 37.5f;         ///< See Settings::detuneHighC.
  float detuneUpperC = 75.0f;        ///< See Settings::detuneUpperC.
  float detuneUpperGain = 0.5012f;   ///< Linear gain from Settings::detuneUpperDb.
  bool oscRandomPhase = false;       ///< See Settings::oscRandomPhase.
  // Live key depth (0..1 of full travel), indexed like kKeys.
  float keyDepth[kNumKeys] = {};
};

/// A note or cue event, sent through an SpscQueue.
struct NoteEvent {
  /// Event kind.
  enum class Type : uint8_t { On, Off, Cue, AllOff };
  Type type = Type::On;   ///< Kind.
  uint8_t key = 0;        ///< Key index (On/Off).
  float note = 60.0f;     ///< MIDI note including octave shift (On).
  float velocity = 1.0f;  ///< 0..1 (On).
  float depth = 1.0f;     ///< Key depth: at note-on (On) or frozen for the release (Off).
  float cueHz = 0.0f;     ///< Beep frequency (Cue).
  float cueSeconds = 0.0f;///< Beep length (Cue).
};

}  // namespace lw
