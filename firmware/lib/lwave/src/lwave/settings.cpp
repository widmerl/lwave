#include "lwave/settings.h"
#include <string.h>
#include "lwave/config.h"

namespace lw {

const SettingInfo kSettingInfos[] = {
    {"key_vel_start", &Settings::keyVelStart, 0.01f, 0.9f, "travel where velocity timing starts"},
    {"key_note_on", &Settings::keyNoteOn, 0.05f, 0.95f, "travel where the note starts"},
    {"key_note_off", &Settings::keyNoteOff, 0.01f, 0.9f, "travel below which the note releases"},
    {"vel_fast_ms", &Settings::velFastMs, 0.1f, 50.0f, "travel time of the hardest strike (ms)"},
    {"vel_slow_ms", &Settings::velSlowMs, 1.0f, 500.0f, "travel time of the softest strike (ms)"},
    {"vel_range_db", &Settings::velRangeDb, 0.0f, 60.0f, "softest strike is this much quieter (dB)"},
    {"vel_attack_span", &Settings::velAttackSpan, 1.0f, 16.0f, "attack factor from strike speed (Velocity knob 33-66 %)"},
    {"swell_curve", &Settings::swellCurve, 0.25f, 4.0f, "Velocity knob 100 %: level = key travel ^ this"},
    {"swell_note_on", &Settings::swellNoteOn, 0.03f, 0.95f, "Velocity knob 100 %: travel where the note starts"},
    {"depth_mode", &Settings::depthMode, 0.0f, 2.0f, "key depth: 0 off, 1 level, 2 sustain only"},
    {"depth_freeze_ms", &Settings::depthFreezeMs, 0.0f, 60.0f, "key depth = highest travel in this window (ms)"},
    {"voices", &Settings::voices, 1.0f, (float)kMaxVoices, "polyphony in use"},
    {"pot_smooth_ms", &Settings::potSmoothMs, 0.0f, 200.0f, "pot smoothing time (ms)"},
    {"pot_deadband", &Settings::potDeadband, 0.0f, 0.05f, "pot dead band (0-1)"},
    {"switch_debounce_ms", &Settings::switchDebounceMs, 1.0f, 200.0f, "switch debounce time (ms)"},
    {"batt_div", &Settings::battDivider, 1.0f, 10.0f, "battery voltage divider ratio"},
    {"batt_low_v", &Settings::battLowV, 2.5f, 4.2f, "LED 8 on below this voltage"},
    {"batt_crit_v", &Settings::battCritV, 2.5f, 4.2f, "LED 8 blinks below this voltage"},
    {"meter_floor_db", &Settings::meterFloorDb, -90.0f, -6.0f, "level that lights meter LED 0 (dBFS)"},
    {"meter_step_db", &Settings::meterStepDb, 1.0f, 12.0f, "dB between meter LEDs"},
    {"meter_fall_db_s", &Settings::meterFallDbS, 5.0f, 300.0f, "meter fall speed (dB per second)"},
    {"filter_env_oct", &Settings::filterEnvOct, 0.0f, 10.0f, "octaves the filter envelope opens"},
    {"detune_copies", &Settings::detuneCopies, 1.0f, 3.0f, "3 centre+sides, 2 main+upper, 1 chorus"},
    {"detune_sides_db", &Settings::detuneSidesDb, -30.0f, 0.0f, "3 copies: side level vs centre (dB)"},
    {"detune_upper_db", &Settings::detuneUpperDb, -30.0f, 0.0f, "2 copies: upper level vs main (dB)"},
    {"detune_low_c", &Settings::detuneLowC, 0.0f, 300.0f, "3 copies: low copy at full knob (cents below)"},
    {"detune_high_c", &Settings::detuneHighC, 0.0f, 300.0f, "3 copies: high copy at full knob (cents above)"},
    {"detune_upper_c", &Settings::detuneUpperC, 0.0f, 300.0f, "2 copies: upper copy at full knob (cents above)"},
    {"osc_random_phase", &Settings::oscRandomPhase, 0.0f, 1.0f, "1 = random oscillator start phase per note"},
    {"hp_volume", &Settings::hpVolume, 0.0f, 1.0f, "codec headphone volume (0-1)"},
    {"auto_off_min", &Settings::autoOffMin, 0.0f, 1440.0f, "switch off after this many minutes unused (0 = never)"},
    {"debug_rate_hz", &Settings::debugRateHz, 1.0f, 200.0f, "debug stream rate (Hz)"},
};
const int kNumSettings = sizeof(kSettingInfos) / sizeof(kSettingInfos[0]);

const SettingInfo* findSetting(const char* name) {
  for (int i = 0; i < kNumSettings; ++i)
    if (strcmp(kSettingInfos[i].name, name) == 0) return &kSettingInfos[i];
  return nullptr;
}

bool setSetting(Settings& s, const char* name, float value) {
  const SettingInfo* info = findSetting(name);
  if (!info) return false;
  if (!(value == value)) value = info->minValue;  // NaN guard
  if (value < info->minValue) value = info->minValue;
  if (value > info->maxValue) value = info->maxValue;
  s.*(info->member) = value;
  return true;
}

void sanitizeSettings(Settings& s) {
  for (int i = 0; i < kNumSettings; ++i) {
    const SettingInfo& info = kSettingInfos[i];
    float v = s.*(info.member);
    if (!(v == v)) v = Settings().*(info.member);
    if (v < info.minValue) v = info.minValue;
    if (v > info.maxValue) v = info.maxValue;
    s.*(info.member) = v;
  }
}

}  // namespace lw
