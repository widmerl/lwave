// L-wave firmware — tunable settings.
// These are the values you are expected to experiment with on the hardware.
// All are floats so one console command (`set <name> <value>`) handles every
// one of them; they are saved together with the calibration (`save`).
#pragma once
#include <stdint.h>

namespace lw {

/// Every tunable value. Stored by console name (see storage_format.h), so adding
/// a setting keeps the others and the calibration.
struct Settings {
  // Keys (fractions of full key travel, 0 = rest, 1 = bottomed out)
  float keyVelStart = 0.10f;   ///< Travel where velocity timing starts.
  float keyNoteOn = 0.30f;     ///< Travel where the note starts.
  float keyNoteOff = 0.10f;    ///< Travel below which the note is released.
  float velFastMs = 1.0f;      ///< Travel time (start → note-on) that counts as the hardest strike.
  float velSlowMs = 40.0f;     ///< Travel time that counts as the softest strike.
  float velRangeDb = 24.0f;    ///< How much quieter the softest strike is than the hardest.
  float velAttackSpan = 4.0f;  ///< Attack-time factor where the Velocity knob gives speed full say over the attack (33-66 %): softest ×span, hardest ÷span.
  float swellCurve = 2.0f;     ///< Velocity knob 100 %: level = (travel past the note-on point)^curve. 1 = linear, 2 = more even in loudness.
  float depthMode = 1.0f;      ///< Key depth: 0 = ignored, 1 = scales the note level, 2 = scales sustain only.
  float depthFreezeMs = 20.0f; ///< Key depth = highest travel within this window, so lifting a key does not dip the level; frozen at release.
  // Voices
  float voices = 20.0f;        ///< Polyphony in use (1 .. kMaxVoices). Lower it if the cpu figure in the stats view gets high.
  // Pots and switches
  float potSmoothMs = 10.0f;   ///< Smoothing time of pot readings.
  float potDeadband = 0.002f;  ///< Change (0-1) a pot must make before its value moves.
  float switchDebounceMs = 20.0f; ///< A switch position must be stable this long to count.
  // Battery
  float battDivider = 2.0f;    ///< Battery voltage / voltage at pin 15. ASSUMED 2.0 until the divider is confirmed.
  float battLowV = 3.4f;       ///< LED 8 lights below this battery voltage.
  float battCritV = 3.2f;      ///< LED 8 blinks below this battery voltage.
  // Volume meter
  float meterFloorDb = -42.0f; ///< Output level that lights LED 0 (dBFS).
  float meterStepDb = 6.0f;    ///< Level step between meter LEDs 0..6; LED 7 lights at -1 dBFS or when limiting.
  float meterFallDbS = 40.0f;  ///< Meter fall speed in dB per second (40 = one 6 dB LED every 0.15 s).
  // Sound
  float filterEnvOct = 6.0f;   ///< Octaves the filter envelope opens at knob 100 %.
  float detuneCopies = 2.0f;   ///< Oscillator copies: 3 = centre + one each side (± half the detune), 2 = main + one a full detune higher, 1 = no copies, the detune knob drives the chorus.
  float detuneSidesDb = -6.0f; ///< With 3 copies: level of each side copy relative to the centre.
  float detuneLowC = 23.17f;   ///< With 3 copies: low copy below the centre at full detune knob (cents).
  float detuneHighC = 37.5f;   ///< With 3 copies: high copy above the centre at full detune knob (cents). 37.5 / 23.17 = golden ratio, so the three beat rates never line up.
  float detuneUpperC = 75.0f;  ///< With 2 copies: upper copy above the main one at full detune knob (cents).
  float detuneUpperDb = -6.0f; ///< With 2 copies: level of the upper copy relative to the main one.
  float oscRandomPhase = 0.0f; ///< 1 = oscillators start at random phases on every note (like free-running analog); 0 = all start together.
  float hpVolume = 0.6f;       ///< SGTL5000 headphone volume (0..1), fixed; the master slider works digitally.
  // Power
  float autoOffMin = 60.0f;    ///< Switch off after this many minutes without use (keys, pots, switches, console); 0 = never.
  // Debug
  float debugRateHz = 50.0f;   ///< Debug stream update rate.
};

/// Description of one setting for the console.
struct SettingInfo {
  const char* name;          ///< Console name, e.g. "key_note_on".
  float Settings::*member;   ///< The field it controls.
  float minValue;            ///< Smallest accepted value.
  float maxValue;            ///< Largest accepted value.
  const char* help;          ///< One-line description.
};

/// Table of all settings (defined in settings.cpp).
extern const SettingInfo kSettingInfos[];
/// Number of entries in kSettingInfos.
extern const int kNumSettings;

/// Finds a setting by console name; nullptr if unknown.
const SettingInfo* findSetting(const char* name);

/// Sets a value, clamped to its range. Returns false if the name is unknown.
bool setSetting(Settings& s, const char* name, float value);

/// Clamps every field of `s` to its valid range (used after loading from storage).
void sanitizeSettings(Settings& s);

}  // namespace lw
