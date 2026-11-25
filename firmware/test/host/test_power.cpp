// Tests: soft power (power button, hold output, auto-off, battery).
#include "lwave/config.h"
#include "lwave/power.h"
#include "test.h"

using namespace lw;

namespace {
constexpr uint32_t kDt = 500;  // 2 kHz scans
using Ev = PowerManager::Event;
using St = PowerManager::State;

// Runs `seconds` of scans with the given inputs; returns the last non-None event.
Ev run(PowerManager& pm, float seconds, bool button = false, bool used = false, bool crit = false, float autoOff = 60.0f) {
  Ev last = Ev::None;
  for (int i = 0; i < (int)(seconds * 2000.0f); ++i) {
    Ev e = pm.update(kDt, button, used && i == 0, crit, autoOff);
    if (e != Ev::None) last = e;
  }
  return last;
}
}  // namespace

TEST(power_latches_after_two_seconds_even_with_the_button_still_held) {
  PowerManager pm;
  CHECK(!pm.holdPower());
  run(pm, 1.9f, true);
  CHECK(!pm.holdPower() && pm.state() == St::Starting);
  CHECK(run(pm, 0.2f, true) == Ev::Latched);
  CHECK(pm.holdPower());
  // The press that switched it on keeps going for 5 s: no switch-off.
  run(pm, 5.0f, true);
  CHECK(pm.state() == St::On);
}

TEST(power_long_press_switches_off_short_press_does_not) {
  PowerManager pm;
  run(pm, 2.5f);  // on, button never pressed (it is armed by the steady release)
  CHECK(pm.state() == St::On);
  run(pm, 1.0f, true);
  run(pm, 0.5f);
  CHECK(pm.state() == St::On);  // 1 s press: nothing
  CHECK(run(pm, 2.05f, true) == Ev::SwitchedOff);
  CHECK(pm.offReason() == PowerOffReason::Button);
  CHECK(pm.holdPower());  // hold drops after the switch-off sound
  run(pm, 0.7f, true);
  CHECK(!pm.holdPower());
  run(pm, 3.0f, true);  // still held (power from the button): stays off
  CHECK(pm.state() == St::Off);
  run(pm, 0.5f);        // released (on real hardware the power is gone now)
  CHECK(run(pm, 0.1f, true) == Ev::Woke);  // on USB power: a new press switches on again
  CHECK(pm.holdPower());
}

TEST(power_auto_off_after_idle_time_and_use_resets_it) {
  PowerManager pm;
  run(pm, 2.5f);
  run(pm, 50.0f, false, false, false, 1.0f);  // 1 min auto-off, 50 s idle
  CHECK(pm.state() == St::On);
  run(pm, 1.0f, false, true, false, 1.0f);    // used
  run(pm, 55.0f, false, false, false, 1.0f);
  CHECK(pm.state() == St::On);
  CHECK(run(pm, 6.0f, false, false, false, 1.0f) == Ev::SwitchedOff);
  CHECK(pm.offReason() == PowerOffReason::Idle);
  CHECK(run(pm, 0.1f, false, true, false, 1.0f) == Ev::Woke);  // a key on USB power wakes it
  // 0 = never.
  PowerManager never;
  run(never, 2.5f);
  run(never, 200.0f, false, false, false, 0.0f);
  CHECK(never.state() == St::On);
}

TEST(power_hour_long_idle_is_counted_without_wrapping) {
  PowerManager pm;
  run(pm, 2.5f);
  for (int i = 0; i < 59; ++i) run(pm, 60.0f);  // 59 minutes
  CHECK(pm.state() == St::On);
  CHECK_NEAR(pm.idleSeconds(), 59.0 * 60.0, 1.0);
  run(pm, 61.0f);
  CHECK(pm.state() == St::Off && pm.offReason() == PowerOffReason::Idle);
}

TEST(power_critical_battery_switches_off_once) {
  PowerManager pm;
  run(pm, 2.5f);
  run(pm, 4.0f, false, false, true);
  CHECK(pm.state() == St::On);  // needs 5 s
  CHECK(run(pm, 1.5f, false, false, true) == Ev::SwitchedOff);
  CHECK(pm.offReason() == PowerOffReason::Battery);
  run(pm, 0.1f, false, true, true);  // still running (USB): woken by use
  run(pm, 20.0f, false, false, true);
  CHECK(pm.state() == St::On);  // not again in the same low-battery episode
  run(pm, 1.0f, false, false, false);
  run(pm, 6.0f, false, false, true);
  CHECK(pm.state() == St::Off);  // a new episode does
}

TEST(power_button_bounce_is_ignored) {
  PowerManager pm;
  run(pm, 2.5f);
  // 3 s of a bouncing contact that is never steady for 20 ms.
  for (int i = 0; i < 6000; ++i) pm.update(kDt, (i / 10) % 2 == 0, false, false, 60.0f);
  CHECK(pm.state() == St::On);
}
