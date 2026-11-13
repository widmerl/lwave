#include "wav.h"
#include <stdint.h>
#include <stdio.h>

namespace {
void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
}  // namespace

bool writeWav(const char* path, const std::vector<float>& samples, int sampleRate) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  uint32_t dataBytes = (uint32_t)samples.size() * 2;
  fwrite("RIFF", 1, 4, f);
  put32(f, 36 + dataBytes);
  fwrite("WAVEfmt ", 1, 8, f);
  put32(f, 16);
  put16(f, 1);  // PCM
  put16(f, 1);  // mono
  put32(f, (uint32_t)sampleRate);
  put32(f, (uint32_t)sampleRate * 2);
  put16(f, 2);
  put16(f, 16);
  fwrite("data", 1, 4, f);
  put32(f, dataBytes);
  for (float s : samples) {
    float v = s * 32767.0f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    put16(f, (uint16_t)(int16_t)v);
  }
  return fclose(f) == 0;
}
