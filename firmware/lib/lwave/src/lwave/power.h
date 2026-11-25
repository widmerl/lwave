// L-wave firmware — soft power: the power button (pin 32), the power hold
// output (pin 33), switching off after a long press, after an hour without use,
// or when the battery is critically low.
#pragma once
#include <stdint.h>

namespace lw {

/// Why the synth switched itself off.
enum class PowerOffReason : uint8_t {
  None,     ///< Not switched off.
  Button,   ///< Power button held for kPowerButtonHoldS.
  Idle,     ///< No use for auto_off_min minutes.
  Battery,  ///< Battery critically low for kPowerBatteryOffS.
};

/// The soft-power state machine. Plain logic, fed once per scan:
///  - Starting: the hold output is low; after kPowerLatchS it goes high (On),
///    so the power stays on once the button that switched it on is released.
///  - On: holding the button for kPowerButtonHoldS, no use for the auto-off
///    time, or a critically low battery for kPowerBatteryOffS switches Off.
///    The button only counts after it has been released once, so the press that
///    switched the synth on cannot switch it off again.
///  - Off: the hold output drops after kPowerOffDelayS. On the next hardware
///    this cuts the power once the button is up. If the synth keeps running
///    (USB power, or no power circuit yet), any use or a new button press
///    switches it back On.
class PowerManager {
 public:
  /// State of the soft power.
  enum class State : uint8_t { Starting, On, Off };
  /// What happened in the last update().
  enum class Event : uint8_t {
    None,         ///< Nothing.
    Latched,      ///< Start-up finished: the hold output went high.
    SwitchedOff,  ///< Decided to switch off (see offReason()).
    Woke,         ///< Still running after switching off, and used again: on again.
  };

  /// Feeds one scan. dtUs: time since the last call; button: power button
  /// pressed; used: the player did something this scan (key, pot, switch,
  /// console); batteryCritical: from BatteryMonitor; autoOffMin: setting
  /// auto_off_min (0 = never). Returns what happened.
  Event update(uint32_t dtUs, bool button, bool used, bool batteryCritical, float autoOffMin);

  /// Level for the power hold output: true = keep the power on.
  bool holdPower() const;
  /// Current state.
  State state() const { return state_; }
  /// Why it last switched off.
  PowerOffReason offReason() const { return reason_; }
  /// Seconds since the last use.
  float idleSeconds() const { return (float)(now_ - lastUse_) * 1e-6f; }
  /// Seconds since start-up.
  float uptimeSeconds() const { return (float)now_ * 1e-6f; }

 private:
  void switchOff(PowerOffReason r);

  State state_ = State::Starting;
  PowerOffReason reason_ = PowerOffReason::None;
  uint64_t now_ = 0, lastUse_ = 0, offAt_ = 0;
  // button
  bool rawButton_ = false, button_ = false, armed_ = false;
  uint64_t rawSince_ = 0, pressedAt_ = 0;
  // battery
  bool critical_ = false, criticalHandled_ = false;
  uint64_t criticalSince_ = 0;
};

/// Short text for a reason, e.g. "power button held".
const char* powerOffReasonText(PowerOffReason r);

}  // namespace lw
