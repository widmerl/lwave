// Minimal 16-bit mono WAV writer for the renderer.
#pragma once
#include <vector>

/// Writes samples (-1..1) as a 16-bit mono WAV file. Returns false on I/O error.
bool writeWav(const char* path, const std::vector<float>& samples, int sampleRate);
