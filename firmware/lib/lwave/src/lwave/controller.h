// L-wave firmware — the controller: everything between the scanner and the
// sound engine. Boot modes, calibration, key and knob processing, LEDs,
// battery and the serial console. Portable; the platform supplies time,
// LEDs, storage and text output.
#pragma once
#include <stdint.h>
#include "lwave/calibration_data.h"
#include "lwave/calibrator.h"
#include "lwave/control_mapper.h"
#include "lwave/engine.h"
#include "lwave/input.h"
#include "lwave/platform.h"
#include "lwave/settings.h"

namespace lw {

/// Operating mode.
enum class Mode : uint8_t { Normal, Calibration, Debug };

/// What the debug stream shows.
enum class DebugView : uint8_t { Raw, Calibrated, Keys, Pots, Stats, Monitor };

/// The controller. Call begin() once, boot() with the first scan, then
/// onFrame() for every scan and onLine() for every console line.
class Controller {
 public:
  /// Loads calibration and settings from storage (defaults if none).
  void begin(Platform& platform, Engine& engine);

  /// Chooses the mode from the keys held in the first scan:
  /// C4 (key D5) → calibration, D3 (key F4) → debug, otherwise normal.
  void boot(const ScanFrame& first);

  /// Processes one complete scan.
  void onFrame(const ScanFrame& f);

  /// Handles one console command line (without the line ending).
  void onLine(const char* line);

  /// Current mode.
  Mode mode() const { return mode_; }
  /// Current settings.
  const Settings& settings() const { return settings_; }
  /// Current calibration.
  const CalibrationData& calibration() const { return cal_; }
  /// Panel state after calibration and filtering.
  const PanelState& panel() const { return panel_; }
  /// Measured full-scan rate in Hz.
  float scanRateHz() const { return scanRate_; }
  /// True if a complete calibration was loaded or made.
  bool calibrated() const { return cal_.complete(); }
  /// True while key k's note is on.
  bool noteOn(int k) const { return keys_.on(k); }
  /// LED state as last set (index 0..8).
  bool led(int i) const { return ledState_[i]; }
  /// The calibrator (for progress display and tests).
  const Calibrator& calibrator() const { return calibrator_; }
  /// Polyphony cap from the CPU guard (kMaxVoices when the CPU load is fine).
  int voiceCap() const { return voiceCap_; }
  /// Audio CPU load in percent (peak of the last 100 ms window), -1 if unknown.
  float cpuPercent() const { return cpu_; }

 private:
  void startCalibration();
  void finishCalibration();
  void processKeys(const ScanFrame& f);
  void processControls(const ScanFrame& f, float dtS);
  void publishParams();
  /// Every 100 ms: reads the audio CPU load and lowers the polyphony cap while
  /// it is above kCpuHighPercent; raises it again while below kCpuLowPercent.
  void updateCpuGuard(uint32_t t);
  void updateLeds(uint32_t t);
  void runCues(uint32_t t);
  void scheduleCue(float hz, float s, float delayS);
  void printDebug(const ScanFrame& f);
  /// One machine-readable MON line for the browser monitor (see docs/REFERENCE.md).
  void printMonitor(const ScanFrame& f);
  void printHelp();
  void printInfo();
  void printSettings();
  bool save();
  void logEvent(const KeyEvent& ev, float note);
  static void noteName(float note, char* buf, size_t n);

  Platform* pf_ = nullptr;
  Engine* engine_ = nullptr;
  Settings settings_;
  CalibrationData cal_{};
  Mode mode_ = Mode::Normal;
  Calibrator calibrator_;
  KeyProcessor keys_;
  PotFilter pots_[kNumPots];
  SwitchDecoder switches_[kNumSwitches];
  BatteryMonitor battery_;
  PanelState panel_;
  float keyNote_[kNumKeys] = {};
  ScanFrame last_{};
  bool haveFrame_ = false;
  uint32_t lastFrameT_ = 0;
  float scanRate_ = 0.0f;
  // debug stream
  bool stream_ = false, csv_ = false, eventLog_ = false;
  DebugView view_ = DebugView::Stats;
  uint32_t nextDebugT_ = 0;
  bool lastLineOut_ = false;
  int lastSwitch_[kNumSwitches] = {};
  // LEDs
  bool ledState_[kNumLeds] = {};
  bool ledsInit_ = false;
  int ledTest_ = -1;
  uint32_t ledTestT_ = 0;
  int potShown_[kNumPots] = {};      // last value printed by the 'p' view (0.1 % steps), -1 = print
  int switchShown_[kNumSwitches] = {};
  EngineParams lastParams_;          // last published parameters (for printing engine values)
  // CPU guard
  float cpu_ = -1.0f;
  int voiceCap_ = kMaxVoices;
  uint32_t nextCpuT_ = 0, capChangeT_ = 0;
  // scheduled cues
  struct PendingCue { uint32_t due; float hz, s; bool used; };
  PendingCue cues_[16] = {};
};

}  // namespace lw
