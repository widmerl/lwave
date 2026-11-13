// L-wave renderer: plays scripted scenes through the real engine and writes
// WAV files, so the sound can be judged before the hardware exists.
//
//   lwave_render <output-dir>          render all demo scenes
//   lwave_render --bench               time the worst case (all voices, all oscillators)
#include <algorithm>
#include <chrono>
#include <math.h>
#include <functional>
#include <stdio.h>
#include <string>
#include <vector>
#include "lwave/control_mapper.h"
#include "lwave/curves.h"
#include "lwave/engine.h"
#include "lwave/hw_map.h"
#include "wav.h"

using namespace lw;

namespace {
constexpr float kSr = 44100.0f;
std::vector<int16_t> gDelay((size_t)(20.5f * kSr));

/// A scene: a panel setting plus timed note events.
struct Scene {
  std::string name;
  float seconds;
  PanelState panel;
  Settings settings;
  struct Ev { float t; bool on; int key; float vel; };
  std::vector<Ev> events;
  std::function<void(float t, PanelState&)> automate;  // optional knob movement
};

/// Panel with every pot at a neutral position and only the saw slider up.
PanelState neutralPanel() {
  PanelState p;
  for (float& v : p.pot) v = 0.0f;
  auto set = [&](Pot which, float v) { p.pot[static_cast<int>(which)] = v; };
  set(Pot::LfoPeriod, 0.667f); set(Pot::LfoDelay, 0.0f);  // 1 s, 10 ms
  set(Pot::Attack, 0.0f); set(Pot::Decay, 0.466f); set(Pot::Sustain, 0.7f); set(Pot::Release, 0.392f);  // 10 ms, 0.25 s, 70 %, 0.15 s
  set(Pot::ReverbTime, 0.5f); set(Pot::DelayTime, 0.58f);  // 1 s, 0.55 s
  set(Pot::FilterFreq, 0.75f); set(Pot::FilterRes, 0.55f);
  set(Pot::Tune, 0.5f); set(Pot::Pwm, 0.5f);
  set(Pot::Volume, 0.85f); set(Pot::LevelSaw, 0.75f);
  p.sw[static_cast<int>(Switch::Octave)] = 3;
  p.sw[static_cast<int>(Switch::FilterType)] = 5;  // LP12
  p.sw[static_cast<int>(Switch::LfoWave)] = 7;     // sine
  p.sw[static_cast<int>(Switch::LfoDest)] = 6;     // pitch
  return p;
}

void pot(PanelState& p, Pot which, float v) { p.pot[static_cast<int>(which)] = v; }
int key(const char* name) { return keyIndexByName(name); }

/// Adds a chord (keys by net name) from t0 to t1.
void chord(Scene& s, float t0, float t1, std::initializer_list<const char*> keys, float vel = 0.9f) {
  for (const char* k : keys) {
    s.events.push_back({t0, true, key(k), vel});
    s.events.push_back({t1, false, key(k), 0});
  }
}

std::vector<float> render(const Scene& sc) {
  static Engine engine;  // large; keep off the stack
  engine.init(kSr, gDelay.data(), (uint32_t)gDelay.size());
  std::vector<float> out;
  int total = (int)(sc.seconds * kSr) / kBlockSize * kBlockSize;
  float buf[kBlockSize];
  size_t next = 0;
  std::vector<Scene::Ev> evs = sc.events;
  std::sort(evs.begin(), evs.end(), [](const Scene::Ev& a, const Scene::Ev& b) { return a.t < b.t; });
  PanelState panel = sc.panel;
  for (int i = 0; i < total; i += kBlockSize) {
    float t = (float)i / kSr;
    if (sc.automate) sc.automate(t, panel);
    EngineParams& p = engine.params().writeBuffer();
    mapControls(panel, sc.settings, engine.maxDelaySeconds(), p);
    for (float& d : p.keyDepth) d = 1.0f;
    engine.params().publish();
    while (next < evs.size() && evs[next].t <= t) {
      const Scene::Ev& e = evs[next++];
      NoteEvent n;
      n.type = e.on ? NoteEvent::Type::On : NoteEvent::Type::Off;
      n.key = (uint8_t)e.key;
      n.note = (float)kKeys[e.key].note + (float)octaveShiftSemitones(panel.sw[static_cast<int>(Switch::Octave)]);
      n.velocity = e.vel;
      n.depth = 1.0f;
      engine.post(n);
    }
    engine.process(buf, kBlockSize);
    out.insert(out.end(), buf, buf + kBlockSize);
  }
  return out;
}

std::vector<Scene> scenes() {
  std::vector<Scene> v;
  // 1. Tonnetz triads: neighbouring triangles on the key grid.
  {
    Scene s{"01_tonnetz_triads", 9.0f, neutralPanel(), Settings(), {}, nullptr};
    pot(s.panel, Pot::LevelSquare, 0.6f); pot(s.panel, Pot::Detune, 0.7f);
    pot(s.panel, Pot::ReverbMix, 0.6f);
    chord(s, 0.0f, 1.8f, {"D5", "D6", "C5"});  // C major: C4 E4 G4
    chord(s, 2.0f, 3.8f, {"D6", "C5", "C6"});  // E minor: E4 G4 B4
    chord(s, 4.0f, 5.8f, {"D5", "C4", "C5"});  // C minor: C4 D#4 G4
    chord(s, 6.0f, 7.8f, {"D4", "D5", "C4"});  // G# major: G#3 C4 D#4
    v.push_back(s);
  }
  // 2. The eight filter types on one sustained chord with a slow cutoff sweep.
  {
    Scene s{"02_filter_types", 16.0f, neutralPanel(), Settings(), {}, nullptr};
    pot(s.panel, Pot::FilterRes, 0.75f); pot(s.panel, Pot::FilterTrack, 0.5f);
    for (int ft = 0; ft < 8; ++ft) chord(s, 2.0f * ft, 2.0f * ft + 1.9f, {"D2", "D5"});
    s.automate = [](float t, PanelState& p) {
      p.sw[static_cast<int>(Switch::FilterType)] = (int)(t / 2.0f) & 7;
      float ph = fmodf(t, 2.0f) / 2.0f;
      pot(p, Pot::FilterFreq, 0.35f + 0.5f * ph);
    };
    v.push_back(s);
  }
  // 3. The eight LFO shapes on pitch (one-shots retrigger with each note).
  {
    Scene s{"03_lfo_shapes_on_pitch", 16.0f, neutralPanel(), Settings(), {}, nullptr};
    pot(s.panel, Pot::LfoStrength, 0.3f); pot(s.panel, Pot::LfoPeriod, 0.534f);  // 0.4 s
    pot(s.panel, Pot::LevelSaw, 0.0f); pot(s.panel, Pot::LevelTri, 0.8f);
    for (int w = 0; w < 8; ++w) chord(s, 2.0f * w, 2.0f * w + 1.8f, {"B2"});
    s.automate = [](float t, PanelState& p) { p.sw[static_cast<int>(Switch::LfoWave)] = (int)(t / 2.0f) & 7; };
    v.push_back(s);
  }
  // 4. Effects: dry, overdrive, bitcrusher sweep, delay, reverb.
  {
    Scene s{"04_effects", 20.0f, neutralPanel(), Settings(), {}, nullptr};
    for (int i = 0; i < 10; ++i) chord(s, 2.0f * i, 2.0f * i + 0.6f, {"D5", "D6"}, 0.8f);
    s.automate = [](float t, PanelState& p) {
      pot(p, Pot::Overdrive, t >= 4.0f && t < 8.0f ? (t - 4.0f) / 4.0f : 0.0f);
      pot(p, Pot::Crush, t >= 8.0f && t < 12.0f ? (t - 8.0f) / 4.0f : 0.0f);
      pot(p, Pot::DelayMix, t >= 12.0f && t < 16.0f ? 0.9f : 0.0f);
      pot(p, Pot::DelayFeedback, 0.6f);
      pot(p, Pot::ReverbMix, t >= 16.0f ? 1.0f : 0.0f);
      pot(p, Pot::ReverbTime, 0.75f);
    };
    v.push_back(s);
  }
  // 5. Velocity: the same note from softest to hardest, with the velocity knob
  //    also scaling attack time.
  {
    Scene s{"05_velocity", 8.0f, neutralPanel(), Settings(), {}, nullptr};
    pot(s.panel, Pot::Velocity, 1.0f); pot(s.panel, Pot::Attack, 0.271f);  // ~65 ms
    for (int i = 0; i < 8; ++i) s.events.push_back({1.0f * i, true, key("D5"), i / 7.0f}),
                                s.events.push_back({1.0f * i + 0.7f, false, key("D5"), 0});
    v.push_back(s);
  }
  // 6. A pad: all oscillators, detune, slow attack, LP24 with envelope, reverb.
  {
    Scene s{"06_pad", 10.0f, neutralPanel(), Settings(), {}, nullptr};
    for (Pot l : {Pot::LevelSquare, Pot::LevelSaw, Pot::LevelTri, Pot::LevelSine, Pot::LevelSub1})
      pot(s.panel, l, 0.55f);
    pot(s.panel, Pot::LevelPink, 0.25f);
    pot(s.panel, Pot::Detune, 0.85f); pot(s.panel, Pot::Attack, 0.604f); pot(s.panel, Pot::Release, 0.667f);  // ~0.65 s, ~1 s
    pot(s.panel, Pot::FilterFreq, 0.45f); pot(s.panel, Pot::FilterEnv, 0.5f); pot(s.panel, Pot::FilterRes, 0.65f);
    pot(s.panel, Pot::ReverbMix, 0.9f); pot(s.panel, Pot::ReverbTime, 0.8f);
    s.panel.sw[static_cast<int>(Switch::FilterType)] = 6;  // LP24
    chord(s, 0.0f, 6.0f, {"D5", "D6", "C5", "C6"});
    v.push_back(s);
  }
  return v;
}

int bench() {
  static Engine engine;
  engine.init(kSr, gDelay.data(), (uint32_t)gDelay.size());
  PanelState p = neutralPanel();
  for (Pot l : {Pot::LevelPink, Pot::LevelSquare, Pot::LevelSaw, Pot::LevelTri, Pot::LevelSine, Pot::LevelSub1,
                Pot::LevelSub2})
    pot(p, l, 0.5f);
  pot(p, Pot::Detune, 0.8f); pot(p, Pot::ReverbMix, 0.8f); pot(p, Pot::DelayMix, 0.6f);
  pot(p, Pot::Overdrive, 0.5f); pot(p, Pot::Crush, 0.2f); pot(p, Pot::LfoStrength, 0.3f);
  p.sw[static_cast<int>(Switch::FilterType)] = 1;  // formant (heaviest)
  EngineParams& ep = engine.params().writeBuffer();
  mapControls(p, Settings(), engine.maxDelaySeconds(), ep);
  for (float& d : ep.keyDepth) d = 1.0f;
  engine.params().publish();
  for (int k = 0; k < 10; ++k) {
    NoteEvent n; n.type = NoteEvent::Type::On; n.key = (uint8_t)k; n.note = 48.0f + 3.0f * k; n.velocity = 1.0f;
    engine.post(n);
  }
  float buf[kBlockSize];
  const int blocks = 3445;  // 10 s
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < blocks; ++i) engine.process(buf, kBlockSize);
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  double audio = blocks * kBlockSize / kSr;
  printf("10 voices, all oscillators, formant filter, all effects: %.1f us per block, %.1f%% of real time on this PC\n",
         s / blocks * 1e6, 100.0 * s / audio);
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--bench") return bench();
  std::string dir = argc > 1 ? argv[1] : ".";
  for (const Scene& s : scenes()) {
    std::vector<float> audio = render(s);
    float peak = 0;
    for (float x : audio) peak = fmaxf(peak, fabsf(x));
    std::string path = dir + "/" + s.name + ".wav";
    bool ok = writeWav(path.c_str(), audio, (int)kSr);
    printf("%-28s %5.1f s  peak %6.1f dBFS  %s\n", s.name.c_str(), s.seconds, gainToDb(peak), ok ? "ok" : "WRITE FAILED");
  }
  return 0;
}
