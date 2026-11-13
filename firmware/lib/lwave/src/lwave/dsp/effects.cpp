#include "lwave/dsp/effects.h"
#include <math.h>
#include <string.h>
#include "lwave/curves.h"

namespace lw {

namespace {
constexpr float kHalfPi = 1.57079632679f;
constexpr float kPi = 3.14159265358979f;
}

// ---- Overdrive -------------------------------------------------------------
void Overdrive::setAmount(float amount) {
  bypass_ = amount < 0.005f;
  drive_ = powf(30.0f, clampf(amount, 0.0f, 1.0f));
  makeup_ = 1.0f / sqrtf(drive_);
}

// ---- Chorus ----------------------------------------------------------------
void Chorus::init(float sampleRate) {
  sr_ = sampleRate;
  for (float& v : buf_) v = 0.0f;
  write_ = 0;
  phase_ = lp_ = 0.0f;
  lpCoef_ = 1.0f - expf(-2.0f * kPi * 9000.0f / sampleRate);
  centre_ = 0.0035f * sampleRate;
  set(0.0f);
}

void Chorus::set(float amount) {
  amount = clampf(amount, 0.0f, 1.0f);
  active_ = amount > 0.0f;
  const float theta = 0.25f * kPi * clampf(amount / 0.4f, 0.0f, 1.0f);  // equal-power mix
  dry_ = cosf(theta);
  wet_ = sinf(theta);
  swing_ = (0.0006f + 0.0014f * amount) * sr_;
  inc_ = (0.45f + 0.45f * amount) / sr_;
}

float Chorus::tap(float d) const {
  float rp = (float)write_ - d;
  float fl = floorf(rp);
  int i = (int)fl;
  float fr = rp - fl;
  float a = buf_[i & (kBufferSize - 1)], b = buf_[(i + 1) & (kBufferSize - 1)];
  return a + (b - a) * fr;
}

float Chorus::process(float x) {
  buf_[write_] = x;
  float y = x;
  if (active_) {
    phase_ += inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    const float tri = 4.0f * fabsf(phase_ - 0.5f) - 1.0f;  // -1..1
    // 0.6 rather than 0.5: the two taps are only partly correlated, so this keeps
    // the level within about 1 dB of the dry signal at every amount.
    const float wet = 0.6f * (tap(centre_ + swing_ * tri) + tap(centre_ - swing_ * tri));
    lp_ += (wet - lp_) * lpCoef_;
    y = dry_ * x + wet_ * lp_;
  }
  write_ = (write_ + 1) & (kBufferSize - 1);
  return y;
}

// ---- Bitcrusher ------------------------------------------------------------
void Bitcrusher::init(float sampleRate) {
  sr_ = sampleRate;
  envRelease_ = expf(-1.0f / (0.2f * sampleRate));  // 200 ms peak-follower release
}

void Bitcrusher::setAmount(float amount) {
  amount = clampf(amount, 0.0f, 1.0f);
  bypass_ = amount < 0.005f;
  // Sample rate falls first (full drop reached at 75 %), resolution mostly after 40 %.
  float maxDiv = sr_ / kMinRateHz;
  rateDiv_ = powf(maxDiv > 1.0f ? maxDiv : 1.0f, clampf(amount / 0.75f, 0.0f, 1.0f));
  float late = clampf((amount - 0.4f) / 0.6f, 0.0f, 1.0f);
  float bits = 16.0f - (16.0f - kMinBits) * late * late;
  levels_ = powf(2.0f, bits - 1.0f);  // steps per half range
}

float Bitcrusher::process(float x) {
  float a = fabsf(x);
  env_ = a > env_ ? a : env_ * envRelease_;
  if (bypass_) return x;
  counter_ += 1.0f;
  if (counter_ >= rateDiv_) {
    counter_ -= rateDiv_;
    float step = (env_ > 1e-6f ? env_ : 1e-6f) / levels_;  // quantise relative to the signal level
    held_ = floorf(x / step + 0.5f) * step;
  }
  return held_;
}

// ---- TapeDelay -------------------------------------------------------------
void TapeDelay::init(int16_t* buffer, uint32_t len, float sampleRate) {
  buf_ = buffer;
  len_ = len;
  w_ = 0;
  sr_ = sampleRate;
  if (buf_ && len_) memset(buf_, 0, len_ * sizeof(int16_t));
  glide_ = onePoleCoef(0.15f, sampleRate);
  lpCoef_ = 1.0f - expf(-2.0f * 3.14159265f * 5000.0f / sampleRate);
  // Start at 0.3 s, or at the buffer length if that is shorter.
  float maxLen = len_ > 4 ? (float)(len_ - 4) : 1.0f;
  current_ = target_ = clampf(0.3f * sampleRate, 1.0f, maxLen);
}

float TapeDelay::maxSeconds() const { return len_ > 4 ? (float)(len_ - 4) / sr_ : 0.0f; }

void TapeDelay::set(float timeS, float feedback, float mix) {
  target_ = clampf(timeS * sr_, 1.0f, (float)(len_ > 4 ? len_ - 4 : 1));
  fb_ = clampf(feedback, 0.0f, 0.9f);
  // Knob 100 % = echo as loud as the original (equal-power 50/50 point).
  mix = 0.5f * clampf(mix, 0.0f, 1.0f);
  dry_ = cosf(mix * kHalfPi);
  wet_ = sinf(mix * kHalfPi);
  active_ = buf_ != nullptr && len_ > 4;
}

float TapeDelay::process(float x) {
  if (!active_) return x;
  current_ += (target_ - current_) * glide_;
  float rp = (float)w_ - current_;
  if (rp < 0.0f) rp += (float)len_;
  uint32_t i0 = (uint32_t)rp;
  float frac = rp - (float)i0;
  uint32_t i1 = i0 + 1 >= len_ ? 0 : i0 + 1;
  float a = buf_[i0] * (1.0f / 32768.0f), b = buf_[i1] * (1.0f / 32768.0f);
  float delayed = a + (b - a) * frac;
  lp_ += (delayed - lp_) * lpCoef_;
  float in = fastTanh(x + fb_ * lp_);
  buf_[w_] = (int16_t)(clampf(in, -1.0f, 0.99997f) * 32768.0f);
  if (++w_ >= len_) w_ = 0;
  return dry_ * x + wet_ * delayed;
}

// ---- Reverb ----------------------------------------------------------------
void Reverb::init(float sampleRate) {
  static const int kCombBase[kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
  static const int kApBase[kAllpasses] = {556, 441, 341, 225};
  sr_ = sampleRate;
  float scale = sampleRate / 44100.0f;
  memset(pool_, 0, sizeof(pool_));
  int used = 0;
  for (int i = 0; i < kCombs; ++i) {
    combLen_[i] = (int)(kCombBase[i] * scale);
    comb_[i] = pool_ + used;
    used += combLen_[i];
    combPos_[i] = 0;
    combLp_[i] = 0.0f;
  }
  for (int i = 0; i < kAllpasses; ++i) {
    apLen_[i] = (int)(kApBase[i] * scale);
    ap_[i] = pool_ + used;
    used += apLen_[i];
    apPos_[i] = 0;
  }
  preLen_ = (int)(0.020f * sampleRate);
  if (used + preLen_ > kPoolSize) preLen_ = kPoolSize - used;  // only at very high sample rates
  if (preLen_ < 1) preLen_ = 1;
  pre_ = pool_ + used;
  prePos_ = 0;
  set(1.0f, 0.0f);
}

void Reverb::set(float timeS, float mix) {
  timeS = clampf(timeS, 0.05f, 30.0f);
  for (int i = 0; i < kCombs; ++i) combFb_[i] = powf(10.0f, -3.0f * (float)combLen_[i] / (timeS * sr_));
  // Knob 100 % = reverb as loud as the dry sound (equal-power 50/50 point).
  mix = 0.5f * clampf(mix, 0.0f, 1.0f);
  dry_ = cosf(mix * kHalfPi);
  wet_ = sinf(mix * kHalfPi);
  active_ = mix > 0.0005f;
}

float Reverb::process(float x) {
  if (!active_) return x;
  float in = pre_[prePos_];
  pre_[prePos_] = x;
  if (++prePos_ >= preLen_) prePos_ = 0;
  in *= 0.015f;
  float sum = 0.0f;
  const float damp = 0.2f;
  for (int i = 0; i < kCombs; ++i) {
    float out = comb_[i][combPos_[i]];
    combLp_[i] = out * (1.0f - damp) + combLp_[i] * damp;
    comb_[i][combPos_[i]] = in + combLp_[i] * combFb_[i];
    if (++combPos_[i] >= combLen_[i]) combPos_[i] = 0;
    sum += out;
  }
  for (int i = 0; i < kAllpasses; ++i) {
    float bufout = ap_[i][apPos_[i]];
    ap_[i][apPos_[i]] = sum + bufout * 0.5f;
    sum = bufout - sum;
    if (++apPos_[i] >= apLen_[i]) apPos_[i] = 0;
  }
  return dry_ * x + wet_ * sum * 3.0f;
}

// ---- Limiter ---------------------------------------------------------------
void Limiter::init(float sampleRate) {
  attack_ = onePoleCoef(0.0005f, sampleRate);
  release_ = onePoleCoef(0.120f, sampleRate);
  env_ = 0.0f;
  gain_ = 1.0f;
}

float Limiter::process(float x) {
  const float threshold = 0.89f;  // about -1 dBFS
  float a = fabsf(x);
  env_ += (a - env_) * (a > env_ ? attack_ : release_);
  float target = env_ > threshold ? threshold / env_ : 1.0f;
  gain_ += (target - gain_) * (target < gain_ ? attack_ : release_);
  float y = x * gain_;
  // Soft clip whatever the follower let through.
  float m = fabsf(y);
  if (m > 0.9f) {
    float over = 0.9f + 0.0999f * fastTanh((m - 0.9f) * 10.0f);
    y = y > 0.0f ? over : -over;
  }
  return y;
}

float Limiter::reductionDb() const { return -gainToDb(gain_); }

// ---- CueSynth --------------------------------------------------------------
void CueSynth::play(float freqHz, float durationS, float level) {
  Beep& b = beeps_[next_];
  next_ = (next_ + 1) % 4;
  b.phase = 0.0f;
  b.inc = freqHz / sr_;
  b.level = level;
  b.total = b.remaining = (int)(durationS * sr_);
}

void CueSynth::render(float* out, int n) {
  const int fade = (int)(0.005f * sr_);
  for (Beep& b : beeps_) {
    if (b.remaining <= 0) continue;
    for (int i = 0; i < n && b.remaining > 0; ++i) {
      int done = b.total - b.remaining;
      float env = 1.0f;
      if (done < fade) env = (float)done / (float)fade;
      if (b.remaining < fade) env = (float)b.remaining / (float)fade;
      out[i] += b.level * env * fastSin01(b.phase);
      b.phase = wrap01(b.phase + b.inc);
      --b.remaining;
    }
  }
}

// ---- PeakMeter -------------------------------------------------------------
void PeakMeter::update(const float* x, int n, float sampleRate, float fallDbPerS) {
  float peak = 0.0f;
  for (int i = 0; i < n; ++i) peak = fmaxf(peak, fabsf(x[i]));
  float db = gainToDb(peak);
  float fallen = levelDb_ - fallDbPerS * (float)n / sampleRate;
  levelDb_ = db > fallen ? db : fallen;
  if (levelDb_ < -120.0f) levelDb_ = -120.0f;
}

}  // namespace lw
