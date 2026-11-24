#include "lwave/calibrator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lwave/dsp/basics.h"
#include "lwave/hw_map.h"

namespace lw {

namespace {
constexpr uint32_t kSettleUs = 500000;      // after release, before measuring rest
constexpr uint32_t kRestUs = 2000000;       // rest measurement time
constexpr uint32_t kFinishHoldUs = 3000000; // hold C4 this long to finish early
// Nominal ladder reading of switch position i (0 = position 1 = 3.3 V).
int nominalSwitchLevel(int i) { return (kAdcMax * (kSwitchPositions - 1 - i)) / (kSwitchPositions - 1); }

// Appends text to buf (size bytes) at *n, never overflowing.
void put(char* buf, int size, int* n, const char* text) {
  while (*text && *n < size - 1) buf[(*n)++] = *text++;
  buf[*n] = 0;
}
void putNum(char* buf, int size, int* n, int v) {
  char tmp[12];
  int len = 0;
  if (v < 0) { put(buf, size, n, "-"); v = -v; }
  do { tmp[len++] = (char)('0' + v % 10); v /= 10; } while (v && len < 11);
  char out[12];
  for (int i = 0; i < len; ++i) out[i] = tmp[len - 1 - i];
  out[len] = 0;
  put(buf, size, n, out);
}
}  // namespace

void Calibrator::start(const CalibrationData& previous, uint32_t tUs) {
  *this = Calibrator();
  result_ = previous;
  phase_ = Phase::WaitRelease;
  phaseStart_ = tUs;
  calKey_ = keyIndexByName(kCalibrationKey);
  // Rising three-note chime: calibration has started.
  pushCue(523.25f, 0.12f, 0.0f);
  pushCue(659.25f, 0.12f, 0.15f);
  pushCue(783.99f, 0.20f, 0.30f);
}

void Calibrator::pushCue(float hz, float s, float delayS) {
  if (cueCount_ < 16) cues_[cueCount_++] = Cue{hz, s, delayS};
}

int Calibrator::drainCues(Cue* out, int max) {
  int n = cueCount_ < max ? cueCount_ : max;
  for (int i = 0; i < n; ++i) out[i] = cues_[i];
  for (int i = n; i < cueCount_; ++i) cues_[i - n] = cues_[i];
  cueCount_ -= n;
  return n;
}

void Calibrator::update(const ScanFrame& f) {
  const uint32_t t = f.timeUs;
  switch (phase_) {
    case Phase::WaitRelease: {
      bool held[kNumKeys];
      CalibrationData blank;
      blank.setDefaults();  // all keys invalid → median comparison
      detectHeldKeys(f.keys, blank, held);
      bool any = false;
      for (int k = 0; k < kNumKeys; ++k) { held_[k] = held[k]; any = any || held[k]; }
      if (any) phaseStart_ = t;
      else if ((uint32_t)(t - phaseStart_) > kSettleUs) {
        phase_ = Phase::Rest;
        phaseStart_ = t;
        for (int k = 0; k < kNumKeys; ++k) { restMin_[k] = 0xFFFF; restMax_[k] = 0; restSum_[k] = 0; }
        restCount_ = 0;
      }
      break;
    }
    case Phase::Rest: {
      for (int k = 0; k < kNumKeys; ++k) {
        restSum_[k] += f.keys[k];
        if (f.keys[k] < restMin_[k]) restMin_[k] = f.keys[k];
        if (f.keys[k] > restMax_[k]) restMax_[k] = f.keys[k];
      }
      ++restCount_;
      if ((uint32_t)(t - phaseStart_) >= kRestUs && restCount_ > 0) {
        for (int k = 0; k < kNumKeys; ++k) {
          rest_[k] = (uint16_t)(restSum_[k] / restCount_);
          keyExtreme_[k] = rest_[k];
        }
        for (int p = 0; p < kNumPots; ++p) potMin_[p] = potMax_[p] = f.pots[p];
        for (int s = 0; s < kNumSwitches; ++s) {
          switchAnchor_[s] = f.switches[s];
          switchSince_[s] = t;
          switchSum_[s] = switchN_[s] = 0;
          switchChecked_[s] = false;
        }
        phase_ = Phase::Sweep;
        phaseStart_ = t;
        pushCue(659.25f, 0.12f, 0.0f);  // second chime: start sweeping
        pushCue(783.99f, 0.20f, 0.15f);
      }
      break;
    }
    case Phase::Sweep: {
      // Keys: remember the reading farthest from rest; done once the span is
      // large enough and the key has come back up.
      for (int k = 0; k < kNumKeys; ++k) {
        int dev = abs((int)f.keys[k] - (int)rest_[k]);
        int ext = abs((int)keyExtreme_[k] - (int)rest_[k]);
        if (dev > ext) { keyExtreme_[k] = f.keys[k]; ext = dev; }
        if (!keyDone_[k] && ext >= kMinKeySpan && dev * 5 < ext) {
          keyDone_[k] = true;
          pushCue(midiToHz((float)kKeys[k].note), 0.08f, 0.0f);  // tick at the key's own pitch
        }
      }
      // Pots: track both ends; done when both rails were approached.
      for (int p = 0; p < kNumPots; ++p) {
        if (f.pots[p] < potMin_[p]) potMin_[p] = f.pots[p];
        if (f.pots[p] > potMax_[p]) potMax_[p] = f.pots[p];
        if (!potDone_[p] && potMin_[p] < kPotRailMargin && potMax_[p] > kAdcMax - kPotRailMargin) {
          potDone_[p] = true;
          pushCue(1760.0f, 0.05f, 0.0f);
        }
      }
      // Switches: a reading that stays steady for kSwitchStableUs near one of the
      // 8 nominal ladder voltages records that position (averaged over the stretch).
      for (int s = 0; s < kNumSwitches; ++s) {
        const uint16_t v = f.switches[s];
        if (abs((int)v - (int)switchAnchor_[s]) > kSwitchJitter) {
          switchAnchor_[s] = v;
          switchSince_[s] = t;
          switchSum_[s] = switchN_[s] = 0;
          switchChecked_[s] = false;
        }
        switchSum_[s] += v;
        ++switchN_[s];
        if (switchDone_[s] || switchChecked_[s] || (uint32_t)(t - switchSince_[s]) < kSwitchStableUs) continue;
        switchChecked_[s] = true;
        const int avg = (int)(switchSum_[s] / switchN_[s]);
        const float step = (float)kAdcMax / (float)(kSwitchPositions - 1);
        int i = (int)((float)(kAdcMax - avg) / step + 0.5f);
        i = i < 0 ? 0 : (i >= kSwitchPositions ? kSwitchPositions - 1 : i);
        if (abs(avg - nominalSwitchLevel(i)) > kSwitchNominalTolerance || switchSeen_[s][i]) continue;
        switchSeen_[s][i] = true;
        switchLevels_[s][i] = (uint16_t)avg;
        ++switchCount_[s];
        pushCue(880.0f * exp2f((float)(2 * i) / 12.0f), 0.06f, 0.0f);  // one tick per new position
        if (switchCount_[s] == kSwitchPositions) {
          switchDone_[s] = true;
          pushCue(1318.5f, 0.08f, 0.1f);
          pushCue(1568.0f, 0.08f, 0.2f);
        }
      }
      // Finish: everything done, or C4 held for 3 s.
      bool held = calKey_ >= 0 && abs((int)f.keys[calKey_] - (int)rest_[calKey_]) > kMinKeySpan;
      if (held && !calKeyHeld_) calKeyHeldSince_ = t;
      calKeyHeld_ = held;
      if (progress() >= 1.0f || (held && (uint32_t)(t - calKeyHeldSince_) >= kFinishHoldUs)) finish();
      break;
    }
    case Phase::Done:
      break;
  }
}

void Calibrator::finish() {
  for (int k = 0; k < kNumKeys; ++k)
    if (keyDone_[k]) {
      result_.keyRest[k] = rest_[k];
      result_.keyBottom[k] = keyExtreme_[k];
      result_.keyValid[k] = 1;
    }
  for (int p = 0; p < kNumPots; ++p)
    if (potDone_[p]) {
      result_.potMin[p] = potMin_[p];
      result_.potMax[p] = potMax_[p];
      result_.potValid[p] = 1;
    }
  for (int s = 0; s < kNumSwitches; ++s)
    if (switchDone_[s]) {
      // Already indexed by position (position 1 = highest voltage).
      memcpy(result_.switchLevel[s], switchLevels_[s], sizeof(result_.switchLevel[s]));
      result_.switchValid[s] = 1;
    }
  phase_ = Phase::Done;
  // Major chord: finished.
  pushCue(523.25f, 0.5f, 0.0f);
  pushCue(659.25f, 0.5f, 0.0f);
  pushCue(783.99f, 0.5f, 0.0f);
}

void Calibrator::describeMissing(char* buf, int size) const {
  int n = 0;
  buf[0] = 0;
  int missing = kNumKeys - keysDone(), listed = 0;
  if (missing > 0) {
    put(buf, size, &n, "keys");
    for (int k = 0; k < kNumKeys && listed < 12; ++k)
      if (!keyDone_[k]) { put(buf, size, &n, " "); put(buf, size, &n, kKeys[k].name); ++listed; }
    if (missing > listed) { put(buf, size, &n, " +"); putNum(buf, size, &n, missing - listed); put(buf, size, &n, " more"); }
  }
  bool first = true;
  int potsListed = 0, potsMissing = kNumPots - potsDone();
  for (int p = 0; p < kNumPots && potsListed < 8; ++p) {
    if (potDone_[p]) continue;
    ++potsListed;
    put(buf, size, &n, first ? (n ? "; pots " : "pots ") : ", ");
    first = false;
    put(buf, size, &n, kPotNames[p]);
    put(buf, size, &n, " (");
    putNum(buf, size, &n, potMin_[p]);
    put(buf, size, &n, "-");
    putNum(buf, size, &n, potMax_[p]);
    put(buf, size, &n, ")");
  }
  if (potsMissing > potsListed) { put(buf, size, &n, " +"); putNum(buf, size, &n, potsMissing - potsListed); put(buf, size, &n, " more"); }
  first = true;
  for (int s = 0; s < kNumSwitches; ++s) {
    if (switchDone_[s]) continue;
    put(buf, size, &n, first ? (n ? "; switches " : "switches ") : ", ");
    first = false;
    put(buf, size, &n, kSwitchNames[s]);
    put(buf, size, &n, " missing");
    for (int i = 0; i < kSwitchPositions; ++i)
      if (!switchSeen_[s][i]) { put(buf, size, &n, " "); putNum(buf, size, &n, i + 1); }
  }
  if (n == 0) put(buf, size, &n, "nothing");
}

void Calibrator::describeHeld(char* buf, int size) const {
  int n = 0;
  buf[0] = 0;
  for (int k = 0; k < kNumKeys; ++k)
    if (held_[k]) { put(buf, size, &n, n ? " " : ""); put(buf, size, &n, kKeys[k].name); }
  if (n == 0) put(buf, size, &n, "none");
}

int Calibrator::keysDone() const {
  int n = 0;
  for (bool d : keyDone_) n += d ? 1 : 0;
  return n;
}
int Calibrator::potsDone() const {
  int n = 0;
  for (bool d : potDone_) n += d ? 1 : 0;
  return n;
}
int Calibrator::switchesDone() const {
  int n = 0;
  for (bool d : switchDone_) n += d ? 1 : 0;
  return n;
}
float Calibrator::progress() const {
  return (float)(keysDone() + potsDone() + switchesDone()) / (float)(kNumKeys + kNumPots + kNumSwitches);
}

}  // namespace lw
