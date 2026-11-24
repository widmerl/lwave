#include "lwave/control_mapper.h"
#include <math.h>
#include <stdio.h>
#include "lwave/curves.h"
#include "lwave/hw_map.h"

namespace lw {

namespace {
// Fitted once at start-up (global constructors run before setup()).
const LogTo0 kDetuneCurve = LogTo0::fit(0.7f, 1.0f / 3.0f, 1.0f);
const LogTo0& detCurve() { return kDetuneCurve; }
float pot(const PanelState& p, Pot which) { return p.pot[static_cast<int>(which)]; }
float level(const PanelState& p, Pot which) { return curveDbTo0(pot(p, which), -24.0f, 12.0f, 0.05f); }
}  // namespace

float curveLogFull(float p, float lo, float hi) { return lo * powf(hi / lo, clampf(p, 0.0f, 1.0f)); }

// Equal ratio per degree of rotation: 10, 20, 50, 100, 200, 500 ms, 1, 2, 5, 10 s.
float timeKnobCurve(float p) { return curveLogFull(p, kTimeKnobMinS, kTimeKnobMaxS); }
float detuneCurve(float p) { return detCurve()(p); }

float potEngineValue(const EngineParams& e, Pot pot) {
  switch (pot) {
    case Pot::LfoPeriod: return e.lfoPeriod;
    case Pot::LfoDelay: return e.lfoDelay;
    case Pot::LfoStrength: return e.lfoStrength;
    case Pot::Velocity: return e.velocityKnob;
    case Pot::Attack: return e.attack;
    case Pot::Decay: return e.decay;
    case Pot::Sustain: return e.sustain;
    case Pot::Release: return e.release;
    case Pot::ReverbTime: return e.reverbTime;
    case Pot::ReverbMix: return e.reverbMix;
    case Pot::DelayTime: return e.delayTime;
    case Pot::DelayFeedback: return e.delayFeedback;
    case Pot::DelayMix: return e.delayMix;
    case Pot::FilterFreq: return e.filterFreq;
    case Pot::FilterEnv: return e.filterEnv;
    case Pot::FilterTrack: return e.filterTrack;
    case Pot::FilterRes: return e.filterQ;
    case Pot::Overdrive: return e.drive;
    case Pot::Tune: return e.tuneCents;
    case Pot::Crush: return e.crush;
    case Pot::Pwm: return e.pwm;
    case Pot::Detune:
      if (e.detuneCopies == 1) return e.detuneKnob;
      return e.detuneAmount * (e.detuneCopies == 2 ? e.detuneUpperC : e.detuneHighC);
    case Pot::Volume: return gainToDb(e.volume);
    case Pot::LevelPink: return gainToDb(e.levelPink);
    case Pot::LevelSquare: return gainToDb(e.levelSquare);
    case Pot::LevelSaw: return gainToDb(e.levelSaw);
    case Pot::LevelTri: return gainToDb(e.levelTri);
    case Pot::LevelSine: return gainToDb(e.levelSine);
    case Pot::LevelSub1: return gainToDb(e.levelSub1);
    case Pot::LevelSub2: return gainToDb(e.levelSub2);
    default: return 0.0f;
  }
}

void formatPotValue(const EngineParams& e, Pot pot, char* buf, int size) {
  float v = potEngineValue(e, pot);
  switch (pot) {
    case Pot::LfoPeriod: case Pot::LfoDelay: case Pot::Attack: case Pot::Decay: case Pot::Release:
    case Pot::ReverbTime: case Pot::DelayTime:
      if (v < 1.0f) snprintf(buf, (size_t)size, "%.1f ms", v * 1000.0f);
      else snprintf(buf, (size_t)size, "%.2f s", v);
      break;
    case Pot::FilterFreq:
      if (v < 1000.0f) snprintf(buf, (size_t)size, "%.0f Hz", v);
      else snprintf(buf, (size_t)size, "%.2f kHz", v / 1000.0f);
      break;
    case Pot::FilterRes: snprintf(buf, (size_t)size, "Q %.2f", v); break;
    case Pot::Tune: snprintf(buf, (size_t)size, "%+.1f c", v); break;
    case Pot::Detune:
      if (e.detuneCopies == 1) snprintf(buf, (size_t)size, "chorus %.0f %%", v * 100.0f);
      else if (e.detuneCopies == 2) snprintf(buf, (size_t)size, "+%.1f c", v);
      else snprintf(buf, (size_t)size, "-%.1f / +%.1f c", e.detuneAmount * e.detuneLowC, v);
      break;
    case Pot::Volume: case Pot::LevelPink: case Pot::LevelSquare: case Pot::LevelSaw: case Pot::LevelTri:
    case Pot::LevelSine: case Pot::LevelSub1: case Pot::LevelSub2:
      if (v <= -119.0f) snprintf(buf, (size_t)size, "-inf dB");
      else snprintf(buf, (size_t)size, "%+.1f dB", v);
      break;
    default: snprintf(buf, (size_t)size, "%.0f %%", v * 100.0f); break;
  }
}

int octaveShiftSemitones(int octavePos) { return 12 * (octavePos + 1 - 4); }

void mapControls(const PanelState& panel, const Settings& s, float maxDelayS, EngineParams& o) {
  o.lfoWave = static_cast<LfoWave>(panel.sw[static_cast<int>(Switch::LfoWave)] & 7);
  o.lfoDest = static_cast<LfoDest>(panel.sw[static_cast<int>(Switch::LfoDest)] & 7);
  o.filterType = static_cast<FilterType>(panel.sw[static_cast<int>(Switch::FilterType)] & 7);

  o.lfoPeriod = timeKnobCurve(pot(panel, Pot::LfoPeriod));
  o.lfoDelay = timeKnobCurve(pot(panel, Pot::LfoDelay));
  o.lfoStrength = pot(panel, Pot::LfoStrength);
  o.velocityKnob = pot(panel, Pot::Velocity);

  o.attack = timeKnobCurve(pot(panel, Pot::Attack));
  o.decay = timeKnobCurve(pot(panel, Pot::Decay));
  o.sustain = pot(panel, Pot::Sustain);
  o.release = timeKnobCurve(pot(panel, Pot::Release));

  o.reverbTime = curveLog3(pot(panel, Pot::ReverbTime), 0.05f, 1.0f, 20.0f);
  o.reverbMix = pot(panel, Pot::ReverbMix);

  // Delay time 10 ms - 10 s, fully logarithmic; without PSRAM the buffer is
  // shorter, so the knob ends at the longest possible time instead.
  float hiD = maxDelayS < kTimeKnobMaxS ? maxDelayS : kTimeKnobMaxS;
  if (hiD < 2.0f * kTimeKnobMinS) hiD = 2.0f * kTimeKnobMinS;
  o.delayTime = curveLogFull(pot(panel, Pot::DelayTime), kTimeKnobMinS, hiD);
  o.delayFeedback = 0.9f * pot(panel, Pot::DelayFeedback);
  o.delayMix = pot(panel, Pot::DelayMix);

  o.filterFreq = curveLog3(pot(panel, Pot::FilterFreq), 20.0f, 640.0f, 20000.0f);
  o.filterEnv = pot(panel, Pot::FilterEnv);
  o.filterTrack = pot(panel, Pot::FilterTrack);
  o.filterQ = curveLog3(pot(panel, Pot::FilterRes), 0.05f, 1.0f, 20.0f);

  o.drive = pot(panel, Pot::Overdrive);
  o.tuneCents = curveLinear(pot(panel, Pot::Tune), -50.0f, 50.0f);
  o.crush = pot(panel, Pot::Crush);
  o.pwm = pot(panel, Pot::Pwm);
  o.detuneAmount = detuneCurve(pot(panel, Pot::Detune));
  o.detuneKnob = pot(panel, Pot::Detune);

  o.volume = level(panel, Pot::Volume);
  o.levelPink = level(panel, Pot::LevelPink);
  o.levelSquare = level(panel, Pot::LevelSquare);
  o.levelSaw = level(panel, Pot::LevelSaw);
  o.levelTri = level(panel, Pot::LevelTri);
  o.levelSine = level(panel, Pot::LevelSine);
  o.levelSub1 = level(panel, Pot::LevelSub1);
  o.levelSub2 = level(panel, Pot::LevelSub2);

  o.voices = (int)(s.voices + 0.5f);
  o.velRangeDb = s.velRangeDb;
  o.velAttackSpan = s.velAttackSpan;
  o.depthMode = (int)(s.depthMode + 0.5f);
  o.filterEnvOct = s.filterEnvOct;
  o.meterFallDbS = s.meterFallDbS;
  int copies = (int)(s.detuneCopies + 0.5f);
  o.detuneCopies = copies < 1 ? 1 : (copies > 3 ? 3 : copies);
  o.detuneSideGain = dbToGain(s.detuneSidesDb);
  o.detuneLowC = s.detuneLowC;
  o.detuneHighC = s.detuneHighC;
  o.detuneUpperC = s.detuneUpperC;
  o.detuneUpperGain = dbToGain(s.detuneUpperDb);
  o.oscRandomPhase = s.oscRandomPhase >= 0.5f;
}

}  // namespace lw
