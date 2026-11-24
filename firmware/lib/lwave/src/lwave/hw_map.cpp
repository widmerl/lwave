#include "lwave/hw_map.h"
#include <string.h>

namespace lw {

const AnalogSource kPotSources[kNumPots] = {
    {U105, 15},  // LfoPeriod      RR20
    {U105, 14},  // LfoDelay       RR21
    {U105, 13},  // LfoStrength    RR22
    {U105, 12},  // Velocity       RR31
    {U105, 11},  // Attack         RR23
    {U105, 10},  // Decay          RR24
    {U105, 9},   // Sustain        RR25
    {U105, 8},   // Release        RR26
    {U104, 2},   // ReverbTime     RR38
    {U104, 1},   // ReverbMix      RR32
    {U105, 3},   // DelayTime      RR27
    {U105, 2},   // DelayFeedback  RR28
    {U105, 1},   // DelayMix       RR30
    {U104, 0},   // FilterFreq     RR33
    {U104, 15},  // FilterEnv      RR35
    {U105, 4},   // FilterTrack    RR36
    {U105, 5},   // FilterRes      RR34
    {U104, 4},   // Overdrive      RR19
    {U104, 7},   // Tune           RR29
    {U104, 10},  // Crush          RR40
    {U105, 6},   // Pwm            RR39
    {U105, 7},   // Detune         RR37
    {U104, 5},   // Volume         LR12
    {U104, 6},   // LevelPink      LR13
    {U104, 8},   // LevelSquare    LR8
    {U104, 9},   // LevelSaw       LR9
    {U104, 11},  // LevelTri       LR10
    {U104, 12},  // LevelSine      LR11
    {U104, 13},  // LevelSub1      LR6
    {U104, 14},  // LevelSub2      LR7
};

const AnalogSource kSwitchSources[kNumSwitches] = {
    {kDirect, 25},  // LfoDest    SW4, pin 25 (A11)
    {U105, 0},      // LfoWave    SW3
    {kDirect, 26},  // Octave     SW2, pin 26 (A12)
    {U104, 3},      // FilterType SW6
};

const KeyInfo kKeys[kNumKeys] = {
    {"A1", U101, 1, 65},  {"A2", U101, 12, 69}, {"A3", U101, 13, 73}, {"A4", U101, 14, 77},
    {"A5", U101, 15, 81}, {"A6", U101, 11, 85}, {"A7", U101, 10, 89},
    {"B1", U101, 2, 58},  {"B2", U101, 0, 62},  {"B3", U101, 5, 66},  {"B4", U101, 4, 70},
    {"B5", U101, 3, 74},  {"B6", U101, 6, 78},  {"B7", U101, 7, 82},  {"B8", U101, 9, 86},
    {"C1", U102, 15, 51}, {"C2", U102, 14, 55}, {"C3", U102, 13, 59}, {"C4", U102, 11, 63},
    {"C5", U102, 12, 67}, {"C6", U102, 10, 71}, {"C7", U102, 9, 75},  {"C8", U102, 8, 79},
    {"C9", U101, 8, 83},
    {"D1", U103, 14, 44}, {"D2", U102, 1, 48},  {"D3", U102, 0, 52},  {"D4", U102, 5, 56},
    {"D5", U102, 6, 60},  {"D6", U102, 7, 64},  {"D7", U102, 4, 68},  {"D8", U102, 3, 72},
    {"D9", U102, 2, 76},
    {"E1", U103, 15, 41}, {"E2", U103, 0, 45},  {"E3", U103, 13, 49}, {"E4", U103, 11, 53},
    {"E5", U103, 12, 57}, {"E6", U103, 10, 61}, {"E7", U103, 9, 65},  {"E8", U103, 8, 69},
    {"F1", U103, 2, 38},  {"F2", U103, 1, 42},  {"F3", U103, 6, 46},  {"F4", U103, 7, 50},
    {"F5", U103, 5, 54},  {"F6", U103, 4, 58},  {"F7", U103, 3, 62},
};

const char* const kPotNames[kNumPots] = {
    "lfo_period", "lfo_delay", "lfo_strength", "velocity",
    "attack", "decay", "sustain", "release",
    "rvb_time", "rvb_mix", "dly_time", "dly_fb", "dly_mix",
    "flt_freq", "flt_env", "flt_track", "flt_res",
    "drive", "tune", "crush", "pwm", "detune",
    "volume", "lvl_pink", "lvl_square", "lvl_saw", "lvl_tri", "lvl_sine", "lvl_sub1", "lvl_sub2",
};

// The 22 dials read high when turned fully counter-clockwise (found on the first
// hardware build); the 8 sliders read low at the bottom.
static_assert(static_cast<int>(Pot::Volume) == 22, "dials are the first 22 pots");
const bool kPotReversed[kNumPots] = {
    true, true, true, true,               // LFO period, delay, strength, velocity
    true, true, true, true,               // attack, decay, sustain, release
    true, true, true, true, true,         // reverb time/mix, delay time/feedback/mix
    true, true, true, true,               // filter freq, env, tracking, resonance
    true, true, true, true, true,         // overdrive, tune, crush, PWM, detune
    false, false, false, false, false, false, false, false,  // sliders
};

const char* const kSwitchNames[kNumSwitches] = {"lfo_dest", "lfo_wave", "octave", "flt_type"};

int keyIndexByName(const char* name) {
  for (int i = 0; i < kNumKeys; ++i)
    if (strcmp(kKeys[i].name, name) == 0) return i;
  return -1;
}

int keyIndexAt(uint8_t mux, uint8_t channel) {
  for (int i = 0; i < kNumKeys; ++i)
    if (kKeys[i].mux == mux && kKeys[i].channel == channel) return i;
  return -1;
}

int potIndexAt(uint8_t mux, uint8_t channel) {
  for (int i = 0; i < kNumPots; ++i)
    if (kPotSources[i].mux == mux && kPotSources[i].channel == channel) return i;
  return -1;
}

int switchIndexAt(uint8_t mux, uint8_t channel) {
  for (int i = 0; i < kNumSwitches; ++i)
    if (kSwitchSources[i].mux == mux && kSwitchSources[i].channel == channel) return i;
  return -1;
}

}  // namespace lw
