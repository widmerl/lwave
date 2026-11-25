#include "lwave/voice.h"
#include <math.h>
#include "lwave/curves.h"
#include "lwave/dsp/basics.h"
#include "lwave/dsp/oscillators.h"

namespace lw {

VelocityKnobWeights velocityKnobWeights(float k) {
  k = clampf(k, 0.0f, 1.0f);
  VelocityKnobWeights w;
  w.speedToAttack = clampf(k * 3.0f, 0.0f, 1.0f);           // 0 → 33 %: rises to full
  w.speedToLevel = clampf(2.0f - k * 3.0f, 0.0f, 1.0f);     // 33 → 66 %: falls to none
  w.depthIsLevel = clampf(k * 3.0f - 2.0f, 0.0f, 1.0f);     // 66 → 100 %: rises to full
  return w;
}

namespace {
// A key counts as arrived at the bottom when it is this deep, or when it has not
// gone deeper (by kRiseStep) for kArriveS.
constexpr float kArriveDepth = 0.95f, kRiseStep = 0.02f, kArriveS = 0.05f;

// Start phases for osc_random_phase (audio thread only).
uint32_t gPhaseSeed = 0x9E3779B9u;
float randomPhase() {
  gPhaseSeed ^= gPhaseSeed << 13;
  gPhaseSeed ^= gPhaseSeed >> 17;
  gPhaseSeed ^= gPhaseSeed << 5;
  return (float)(gPhaseSeed >> 8) * (1.0f / 16777216.0f);
}
}  // namespace

void Voice::begin(uint8_t key, float note, float velocity, float depth, uint32_t age, const Switches& sw) {
  active_ = true;
  released_ = false;
  fading_ = false;
  fadeGain_ = 1.0f;
  key_ = key;
  note_ = note;
  velocity_ = clampf(velocity, 0.0f, 1.0f);
  depth_ = depthGain_ = maxDepth_ = clampf(depth, 0.0f, 1.0f);
  arrived_ = false;
  sinceRise_ = 0.0f;
  swellNow_ = 0.0f;
  age_ = age;
  filterType_ = sw.filter;
  lfoWave_ = sw.wave;
  lfoDest_ = sw.dest;
  env_.reset();
  env_.noteOn();
  filter_.reset();
  filter_.setType(filterType_);
  lfo_.trigger();
  lfoSmooth_ = 0.0f;
  volModNow_ = 1.0f;
  for (float& ph : phase_) ph = sw.randomPhase ? randomPhase() : 0.0f;
}

void Voice::start(uint8_t key, float note, float velocity, float depth, uint32_t age, const EngineParams& p) {
  hasPending_ = false;
  begin(key, note, velocity, depth, age, Switches{p.filterType, p.lfoWave, p.lfoDest, p.oscRandomPhase});
  depthGain_ = depthLevel(p);  // no glide from a wrong start value
}

float Voice::swellTarget(const EngineParams& p) const {
  // Silent at the note-on point, full at the bottom.
  const float start = p.swellStart < 0.95f ? p.swellStart : 0.95f;
  const float rel = clampf((depth_ - start) / (1.0f - start), 0.0f, 1.0f);
  return powf(rel, p.swellCurve);
}

float Voice::depthLevel(const EngineParams& p) const {
  const float normal = p.depthMode == 1 ? depth_ : 1.0f;
  const float swellW = velocityKnobWeights(p.velocityKnob).depthIsLevel;
  if (swellW <= 0.0f) return normal;
  // swellNow_ is the depth-driven level after the Attack knob's rise limit.
  return normal + (swellNow_ - normal) * swellW;
}

void Voice::steal(uint8_t key, float note, float velocity, float depth, uint32_t age, const EngineParams& p,
                  float sampleRate) {
  if (!active_) { start(key, note, velocity, depth, age, p); return; }
  pending_ = Pending{key, note, velocity, depth, age, false, depth,
                     Switches{p.filterType, p.lfoWave, p.lfoDest, p.oscRandomPhase}};
  hasPending_ = true;
  fading_ = true;
  fadeStep_ = 1.0f / (0.003f * sampleRate);
  // Report the new owner right away so a quick note-off still finds it.
  // note_ keeps the old pitch until the fade-out ends.
  key_ = key;
  age_ = age;
  released_ = false;
}

void Voice::release(float frozenDepth) {
  if (fading_ && hasPending_) {
    pending_.released = true;
    pending_.frozen = clampf(frozenDepth, 0.0f, 1.0f);
    released_ = true;
    return;
  }
  released_ = true;
  depth_ = clampf(frozenDepth, 0.0f, 1.0f);
  env_.noteOff();
}

void Voice::copyPhases(const Voice& other) {
  for (int i = 0; i < kNumOsc; ++i) phase_[i] = other.phase_[i];
}

void Voice::fadeOut(float sampleRate) {
  if (!active_ || fadingOut()) return;
  hasPending_ = false;
  released_ = true;
  fading_ = true;
  fadeStep_ = 1.0f / (0.003f * sampleRate);
}

void Voice::kill() {
  active_ = released_ = fading_ = hasPending_ = false;
  env_.reset();
}

void Voice::prepareBlock(const EngineParams& p, float sampleRate) {
  // Velocity knob: strike speed scales the attack time (span^±1 from 33 %);
  // towards 100 % the envelope attack fades out and key depth takes over, with
  // the Attack knob limiting how fast the depth-driven level may rise (render()).
  const VelocityKnobWeights w = velocityKnobWeights(p.velocityKnob);
  float attack = p.attack * powf(p.velAttackSpan, w.speedToAttack * (1.0f - 2.0f * velocity_));
  attack *= 1.0f - w.depthIsLevel;
  env_.setTimes(attack, p.decay, p.release, sampleRate);
}

void Voice::render(const EngineParams& p, const float* noise, float* out, int n, float sampleRate) {
  if (!active_) return;
  const float dtSub = (float)kSubBlock / sampleRate;
  const VelocityKnobWeights knobW = velocityKnobWeights(p.velocityKnob);
  const float velGain = dbToGain(-p.velRangeDb * (1.0f - velocity_) * knobW.speedToLevel);
  const float nyquistGuard = 0.45f;
  const float depthCoef = onePoleCoef(0.004f, sampleRate);  // key depth glides over ~4 ms

  for (int s = 0; s < n; s += kSubBlock) {
    // ---- control rate -----------------------------------------------------
    float lfoRaw = lfo_.advance(dtSub, lfoWave_, p.lfoPeriod, p.lfoDelay);
    lfoSmooth_ += (lfoRaw - lfoSmooth_) * 0.5f;  // ~1 ms smoothing so square edges don't click
    const float l = lfoSmooth_, st = p.lfoStrength;

    float pitchSemis = p.tuneCents * 0.01f;
    float volModTarget = 1.0f;
    float pwm = p.pwm;
    float detune = p.detuneAmount;  // 0..1 of the full-knob spread
    float filterOct = 0.0f;
    const float pitchMod = clampf(st * st * 12.0f * l, -kMaxLfoSemitones, kMaxLfoSemitones);
    const float volMod = clampf(1.0f + st * l, 0.0f, kMaxLfoGain);
    switch (lfoDest_) {
      case LfoDest::Pitch: pitchSemis += pitchMod; break;
      case LfoDest::Volume: volModTarget = volMod; break;
      case LfoDest::VolumePitch:  // high volume coincides with low pitch
        volModTarget = volMod;
        pitchSemis -= pitchMod;
        break;
      case LfoDest::Pwm: pwm = clampf(pwm + 0.45f * st * l, 0.0f, 1.0f); break;
      case LfoDest::Detune: detune = clampf(detune * (1.0f + st * l), 0.0f, kMaxLfoDetune); break;
      case LfoDest::FilterFreq: filterOct = clampf(4.0f * st * l, -12.0f, 12.0f); break;
      default: break;  // Bitcrush and Delay act on the shared chain
    }

    // Key depth: live while held, frozen after release; smoothed per sample below.
    if (!released_ && !fading_) depth_ = p.keyDepth[key_];
    if (p.depthMode == 2) env_.setSustain(p.sustain * depth_);
    else env_.setSustain(p.sustain);
    // Depth-driven mode (Velocity knob towards 100 %): the level follows the key
    // down at once, but rises no faster than 0 to full in the Attack knob's time.
    // After release it no longer rises.
    bool caughtUp = true;
    if (knobW.depthIsLevel > 0.0f) {
      const float swellT = swellTarget(p);
      if (swellT <= swellNow_) swellNow_ = swellT;
      else if (!released_) swellNow_ = fminf(swellT, swellNow_ + dtSub / fmaxf(p.attack, 1e-4f));
      caughtUp = swellNow_ >= swellT - 1e-4f;
    }
    const float depthTarget = depthLevel(p);
    // The decay waits until the key stops going deeper and the level has caught up.
    if (!arrived_ && !released_) {
      sinceRise_ += dtSub;
      if (depth_ > maxDepth_ + kRiseStep) { maxDepth_ = depth_; sinceRise_ = 0.0f; }
      if ((depth_ >= kArriveDepth || sinceRise_ >= kArriveS) && caughtUp) arrived_ = true;
    }
    env_.setHold(knobW.depthIsLevel > 0.0f && !arrived_ && !released_);

    const float f0 = midiToHz(note_ + pitchSemis);
    const float dt0 = f0 / sampleRate;
    const float dtSub1 = dt0 * 0.5f, dtSub2 = dt0 * 0.25f;

    // Detuned copies (setting detune_copies). 3: centre plus a low and a high
    // copy, detune_low_c below and detune_high_c above at full knob (23.17 and
    // 37.5 c: golden ratio, so the three beat rates never line up), the sides at
    // detune_sides_db. 2: main plus one copy detune_upper_c higher (75 c) at
    // detune_upper_db. 1: main only (the detune knob drives the shared chorus
    // instead). The detune knob scales the offsets. The copy amplitudes add up
    // to 1, so every mode has the same level with the detune knob at 0.
    const int copies = p.detuneCopies;
    const float dtC = dt0;
    const float dtL = dt0 * exp2f(-detune * p.detuneLowC * (1.0f / 1200.0f));
    const float dtH = dt0 * exp2f(detune * (copies == 2 ? p.detuneUpperC : p.detuneHighC) * (1.0f / 1200.0f));
    const bool hasL = copies >= 3, hasH = copies >= 2;
    float aC = 1.0f, aL = 0.0f, aH = 0.0f;
    if (copies >= 3) {
      aC = 1.0f / (1.0f + 2.0f * p.detuneSideGain);
      aL = aH = p.detuneSideGain * aC;
    } else if (copies == 2) {
      aC = 1.0f / (1.0f + p.detuneUpperGain);
      aH = p.detuneUpperGain * aC;
    }
    if (dtC >= nyquistGuard) aC = 0.0f;  // copies above 0.45 x sample rate are muted
    if (dtL >= nyquistGuard) aL = 0.0f;
    if (dtH >= nyquistGuard) aH = 0.0f;
    const bool useL = hasL && aL > 0.0f, useH = hasH && aH > 0.0f;
    const bool anyCopy = aC > 0.0f || useL || useH;

    // Formant vowels stay put across the keyboard (as in a real voice), so key
    // tracking does not apply to them.
    const float trackOct = filterType_ == FilterType::Formant ? 0.0f : p.filterTrack * (note_ - 60.0f) * (1.0f / 12.0f);
    float cutoff = p.filterFreq * exp2f(trackOct + p.filterEnvOct * p.filterEnv * env_.level() + filterOct);
    filter_.set(cutoff, p.filterQ, sampleRate);

    const float gSq = anyCopy ? p.levelSquare : 0.0f;
    const float gSaw = anyCopy ? p.levelSaw : 0.0f;
    const float gTri = anyCopy ? p.levelTri : 0.0f;
    const float gSin = anyCopy ? p.levelSine : 0.0f;
    const float gSub1 = dtSub1 < nyquistGuard ? p.levelSub1 : 0.0f;
    const float gSub2 = dtSub2 < nyquistGuard ? p.levelSub2 : 0.0f;
    const float gPink = p.levelPink;

    const float volStep = (volModTarget - volModNow_) / (float)kSubBlock;
    float volModNow = volModNow_;
    // Phases live in registers for the sub-block (the filter call would
    // otherwise force a store and reload of every phase each sample).
    float pC = phase_[0], pL = phase_[1], pH = phase_[2], pS1 = phase_[3], pS2 = phase_[4];

    // ---- audio rate -------------------------------------------------------
    for (int i = 0; i < kSubBlock; ++i) {
      float x = 0.0f;
      if (gSaw > 0.0f) {
        float o = aC * oscSaw(pC, dtC);
        if (useL) o += aL * oscSaw(pL, dtL);
        if (useH) o += aH * oscSaw(pH, dtH);
        x += gSaw * o;
      }
      if (gSq > 0.0f) {
        float o = aC * oscPulse(pC, dtC, pwm);
        if (useL) o += aL * oscPulse(pL, dtL, pwm);
        if (useH) o += aH * oscPulse(pH, dtH, pwm);
        x += gSq * o;
      }
      if (gTri > 0.0f) {
        float o = aC * oscTri(pC);
        if (useL) o += aL * oscTri(pL);
        if (useH) o += aH * oscTri(pH);
        x += gTri * o;
      }
      if (gSin > 0.0f) {
        float o = aC * oscSine(pC);
        if (useL) o += aL * oscSine(pL);
        if (useH) o += aH * oscSine(pH);
        x += gSin * o;
      }
      if (gSub1 > 0.0f) x += gSub1 * oscSine(pS1);
      if (gSub2 > 0.0f) x += gSub2 * oscSine(pS2);
      if (gPink > 0.0f) x += gPink * noise[s + i];

      pC = wrap01(pC + dtC);
      if (hasL) pL = wrap01(pL + dtL);
      if (hasH) pH = wrap01(pH + dtH);
      pS1 = wrap01(pS1 + dtSub1); pS2 = wrap01(pS2 + dtSub2);

      float y = filter_.process(x);
      float e = env_.process();
      volModNow += volStep;
      depthGain_ += (depthTarget - depthGain_) * depthCoef;
      float amp = e * velGain * depthGain_ * volModNow;
      if (fading_) {
        fadeGain_ -= fadeStep_;
        if (fadeGain_ < 0.0f) fadeGain_ = 0.0f;
        amp *= fadeGain_;
      }
      out[s + i] += y * amp;
    }
    phase_[0] = pC; phase_[1] = pL; phase_[2] = pH; phase_[3] = pS1; phase_[4] = pS2;
    volModNow_ = volModTarget;

    // ---- end of life / stolen start ---------------------------------------
    if (fading_ && fadeGain_ <= 0.0f) {
      if (hasPending_) {
        Pending pd = pending_;
        hasPending_ = false;
        begin(pd.key, pd.note, pd.velocity, pd.depth, pd.age, pd.sw);
        depthGain_ = depthLevel(p);
        if (pd.released) release(pd.frozen);
      } else {
        kill();
        return;
      }
    } else if (!env_.active()) {
      kill();
      return;
    }
  }
}

}  // namespace lw
