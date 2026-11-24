#include "lwave/calibration_data.h"
#include "lwave/hw_map.h"
#include <stdlib.h>
#include <string.h>

namespace lw {

void CalibrationData::setDefaults() {
  memset(this, 0, sizeof(*this));
  // DRV5056 keys on the first build: about 950-1045 counts at rest (0.8 V),
  // 1950-2230 fully down. Rest is set just above the highest rest reading so
  // released keys read 0. Only used until the keys are calibrated.
  for (int k = 0; k < kNumKeys; ++k) {
    keyRest[k] = 1050;
    keyBottom[k] = 2100;
  }
  for (int p = 0; p < kNumPots; ++p) {
    potMin[p] = 20;
    potMax[p] = kAdcMax - 20;
  }
  for (int s = 0; s < kNumSwitches; ++s)
    for (int i = 0; i < kSwitchPositions; ++i)
      switchLevel[s][i] = (uint16_t)((kAdcMax * (kSwitchPositions - 1 - i)) / (kSwitchPositions - 1));
}

float CalibrationData::keyTravel(int key, uint16_t raw) const {
  float span = (float)keyBottom[key] - (float)keyRest[key];
  if (span > -1.0f && span < 1.0f) return 0.0f;
  float t = ((float)raw - (float)keyRest[key]) / span;
  return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

float CalibrationData::potPosition(int pot, uint16_t raw) const {
  float span = (float)potMax[pot] - (float)potMin[pot];
  if (span < 1.0f) return 0.0f;
  float t = ((float)raw - (float)potMin[pot]) / span;
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return kPotReversed[pot] ? 1.0f - t : t;
}

int CalibrationData::switchPosition(int sw, uint16_t raw) const {
  int best = 0, bestDist = 1 << 30;
  for (int i = 0; i < kSwitchPositions; ++i) {
    int d = abs((int)raw - (int)switchLevel[sw][i]);
    if (d < bestDist) { bestDist = d; best = i; }
  }
  return best;
}

bool CalibrationData::complete() const {
  int k, p, s;
  counts(k, p, s);
  return k == kNumKeys && p == kNumPots && s == kNumSwitches;
}

void CalibrationData::counts(int& keys, int& pots, int& switches) const {
  keys = pots = switches = 0;
  for (int i = 0; i < kNumKeys; ++i) keys += keyValid[i] ? 1 : 0;
  for (int i = 0; i < kNumPots; ++i) pots += potValid[i] ? 1 : 0;
  for (int i = 0; i < kNumSwitches; ++i) switches += switchValid[i] ? 1 : 0;
}

}  // namespace lw
