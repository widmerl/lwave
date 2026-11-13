// L-wave firmware — hardware map: which panel control sits on which mux
// channel or Teensy pin, and which note each key plays.
// Source: "L-wave V1.0 — front-panel controls" and the Teensy schematic.
#pragma once
#include <stdint.h>
#include "lwave/config.h"

namespace lw {

/// Potentiometers (22 dials + 8 sliders), in a fixed order used everywhere.
enum class Pot : uint8_t {
  LfoPeriod, LfoDelay, LfoStrength, Velocity,
  Attack, Decay, Sustain, Release,
  ReverbTime, ReverbMix, DelayTime, DelayFeedback, DelayMix,
  FilterFreq, FilterEnv, FilterTrack, FilterRes,
  Overdrive, Tune, Crush, Pwm, Detune,
  Volume, LevelPink, LevelSquare, LevelSaw, LevelTri, LevelSine, LevelSub1, LevelSub2,
  Count
};
static_assert(static_cast<int>(Pot::Count) == kNumPots, "pot count mismatch");

/// Rotary switches.
enum class Switch : uint8_t { LfoDest, LfoWave, Octave, FilterType, Count };
static_assert(static_cast<int>(Switch::Count) == kNumSwitches, "switch count mismatch");

/// Mux numbers: index into kMuxPins. U101..U105 → 0..4.
enum Mux : uint8_t { U101 = 0, U102 = 1, U103 = 2, U104 = 3, U105 = 4, kNumMuxes = 5, kDirect = 0xFF };

/// Where an analog value comes from: a mux channel, or a direct Teensy pin.
struct AnalogSource {
  uint8_t mux;      ///< Mux index (U101..U105) or kDirect.
  uint8_t channel;  ///< Mux channel 0-15, or the Teensy pin number when mux == kDirect.
};

/// One note key.
struct KeyInfo {
  const char* name;  ///< KiCad net name, e.g. "D5".
  uint8_t mux;       ///< Mux index (U101..U103).
  uint8_t channel;   ///< Mux channel 0-15.
  uint8_t note;      ///< MIDI note with the octave switch in position 4 (D5 = C4 = 60).
};

// ---- Teensy pins -----------------------------------------------------------
/// Teensy pin carrying each mux's common output (U101..U105).
constexpr uint8_t kMuxPins[kNumMuxes] = {14, 16, 17, 22, 41};
/// Mux select lines S0..S3.
constexpr uint8_t kMuxSelectPins[4] = {37, 36, 35, 34};
/// LED pins: 0-7 volume meter (0-4 green, 5-6 yellow, 7 red), 8 low battery.
constexpr uint8_t kLedPins[kNumLeds] = {10, 11, 12, 24, 27, 28, 29, 30, 31};
/// Battery voltage sense (analog, through a divider).
constexpr uint8_t kBatteryPin = 15;
/// Line-out plug detect (digital input).
constexpr uint8_t kLineOutDetectPin = 9;
/// Teensy on-board LED (not wired on the panel, used for the alive blink).
constexpr uint8_t kOnboardLedPin = 13;

// ---- Tables (defined in hw_map.cpp) ----------------------------------------
/// Source of each pot, indexed by Pot.
extern const AnalogSource kPotSources[kNumPots];
/// Source of each rotary switch, indexed by Switch.
extern const AnalogSource kSwitchSources[kNumSwitches];
/// All 48 keys, in net-name order A1..F7.
extern const KeyInfo kKeys[kNumKeys];
/// Short display names of the pots, indexed by Pot.
extern const char* const kPotNames[kNumPots];
/// Short display names of the switches, indexed by Switch.
extern const char* const kSwitchNames[kNumSwitches];

/// Index into kKeys of the key with this net name, or -1 if there is none.
int keyIndexByName(const char* name);

/// Index of the key on mux `mux`, channel `channel`, or -1 if none.
int keyIndexAt(uint8_t mux, uint8_t channel);

/// Index of the pot on mux `mux`, channel `channel`, or -1 if none.
int potIndexAt(uint8_t mux, uint8_t channel);

/// Index of the switch on mux `mux`, channel `channel`, or -1 if none.
int switchIndexAt(uint8_t mux, uint8_t channel);

/// Key held at power-up to enter calibration: D5, which plays C4.
constexpr const char* kCalibrationKey = "D5";
/// Key held at power-up to enter debug mode: F4, which plays D3.
constexpr const char* kDebugKey = "F4";

}  // namespace lw
