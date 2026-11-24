// L-wave firmware — compile-time configuration.
// Everything here is fixed at build time. Values you may want to tune while
// playing live in Settings (settings.h) instead, so they can be changed from
// the serial console without recompiling.
#pragma once
#include <stdint.h>

namespace lw {

/// Nominal audio sample rate in Hz. The Teensy runs at 44117.647 Hz; the engine
/// is told the exact rate at start-up, this value is only a default.
constexpr float kDefaultSampleRate = 44100.0f;

/// Samples per audio block (matches the Teensy Audio library's AUDIO_BLOCK_SAMPLES).
constexpr int kBlockSize = 128;

/// Samples per control-rate step inside the engine. Envelopes, LFOs and filter
/// coefficients are updated once per sub-block; oscillators run every sample.
constexpr int kSubBlock = 16;

/// Voices allocated in memory. The number actually used is the `voices`
/// setting (default 20), which can be set anywhere up to this value.
constexpr int kMaxVoices = 24;

/// Time-knob range (fully logarithmic between the two): attack, decay,
/// release, LFO period, LFO delay and delay time.
constexpr float kTimeKnobMinS = 0.01f;
/// Upper end of the time knobs (the delay time stops earlier without PSRAM).
constexpr float kTimeKnobMaxS = 10.0f;

/// CPU guard: above this audio CPU load the polyphony cap drops by two voices
/// below what is sounding (never below kMinVoiceCap) …
constexpr float kCpuHighPercent = 90.0f;
/// … and below this load it rises again by one voice every 0.5 s.
constexpr float kCpuLowPercent = 70.0f;
/// The CPU guard never caps polyphony below this.
constexpr int kMinVoiceCap = 6;

/// Battery: for this long after power-up the reading is followed directly
/// (the divider's filter capacitor is still charging), then smoothed over ~2 s.
constexpr float kBatteryFastS = 0.5f;
/// Battery: no low or critical warning for this long after power-up.
constexpr float kBatteryWarnDelayS = 3.0f;

/// Number of note keys on the panel.
constexpr int kNumKeys = 48;

/// Number of potentiometers: 22 dials + 8 sliders.
constexpr int kNumPots = 30;

/// Number of 8-position rotary switches.
constexpr int kNumSwitches = 4;

/// Positions per rotary switch.
constexpr int kSwitchPositions = 8;

/// Number of LEDs: 0-7 volume meter, 8 battery warning.
constexpr int kNumLeds = 9;

/// Full-scale value of the 12-bit ADC.
constexpr uint16_t kAdcMax = 4095;

/// Comb filter buffer per voice (samples). Sets the lowest comb pitch:
/// sampleRate / kCombBufferSize, about 43 Hz at 44.1 kHz.
constexpr int kCombBufferSize = 1024;

/// Internal level trim applied to the voice sum. Chosen so one note with one
/// level slider at maximum (+12 dB) and master volume at maximum (+12 dB)
/// peaks just below full scale (about -1 dBFS), as specified.
constexpr float kVoiceSumTrim = 0.0562f;

/// Firmware version string printed by `info`.
constexpr const char* kFirmwareVersion = "0.1.0";

}  // namespace lw
