#include "teensy_audio.h"

void EngineStream::update() {
  audio_block_t* block = allocate();
  if (!block) return;
  engine_.process(buf_, AUDIO_BLOCK_SAMPLES);
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
    float v = buf_[i] * 32767.0f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    block->data[i] = (int16_t)v;
  }
  transmit(block, 0);
  release(block);
}
