#include "lwave/power.h"
#include "lwave/config.h"

namespace lw {

namespace {
constexpr uint64_t us(float seconds) { return (uint64_t)(seconds * 1e6f); }
}  // namespace

PowerManager::Event PowerManager::update(uint32_t dtUs, bool button, bool used, bool batteryCritical,
                                         float autoOffMin) {
  now_ += dtUs;
  Event ev = Event::None;

  // Debounced button with press edge. armed_: it has been seen released since
  // start-up or since switching off.
  if (button != rawButton_) {
    rawButton_ = button;
    rawSince_ = now_;
  }
  const bool steady = now_ - rawSince_ >= us(kPowerButtonDebounceS);
  bool pressEdge = false;
  if (rawButton_ != button_ && steady) {
    button_ = rawButton_;
    if (button_) {
      pressedAt_ = now_;
      pressEdge = armed_;
    }
  }
  if (!rawButton_ && !button_ && steady) armed_ = true;  // seen released
  if (pressEdge) used = true;

  // Battery: switch off once per critical episode, after kPowerBatteryOffS.
  if (batteryCritical) {
    if (!critical_) criticalSince_ = now_;
  } else {
    criticalHandled_ = false;
  }
  critical_ = batteryCritical;

  switch (state_) {
    case State::Starting:
      if (used) lastUse_ = now_;
      if (now_ >= us(kPowerLatchS)) {
        state_ = State::On;
        lastUse_ = now_;
        ev = Event::Latched;
      }
      break;
    case State::On:
      if (used) lastUse_ = now_;
      if (button_ && armed_ && now_ - pressedAt_ >= us(kPowerButtonHoldS)) {
        switchOff(PowerOffReason::Button);
        ev = Event::SwitchedOff;
      } else if (critical_ && !criticalHandled_ && now_ - criticalSince_ >= us(kPowerBatteryOffS)) {
        criticalHandled_ = true;
        switchOff(PowerOffReason::Battery);
        ev = Event::SwitchedOff;
      } else if (autoOffMin > 0.0f && now_ - lastUse_ >= (uint64_t)(autoOffMin * 60.0e6f)) {
        switchOff(PowerOffReason::Idle);
        ev = Event::SwitchedOff;
      }
      break;
    case State::Off:
      // Only reached when the power did not go away (USB, or no power circuit).
      if (used) {
        state_ = State::On;
        reason_ = PowerOffReason::None;
        lastUse_ = now_;
        ev = Event::Woke;
      }
      break;
  }
  return ev;
}

void PowerManager::switchOff(PowerOffReason r) {
  state_ = State::Off;
  reason_ = r;
  offAt_ = now_;
  armed_ = false;  // a button still held from the long press must not wake it
}

bool PowerManager::holdPower() const {
  switch (state_) {
    case State::Starting: return false;
    case State::On: return true;
    case State::Off: return now_ - offAt_ < us(kPowerOffDelayS);
  }
  return true;
}

const char* powerOffReasonText(PowerOffReason r) {
  switch (r) {
    case PowerOffReason::Button: return "power button held";
    case PowerOffReason::Idle: return "not used for auto_off_min minutes";
    case PowerOffReason::Battery: return "battery critically low";
    default: return "-";
  }
}

}  // namespace lw
