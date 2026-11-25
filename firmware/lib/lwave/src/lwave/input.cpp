#include "lwave/input.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace lw {

namespace {
// Time (µs, relative to t0) where a straight line from (t0, x0) to (t1, x1) crosses thr.
float crossingTime(float x0, float x1, uint32_t t0, uint32_t t1, float thr) {
  float span = (float)(uint32_t)(t1 - t0);
  if (x1 == x0) return span;
  float f = (thr - x0) / (x1 - x0);
  f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
  return f * span;
}
}  // namespace

// ---- Thresholds for the Velocity knob --------------------------------------
float noteOnPoint(const Settings& s, float w) {
  w = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
  return s.keyNoteOn + (s.swellNoteOn - s.keyNoteOn) * w;
}

Settings keySettingsFor(const Settings& s, float w) {
  if (w <= 0.0f) return s;
  Settings k = s;
  k.keyNoteOn = noteOnPoint(s, w);
  if (k.keyVelStart > k.keyNoteOn) k.keyVelStart = k.keyNoteOn;
  const float maxOff = k.keyNoteOn - kNoteOffGap;
  if (k.keyNoteOff > maxOff) k.keyNoteOff = maxOff > 0.01f ? maxOff : 0.01f;
  const float span = s.keyNoteOn - s.keyVelStart;
  if (span > 0.0f) {
    float ratio = (k.keyNoteOn - k.keyVelStart) / span;
    if (ratio < 0.01f) ratio = 0.01f;
    k.velFastMs *= ratio;
    k.velSlowMs *= ratio;
  }
  return k;
}

// ---- KeyProcessor ------------------------------------------------------------
void KeyProcessor::reset() {
  for (Key& k : keys_) k = Key();
}

float KeyProcessor::velocityFromMs(float ms, const Settings& s) {
  if (ms <= s.velFastMs) return 1.0f;
  if (ms >= s.velSlowMs) return 0.0f;
  return 1.0f - logf(ms / s.velFastMs) / logf(s.velSlowMs / s.velFastMs);
}

float KeyProcessor::windowMax(const Key& key, float windowMs) const {
  int n = (int)(windowMs + 0.5f) + 1;
  if (n > kHistory) n = kHistory;
  float m = key.travel;
  for (int i = 1; i <= n; ++i) {
    float v = key.hist[((int)key.histPos - i + kHistory) % kHistory];  // histPos - 1 = newest
    if (v > m) m = v;
  }
  return m;
}

bool KeyProcessor::update(int k, float travel, uint32_t tUs, const Settings& s, KeyEvent& ev) {
  Key& key = keys_[k];
  float x0 = key.havePrev ? key.prevTravel : travel;
  uint32_t t0 = key.havePrev ? key.prevT : tUs;
  key.travel = travel;

  // Depth history roughly every millisecond. Key depth is the highest travel in
  // the last depth_freeze_ms, so a key on its way up does not dip the level.
  if (!key.havePrev) {
    for (float& h : key.hist) h = travel;
  }
  if (!key.havePrev || (uint32_t)(tUs - key.histT) >= 1000u) {
    key.hist[key.histPos] = travel;
    key.histPos = (uint8_t)((key.histPos + 1) % kHistory);
    key.histT = tUs;
    key.held = windowMax(key, s.depthFreezeMs);
  } else if (travel > key.held) {
    key.held = travel;
  }

  bool fired = false;
  switch (key.state) {
    case State::Idle:
      if (travel >= s.keyVelStart) {
        key.startT = t0 + (uint32_t)crossingTime(x0, travel, t0, tUs, s.keyVelStart);
        key.state = State::Timing;
      }
      if (key.state != State::Timing) break;
      // A very fast press may pass both thresholds in one sample.
      [[fallthrough]];
    case State::Timing:
      if (travel >= s.keyNoteOn) {
        uint32_t onT = t0 + (uint32_t)crossingTime(x0, travel, t0, tUs, s.keyNoteOn);
        float ms = (float)(uint32_t)(onT - key.startT) * 0.001f;
        key.velocity = velocityFromMs(ms, s);
        key.state = State::On;
        ev = KeyEvent{true, (uint8_t)k, key.velocity, key.held, ms};
        fired = true;
      } else if (travel < s.keyVelStart - 0.02f) {
        key.state = State::Idle;
      }
      break;
    case State::On:
      if (travel < s.keyNoteOff) {
        key.state = State::Idle;
        ev = KeyEvent{false, (uint8_t)k, 0.0f, key.held, 0.0f};
        fired = true;
      }
      break;
  }
  key.prevTravel = travel;
  key.prevT = tUs;
  key.havePrev = true;
  return fired;
}

// ---- PotFilter -------------------------------------------------------------
float PotFilter::update(float x, float dtS, const Settings& s) {
  if (!init_) { init_ = true; smooth_ = out_ = x; return out_; }
  float tau = s.potSmoothMs * 0.001f;
  float a = tau > 0.0f ? dtS / (tau + dtS) : 1.0f;
  smooth_ += (x - smooth_) * a;
  float db = s.potDeadband;
  if (smooth_ > out_ + db) out_ = smooth_ - db;
  else if (smooth_ < out_ - db) out_ = smooth_ + db;
  // Let the ends be reached exactly.
  if (smooth_ <= db) out_ = 0.0f;
  if (smooth_ >= 1.0f - db) out_ = 1.0f;
  return out_;
}

// ---- SwitchDecoder ---------------------------------------------------------
int SwitchDecoder::update(int sw, uint16_t raw, uint32_t tUs, const CalibrationData& cal, const Settings& s) {
  int p = cal.switchPosition(sw, raw);
  if (!init_) { init_ = true; pos_ = candidate_ = p; since_ = tUs; return pos_; }
  if (p != candidate_) { candidate_ = p; since_ = tUs; }
  else if (candidate_ != pos_ && (float)(uint32_t)(tUs - since_) >= s.switchDebounceMs * 1000.0f) pos_ = candidate_;
  return pos_;
}

// ---- BatteryMonitor --------------------------------------------------------
void BatteryMonitor::update(uint16_t raw, float dtS, const Settings& s) {
  float v = (float)raw / (float)kAdcMax * 3.3f * s.battDivider;
  if (!init_) {
    init_ = true;
    volts_ = v;
    age_ = 0.0f;
  } else {
    age_ += dtS;
    const float tau = age_ < kBatteryFastS ? 0.02f : 2.0f;  // follow quickly while the divider settles
    volts_ += (v - volts_) * (dtS / (tau + dtS));
  }
  if (age_ < kBatteryWarnDelayS) {  // no warning until the reading has settled
    low_ = crit_ = false;
    return;
  }
  const float hyst = 0.05f;
  if (low_) { if (volts_ > s.battLowV + hyst) low_ = false; }
  else if (volts_ < s.battLowV) low_ = true;
  if (crit_) { if (volts_ > s.battCritV + hyst) crit_ = false; }
  else if (volts_ < s.battCritV) crit_ = true;
}

// ---- Boot detection --------------------------------------------------------
void detectHeldKeys(const uint16_t raw[kNumKeys], const CalibrationData& cal, bool held[kNumKeys]) {
  uint16_t sorted[kNumKeys];
  memcpy(sorted, raw, sizeof(sorted));
  // insertion sort: 48 values, runs once at boot
  for (int i = 1; i < kNumKeys; ++i) {
    uint16_t v = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; --j; }
    sorted[j + 1] = v;
  }
  int median = (sorted[kNumKeys / 2 - 1] + sorted[kNumKeys / 2]) / 2;
  for (int k = 0; k < kNumKeys; ++k) {
    if (cal.keyValid[k]) held[k] = cal.keyTravel(k, raw[k]) > 0.5f;
    else held[k] = abs((int)raw[k] - median) > 600;
  }
}

}  // namespace lw
