// L-wave firmware — turning raw readings into notes, knob positions, switch
// positions and battery state.
#pragma once
#include <stdint.h>
#include "lwave/calibration_data.h"
#include "lwave/config.h"
#include "lwave/settings.h"

namespace lw {

/// One complete scan of the panel, as delivered by the scanner.
struct ScanFrame {
  uint16_t keys[kNumKeys];          ///< Raw key readings, indexed like kKeys.
  uint32_t keyTimeUs[kNumKeys];     ///< When each key was read (microseconds).
  uint16_t pots[kNumPots];          ///< Raw pot readings, indexed by Pot.
  uint16_t switches[kNumSwitches];  ///< Raw switch readings, indexed by Switch.
  uint16_t battery;                 ///< Raw battery-sense reading.
  bool lineOut;                     ///< Line-out detect pin level.
  uint32_t timeUs;                  ///< When the scan finished.
};

/// A note-on or note-off detected by the KeyProcessor.
struct KeyEvent {
  bool on;         ///< true = note-on, false = note-off.
  uint8_t key;     ///< Key index.
  float velocity;  ///< 0..1 (note-on only).
  float depth;     ///< Travel at note-on, or the frozen depth for the release.
  float travelMs;  ///< Measured start → note-on time (note-on only).
};

/// Note detection, velocity and depth for all keys.
///
/// Each key walks Idle → Timing (passed key_vel_start) → On (passed
/// key_note_on) → Idle (fell below key_note_off). Velocity comes from the time
/// between the two upward crossings, interpolated between samples. A key that
/// turns back before key_note_on plays nothing.
class KeyProcessor {
 public:
  /// Clears all key states.
  void reset();

  /// Feeds one reading of key k (travel 0..1 at time tUs). Returns true and
  /// fills ev when a note starts or ends.
  bool update(int k, float travel, uint32_t tUs, const Settings& s, KeyEvent& ev);

  /// Key depth of key k (0..1): the highest travel within the last depth_freeze_ms,
  /// so lifting the key does not dip the level before the note ends.
  float depth(int k) const { return keys_[k].held; }
  /// Current travel of key k (0..1).
  float travel(int k) const { return keys_[k].travel; }
  /// True while key k's note is on.
  bool on(int k) const { return keys_[k].state == State::On; }
  /// Velocity of key k's last note-on.
  float lastVelocity(int k) const { return keys_[k].velocity; }

  /// Velocity (0..1) for a start → note-on travel time, using the vel_fast_ms / vel_slow_ms settings.
  static float velocityFromMs(float ms, const Settings& s);

 private:
  enum class State : uint8_t { Idle, Timing, On };
  static constexpr int kHistory = 64;  // depth history, about 1 ms apart
  struct Key {
    State state = State::Idle;
    float travel = 0.0f, prevTravel = 0.0f, velocity = 0.0f, held = 0.0f;
    uint32_t prevT = 0, startT = 0, histT = 0;
    bool havePrev = false;
    float hist[kHistory] = {};
    uint8_t histPos = 0;
  };
  /// Highest travel in the history entries of the last windowMs.
  float windowMax(const Key& key, float windowMs) const;
  Key keys_[kNumKeys];
};

/// Smooths a pot position and adds a small dead band so a resting pot never jitters.
class PotFilter {
 public:
  /// Feeds a calibrated position (0..1) after dtS seconds; returns the filtered value.
  float update(float x, float dtS, const Settings& s);
  /// Last filtered value.
  float value() const { return out_; }
 private:
  bool init_ = false;
  float smooth_ = 0.0f, out_ = 0.0f;
};

/// Debounced rotary-switch position.
class SwitchDecoder {
 public:
  /// Feeds a raw reading at tUs; returns the accepted position 0..7.
  int update(int sw, uint16_t raw, uint32_t tUs, const CalibrationData& cal, const Settings& s);
  /// Accepted position 0..7.
  int position() const { return pos_; }
 private:
  bool init_ = false;
  int pos_ = 0, candidate_ = 0;
  uint32_t since_ = 0;
};

/// Battery voltage, smoothed, with low / critical flags (with 0.05 V hysteresis).
class BatteryMonitor {
 public:
  /// Feeds a raw reading after dtS seconds.
  void update(uint16_t raw, float dtS, const Settings& s);
  float volts() const { return volts_; }       ///< Smoothed battery voltage.
  bool low() const { return low_; }            ///< Below batt_low_v.
  bool critical() const { return crit_; }      ///< Below batt_crit_v.
 private:
  bool init_ = false, low_ = false, crit_ = false;
  float volts_ = 0.0f;
};

/// Finds keys held down at power-up. Uses calibrated travel (> 50 %) for
/// calibrated keys; otherwise compares each key with the median of all 48
/// readings, so it works before any calibration exists.
void detectHeldKeys(const uint16_t raw[kNumKeys], const CalibrationData& cal, bool held[kNumKeys]);

}  // namespace lw
