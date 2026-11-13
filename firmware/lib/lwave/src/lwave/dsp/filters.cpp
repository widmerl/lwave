#include "lwave/dsp/filters.h"
#include <math.h>
#include <string.h>
#include "lwave/curves.h"
#include "lwave/dsp/basics.h"

namespace lw {

namespace {
constexpr float kPi = 3.14159265358979f;

float clampCutoff(float hz, float sampleRate) { return clampf(hz, 10.0f, 0.45f * sampleRate); }

// Vowel formants (Hz) for A, E, I, O, U: Peterson & Barney's spoken vowels (adult male),
// as in "father", "bed", "beet", "bought", "boot". Levels (dB) follow speech spectra.
const float kVowelFreq[5][3] = {
    {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
const float kVowelDb[5][3] = {{0, -4, -10}, {0, -8, -12}, {0, 6, 2}, {0, -4, -16}, {0, -10, -18}};  // I lifted: its F1 sits on the fundamental
// Formant bandwidths (Hz) at resonance 50 %; resonance 0 doubles them, 100 % halves them.
const float kFormantBw[3] = {80.0f, 90.0f, 120.0f};
// Extra level for F2 and F3 (dB), so the vowel-defining upper formants stand out from a saw.
const float kFormantLiftDb[3] = {0.0f, 6.0f, 8.0f};
// Brings a bright source back to roughly its unfiltered loudness.
constexpr float kFormantMakeup = 3.5f;
}  // namespace

float resonanceNorm(float q) { return clampf(logf(q / 0.05f) / logf(400.0f), 0.0f, 1.0f); }

// ---- Svf -------------------------------------------------------------------
void Svf::set(float cutoffHz, float q, float sampleRate) {
  float g = tanf(kPi * clampCutoff(cutoffHz, sampleRate) / sampleRate);
  k = 1.0f / clampf(q, 0.05f, 40.0f);
  a1 = 1.0f / (1.0f + g * (g + k));
  a2 = g * a1;
  a3 = g * a2;
}

// ---- Ladder ----------------------------------------------------------------
void Ladder::set(float cutoffHz, float res01, float sampleRate) {
  float g = tanf(kPi * clampCutoff(cutoffHz, sampleRate) / sampleRate);
  G = g / (1.0f + g);
  beta = 1.0f / (1.0f + g);
  k = 3.95f * clampf(res01, 0.0f, 1.0f);
}

float Ladder::process(float x) {
  // Solve the feedback loop for the 4th stage output, then run the stages.
  float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
  float S = beta * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
  float y4 = (G4 * x + S) / (1.0f + k * G4);
  float u = fastTanh(x - k * y4);
  for (int i = 0; i < 4; ++i) {
    float v = (u - s[i]) * G;
    float y = v + s[i];
    s[i] = y + v;
    u = y;
  }
  return u * (1.0f + 0.5f * k);  // partial passband-loss compensation
}

// ---- Comb ------------------------------------------------------------------
void Comb::set(float pitchHz, float res01, float sampleRate) {
  delay = clampf(sampleRate / clampf(pitchHz, 1.0f, sampleRate), 2.0f, (float)(kCombBufferSize - 2));
  fb = 0.98f * clampf(res01, 0.0f, 1.0f);
  outGain = 1.0f - 0.9f * fb;
}

float Comb::process(float x) {
  float rp = (float)w - delay;
  if (rp < 0.0f) rp += (float)kCombBufferSize;
  int i0 = (int)rp;
  float frac = rp - (float)i0;
  int i1 = i0 + 1;
  if (i1 >= kCombBufferSize) i1 -= kCombBufferSize;
  float delayed = buf[i0] + (buf[i1] - buf[i0]) * frac;
  float y = x + fb * delayed;
  buf[w] = y;
  if (++w >= kCombBufferSize) w = 0;
  return y * outGain;
}

void Comb::reset() {
  memset(buf, 0, sizeof(buf));
  w = 0;
}

// ---- Formant ---------------------------------------------------------------
void Formant::set(float vowel, float res01, float sampleRate) {
  vowel = clampf(vowel, 0.0f, 4.0f);
  int v0 = (int)vowel;
  if (v0 > 3) v0 = 3;
  float f = vowel - (float)v0;
  float bwScale = exp2f(2.0f * (0.5f - clampf(res01, 0.0f, 1.0f)));
  for (int i = 0; i < 3; ++i) {
    float hz = kVowelFreq[v0][i] + (kVowelFreq[v0 + 1][i] - kVowelFreq[v0][i]) * f;
    // The table comes from sung vowels and already contains the voice's treble
    // roll-off; a saw rolls off too, so compress it and lift F2/F3 to keep them audible.
    float db = 0.35f * (kVowelDb[v0][i] + (kVowelDb[v0 + 1][i] - kVowelDb[v0][i]) * f) + kFormantLiftDb[i];
    bands[i].set(hz, hz / (kFormantBw[i] * bwScale), sampleRate);
    gains[i] = dbToGain(db);
  }
  // A low first formant (I, U) catches far more of a saw's energy; even the vowels out.
  float f1 = kVowelFreq[v0][0] + (kVowelFreq[v0 + 1][0] - kVowelFreq[v0][0]) * f;
  float even = sqrtf(f1 / 600.0f);
  // Fold the even-out, the band-pass normalisation (bp * k has unity peak) and
  // the makeup gain into one factor per band so process() does less work.
  for (int i = 0; i < 3; ++i) gains[i] *= even * bands[i].k * kFormantMakeup;
}

float Formant::process(float x) {
  float y = 0.0f;
  for (int i = 0; i < 3; ++i) {
    bands[i].process(x);
    y += gains[i] * bands[i].bp;
  }
  return y;
}

// ---- VoiceFilter -----------------------------------------------------------
void VoiceFilter::setType(FilterType t) {
  if (t == type_) return;
  bool wasSvf = type_ == FilterType::Notch || type_ == FilterType::BP || type_ == FilterType::HP ||
                type_ == FilterType::LP12;
  bool isSvf = t == FilterType::Notch || t == FilterType::BP || t == FilterType::HP || t == FilterType::LP12;
  if (!(wasSvf && isSvf)) {
    switch (t) {
      case FilterType::LP24: ladder_.reset(); break;
      case FilterType::Comb: comb_.reset(); break;
      case FilterType::Formant: formant_.reset(); break;
      case FilterType::Off: break;
      default: svf_.reset(); break;
    }
  }
  type_ = t;
}

void VoiceFilter::set(float cutoffHz, float q, float sampleRate) {
  switch (type_) {
    case FilterType::Comb: comb_.set(cutoffHz, resonanceNorm(q), sampleRate); break;
    case FilterType::Formant: {
      // Map 20 Hz .. 20 kHz on the cutoff scale to vowels A .. U.
      float pos = 4.0f * logf(clampf(cutoffHz, 20.0f, 20000.0f) / 20.0f) / logf(1000.0f);
      formant_.set(pos, resonanceNorm(q), sampleRate);
      break;
    }
    case FilterType::LP24: ladder_.set(cutoffHz, resonanceNorm(q), sampleRate); break;
    case FilterType::Off: break;
    default: svf_.set(cutoffHz, q, sampleRate); break;
  }
}

float VoiceFilter::process(float x) {
  switch (type_) {
    case FilterType::Comb: return comb_.process(x);
    case FilterType::Formant: return formant_.process(x);
    case FilterType::Notch: svf_.process(x); return svf_.lp + svf_.hp;
    case FilterType::BP: svf_.process(x); return svf_.bp * svf_.k;
    case FilterType::HP: svf_.process(x); return svf_.hp;
    case FilterType::LP12: svf_.process(x); return svf_.lp;
    case FilterType::LP24: return ladder_.process(x);
    case FilterType::Off: default: return x;
  }
}

void VoiceFilter::reset() {
  svf_.reset();
  ladder_.reset();
  comb_.reset();
  formant_.reset();
}

}  // namespace lw
