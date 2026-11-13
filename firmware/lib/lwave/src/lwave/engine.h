// L-wave firmware — the sound engine: voices, voice allocation, global LFO and
// the shared effects chain. Plain C++, runs the same on the Teensy and a PC.
#pragma once
#include <atomic>
#include <stdint.h>
#include "lwave/config.h"
#include "lwave/dsp/basics.h"
#include "lwave/dsp/effects.h"
#include "lwave/dsp/lfo.h"
#include "lwave/engine_types.h"
#include "lwave/handoff.h"
#include "lwave/voice.h"

namespace lw {

/// The complete synth. The audio side calls process(); the control side
/// publishes parameters and posts note events. Both sides are lock-free.
class Engine {
 public:
  /// Prepares everything. delayBuffer (delayLen samples of int16) is the delay
  /// line; its length sets the longest delay time. Call before any audio runs.
  void init(float sampleRate, int16_t* delayBuffer, uint32_t delayLen);

  /// Audio side: renders n samples of mono output (-1..1) into out.
  void process(float* out, int n);

  /// Control side: the parameter snapshot buffer (fill writeBuffer(), then publish()).
  TripleBuffer<EngineParams>& params() { return params_; }

  /// Control side: queues a note or cue event. False if the queue is full.
  bool post(const NoteEvent& e) { return events_.push(e); }

  /// Longest delay time the delay buffer allows, in seconds.
  float maxDelaySeconds() const { return delay_.maxSeconds(); }
  /// Sample rate given to init().
  float sampleRate() const { return sr_; }

  /// Output level in dBFS (peak, falling at the meter_fall_db_s setting), for the LED meter.
  float outputLevelDb() const { return levelDb_.load(std::memory_order_relaxed); }
  /// Gain reduction of the output limiter in dB (0 = not limiting).
  float limiterReductionDb() const { return limitDb_.load(std::memory_order_relaxed); }
  /// Number of voices currently sounding.
  int activeVoices() const { return activeVoices_.load(std::memory_order_relaxed); }
  /// Read-only access to a voice (tests and debug).
  const Voice& voice(int i) const { return voices_[i]; }

 private:
  void handle(const NoteEvent& e, const EngineParams& p);
  void noteOn(const NoteEvent& e, const EngineParams& p);
  void noteOff(const NoteEvent& e);
  /// Polyphony limit from p.voices, clamped to 1..kMaxVoices.
  static int voiceLimit(const EngineParams& p);
  /// Voices holding a note (playing, releasing or about to start a stolen note);
  /// voices fading out to free themselves do not count.
  int busyVoices() const;
  /// Index of the oldest busy voice (only released ones if releasedOnly), or -1.
  int oldestVoice(bool releasedOnly) const;
  void processBlock(float* out, int n);

  float sr_ = kDefaultSampleRate;
  TripleBuffer<EngineParams> params_;
  SpscQueue<NoteEvent, 64> events_;
  Voice voices_[kMaxVoices];
  uint32_t ageCounter_ = 0;
  PinkNoise pink_;
  Lfo globalLfo_;
  LfoWave globalWave_ = LfoWave::Sine;   // switch settings captured at the last note-on
  LfoDest globalDest_ = LfoDest::Pitch;
  float globalLfoSmooth_ = 0.0f;
  Overdrive drive_;
  Chorus chorus_;
  Bitcrusher crusher_;
  TapeDelay delay_;
  Reverb reverb_;
  Limiter limiter_;
  CueSynth cue_;
  PeakMeter meter_;
  OnePole volume_;
  float noise_[kBlockSize] = {};
  float mix_[kBlockSize] = {};
  std::atomic<float> levelDb_{-120.0f};
  std::atomic<float> limitDb_{0.0f};
  std::atomic<int> activeVoices_{0};
};

}  // namespace lw
