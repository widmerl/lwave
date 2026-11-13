// L-wave firmware (Teensy only) — wraps the portable Engine as a Teensy
// Audio library object, so the library's I2S driver plays it.
#pragma once
#include <Arduino.h>  // AudioStream.h expects Arduino types first
#include <AudioStream.h>
#include "lwave/engine.h"

/// Audio object with no inputs and one output: the engine's mono signal.
class EngineStream : public AudioStream {
 public:
  /// Wraps `engine` (which must be init()-ed before AudioMemory() is called).
  explicit EngineStream(lw::Engine& engine) : AudioStream(0, nullptr), engine_(engine) {}
  /// Called by the audio library every 128 samples (in an interrupt).
  void update() override;

 private:
  lw::Engine& engine_;
  float buf_[AUDIO_BLOCK_SAMPLES];
};
