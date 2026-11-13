#include "lwave/engine.h"
#include <math.h>
#include <string.h>
#include "lwave/curves.h"

namespace lw {

void Engine::init(float sampleRate, int16_t* delayBuffer, uint32_t delayLen) {
  sr_ = sampleRate;
  initDspTables();
  for (Voice& v : voices_) v.kill();
  delay_.init(delayBuffer, delayLen, sampleRate);
  reverb_.init(sampleRate);
  chorus_.init(sampleRate);
  crusher_.init(sampleRate);
  limiter_.init(sampleRate);
  cue_.init(sampleRate);
  volume_.setTime(0.01f, sampleRate);
  volume_.y = 0.0f;
}

void Engine::process(float* out, int n) {
  while (n > 0) {
    int chunk = n > kBlockSize ? kBlockSize : n;
    processBlock(out, chunk);
    out += chunk;
    n -= chunk;
  }
}

void Engine::processBlock(float* out, int n) {
  const EngineParams& p = params_.read();

  NoteEvent e;
  while (events_.pop(e)) handle(e, p);

  // More notes than the polyphony limit allows (the setting was lowered, or
  // the controller lowered it because the CPU is overloaded): fade out the
  // oldest release tails first, then the oldest held notes.
  int busy = busyVoices();
  while (busy > voiceLimit(p)) {
    int victim = oldestVoice(true);
    if (victim < 0) victim = oldestVoice(false);
    if (victim < 0) break;
    voices_[victim].fadeOut(sr_);
    --busy;
  }

  for (int i = 0; i < n; ++i) noise_[i] = pink_.next();
  memset(mix_, 0, sizeof(float) * (size_t)n);

  int active = 0;
  for (Voice& v : voices_) {
    if (!v.active()) continue;
    v.prepareBlock(p, sr_);
    v.render(p, noise_, mix_, n, sr_);
    if (v.active()) ++active;
  }
  activeVoices_.store(active, std::memory_order_relaxed);

  // Shared chain: chorus (detune_copies = 1 only) -> overdrive -> bitcrusher ->
  // delay -> reverb -> volume -> limiter.
  chorus_.set(p.detuneCopies == 1 ? p.detuneKnob : 0.0f);
  drive_.setAmount(p.drive);
  reverb_.set(p.reverbTime, p.reverbMix);
  // The global LFO uses the switch settings captured at the last note-on.
  const bool lfoGlobal = globalDest_ == LfoDest::Bitcrush || globalDest_ == LfoDest::Delay;
  const float dtSub = (float)kSubBlock / sr_;
  for (int s = 0; s < n; s += kSubBlock) {
    float l = globalLfo_.advance(dtSub, globalWave_, p.lfoPeriod, p.lfoDelay);
    globalLfoSmooth_ += (l - globalLfoSmooth_) * 0.5f;
    float crush = p.crush, dTime = p.delayTime;
    if (lfoGlobal && globalDest_ == LfoDest::Bitcrush) crush = clampf(crush + 0.5f * p.lfoStrength * globalLfoSmooth_, 0.0f, 1.0f);
    if (lfoGlobal && globalDest_ == LfoDest::Delay) dTime *= clampf(1.0f + 0.05f * p.lfoStrength * globalLfoSmooth_, 0.25f, 4.0f);
    crusher_.setAmount(crush);
    delay_.set(dTime, p.delayFeedback, p.delayMix);
    int end = s + kSubBlock < n ? s + kSubBlock : n;
    for (int i = s; i < end; ++i) {
      float x = mix_[i] * kVoiceSumTrim;
      x = chorus_.process(x);
      x = drive_.process(x);
      x = crusher_.process(x);
      x = delay_.process(x);
      x = reverb_.process(x);
      x *= volume_.process(p.volume);
      out[i] = limiter_.process(x);
    }
  }
  cue_.render(out, n);
  meter_.update(out, n, sr_, p.meterFallDbS);
  levelDb_.store(meter_.levelDb(), std::memory_order_relaxed);
  limitDb_.store(limiter_.reductionDb(), std::memory_order_relaxed);
}

void Engine::handle(const NoteEvent& e, const EngineParams& p) {
  switch (e.type) {
    case NoteEvent::Type::On: noteOn(e, p); break;
    case NoteEvent::Type::Off: noteOff(e); break;
    case NoteEvent::Type::Cue: cue_.play(e.cueHz, e.cueSeconds, 0.125f); break;
    case NoteEvent::Type::AllOff:
      for (Voice& v : voices_)
        if (v.active() && !v.released()) v.release(0.0f);
      break;
  }
}

int Engine::voiceLimit(const EngineParams& p) {
  return p.voices < 1 ? 1 : (p.voices > kMaxVoices ? kMaxVoices : p.voices);
}

int Engine::busyVoices() const {
  int busy = 0;
  for (const Voice& v : voices_)
    if (v.active() && !v.fadingOut()) ++busy;
  return busy;
}

int Engine::oldestVoice(bool releasedOnly) const {
  int chosen = -1;
  uint32_t best = 0xFFFFFFFFu;
  for (int i = 0; i < kMaxVoices; ++i) {
    const Voice& v = voices_[i];
    if (!v.active() || v.fadingOut() || (releasedOnly && !v.released())) continue;
    if (v.age() < best) { best = v.age(); chosen = i; }
  }
  return chosen;
}

void Engine::noteOn(const NoteEvent& e, const EngineParams& p) {
  bool anyHeld = false;
  for (const Voice& v : voices_)
    if (v.active() && !v.released()) anyHeld = true;

  // Choose a voice: below the limit an idle one (or one already fading out);
  // at the limit steal the oldest release tail, then the oldest held note.
  int chosen = -1;
  if (busyVoices() < voiceLimit(p)) {
    for (int i = 0; i < kMaxVoices && chosen < 0; ++i)
      if (!voices_[i].active()) chosen = i;
    for (int i = 0; i < kMaxVoices && chosen < 0; ++i)
      if (voices_[i].fadingOut()) chosen = i;
  }
  if (chosen < 0) chosen = oldestVoice(true);
  if (chosen < 0) chosen = oldestVoice(false);
  if (chosen < 0) return;

  Voice& v = voices_[chosen];
  uint32_t age = ++ageCounter_;
  if (v.active()) {
    v.steal(e.key, e.note, e.velocity, e.depth, age, p, sr_);
  } else {
    v.start(e.key, e.note, e.velocity, e.depth, age, p);
    // Same pitch already sounding on another key: start in phase so the two simply add.
    for (int i = 0; i < kMaxVoices; ++i) {
      const Voice& o = voices_[i];
      if (i != chosen && o.active() && !o.stealing() && fabsf(o.note() - e.note) < 0.01f) {
        v.copyPhases(o);
        break;
      }
    }
  }

  // Global LFO: one-shots restart on every note; repeating shapes only restart
  // their fade-in when a note follows silence.
  globalWave_ = p.lfoWave;
  globalDest_ = p.lfoDest;
  if (isOneShot(p.lfoWave)) globalLfo_.trigger();
  else if (!anyHeld) globalLfo_.restartDelay();
}

void Engine::noteOff(const NoteEvent& e) {
  for (Voice& v : voices_)
    if (v.active() && !v.released() && v.key() == e.key) {
      v.release(e.depth);
      return;
    }
}

}  // namespace lw
