// L-wave firmware — stored calibration: per-key rest/bottom, per-pot ends,
// per-switch position voltages, and the conversions that use them.
#pragma once
#include <stdint.h>
#include "lwave/config.h"

namespace lw {

/// Calibration values in raw 12-bit ADC counts. Plain data (stored as bytes).
struct CalibrationData {
  uint16_t keyRest[kNumKeys];      ///< Reading with the key up.
  uint16_t keyBottom[kNumKeys];    ///< Reading with the key fully down (may be below rest).
  uint16_t potMin[kNumPots];       ///< Reading at the CCW / bottom end.
  uint16_t potMax[kNumPots];       ///< Reading at the CW / top end.
  uint16_t switchLevel[kNumSwitches][kSwitchPositions];  ///< Reading at positions 1..8.
  uint8_t keyValid[kNumKeys];      ///< 1 if the key was calibrated.
  uint8_t potValid[kNumPots];      ///< 1 if the pot was calibrated.
  uint8_t switchValid[kNumSwitches];  ///< 1 if the switch was calibrated.
  uint8_t pad[2];                  ///< Keeps the size a multiple of 4.

  /// Nominal values used until a calibration is stored: pots rail to rail,
  /// switch ladder at (8 - position) / 7 of 3.3 V, keys assumed to read
  /// mid-rail at rest and rise by 1500 counts when pressed (a guess).
  void setDefaults();

  /// Key travel 0 (rest) .. 1 (bottom), clamped. Works for either sensor polarity.
  float keyTravel(int key, uint16_t raw) const;

  /// Pot position 0..1, clamped.
  float potPosition(int pot, uint16_t raw) const;

  /// Switch position 0..7 nearest to raw.
  int switchPosition(int sw, uint16_t raw) const;

  /// True if every key, pot and switch has been calibrated.
  bool complete() const;

  /// Number of calibrated keys, pots and switches.
  void counts(int& keys, int& pots, int& switches) const;
};

}  // namespace lw
