#include "lwave/calibrator.h"
#include <stdlib.h>
#include <string.h>
#include "lwave/dsp/basics.h"
#include "lwave/hw_map.h"

namespace lw {

namespace {
constexpr uint32_t kSettleUs = 500000;      // after release, before measuring rest
constexpr uint32_t kRestUs = 2000000;       // rest measurement time
constexpr uint32_t kFinishHoldUs = 3000000; // hold C4 this long to finish early
constexpr int kSwitchMinGap = 150;          // counts between two distinct switch positions
constexpr int kSwitchStableFrames = 40;     // ~20 ms at 2 kHz scanning
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
      for (bool h : held) any = any || h;
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
        for (int s = 0; s < kNumSwitches; ++s) switchLast_[s] = f.switches[s];
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
      // Switches: collect stable, distinct voltages; done at 8.
      for (int s = 0; s < kNumSwitches; ++s) {
        uint16_t v = f.switches[s];
        if (abs((int)v - (int)switchLast_[s]) < 20) ++switchStable_[s];
        else switchStable_[s] = 0;
        switchLast_[s] = v;
        if (switchDone_[s] || switchStable_[s] != kSwitchStableFrames) continue;
        bool known = false;
        for (int i = 0; i < switchCount_[s]; ++i)
          if (abs((int)v - (int)switchLevels_[s][i]) < kSwitchMinGap) known = true;
        if (!known && switchCount_[s] < kSwitchPositions) {
          switchLevels_[s][switchCount_[s]++] = v;
          if (switchCount_[s] == kSwitchPositions) {
            switchDone_[s] = true;
            pushCue(1318.5f, 0.08f, 0.0f);
            pushCue(1568.0f, 0.08f, 0.1f);
          }
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
      // Position 1 is the highest voltage: sort descending.
      uint16_t* lv = switchLevels_[s];
      for (int i = 1; i < kSwitchPositions; ++i) {
        uint16_t v = lv[i];
        int j = i - 1;
        while (j >= 0 && lv[j] < v) { lv[j + 1] = lv[j]; --j; }
        lv[j + 1] = v;
      }
      memcpy(result_.switchLevel[s], lv, sizeof(result_.switchLevel[s]));
      result_.switchValid[s] = 1;
    }
  phase_ = Phase::Done;
  // Major chord: finished.
  pushCue(523.25f, 0.5f, 0.0f);
  pushCue(659.25f, 0.5f, 0.0f);
  pushCue(783.99f, 0.5f, 0.0f);
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
