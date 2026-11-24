// L-wave firmware — the guided calibration routine (hold C4 at power-up).
#pragma once
#include <stdint.h>
#include "lwave/calibration_data.h"
#include "lwave/config.h"
#include "lwave/input.h"

namespace lw {

/// An audio cue requested by the calibrator (played by the engine's beeper).
struct Cue {
  float hz;       ///< Frequency.
  float seconds;  ///< Length.
  float delayS;   ///< Start this long after the request (for chimes and chords).
};

/// Walks through calibration:
///  1. WaitRelease: waits until all keys look released (including the one held at boot).
///  2. Rest (2 s): records each key's rest value and noise.
///  3. Sweep: records each key's extreme, each pot's ends and each switch's
///     8 voltages. Keys and pots tick once when complete; a switch ticks for
///     every new position (higher pitch for higher positions) and chimes when
///     all 8 are found. A switch position counts once its reading stays within
///     kSwitchJitter counts for kSwitchStableUs and lies within
///     kSwitchNominalTolerance of the nominal ladder voltage (8 - position) / 7
///     of full scale, so readings between detents are ignored.
///  4. Done: when everything is complete, or after C4 is held for 3 s.
class Calibrator {
 public:
  /// Calibration phase.
  enum class Phase : uint8_t { WaitRelease, Rest, Sweep, Done };

  /// Starts a new calibration. `previous` supplies values for controls that end up not calibrated.
  void start(const CalibrationData& previous, uint32_t tUs);

  /// Feeds one scan frame.
  void update(const ScanFrame& f);

  /// Current phase.
  Phase phase() const { return phase_; }

  /// Copies out pending audio cues (up to max); returns how many.
  int drainCues(Cue* out, int max);

  /// The result: previous values overwritten by every completed control.
  const CalibrationData& result() const { return result_; }

  /// Number of keys completed.
  int keysDone() const;
  /// Number of pots completed.
  int potsDone() const;
  /// Number of switches completed.
  int switchesDone() const;
  /// Fraction 0..1 of all controls completed.
  float progress() const;
  /// True if key k is complete.
  bool keyDone(int k) const { return keyDone_[k]; }
  /// True if pot p is complete.
  bool potDone(int p) const { return potDone_[p]; }
  /// True if switch s is complete.
  bool switchDone(int s) const { return switchDone_[s]; }
  /// Number of distinct positions seen so far on switch s.
  int switchLevelsSeen(int s) const { return switchCount_[s]; }
  /// True once position i (0 = position 1) of switch s has been recorded.
  bool switchPositionSeen(int s, int i) const { return switchSeen_[s][i]; }
  /// Writes what is still missing during the sweep, for the serial console,
  /// e.g. "keys A3 F7; pots flt_res (12-3410); switches octave missing 3 7".
  /// Pots show the lowest and highest reading so far; each must come within
  /// kPotRailMargin of 0 and of full scale.
  void describeMissing(char* buf, int size) const;
  /// Writes the keys that currently look pressed (while waiting for release).
  void describeHeld(char* buf, int size) const;

  /// Minimum key span (counts) between rest and bottom to accept a key.
  static constexpr int kMinKeySpan = 400;
  /// A pot must come this close (counts) to each rail to be accepted.
  static constexpr int kPotRailMargin = 400;
  /// A switch reading must stay within this many counts ...
  static constexpr int kSwitchJitter = 40;
  /// ... for this long (µs) to count as a position.
  static constexpr uint32_t kSwitchStableUs = 60000;
  /// A position's reading must be this close (counts) to its nominal ladder voltage.
  static constexpr int kSwitchNominalTolerance = 250;

 private:
  void pushCue(float hz, float s, float delayS);
  void finish();

  Phase phase_ = Phase::Done;
  CalibrationData result_{};
  uint32_t phaseStart_ = 0;
  // rest
  uint32_t restSum_[kNumKeys] = {};
  uint32_t restCount_ = 0;
  uint16_t restMin_[kNumKeys] = {}, restMax_[kNumKeys] = {};
  uint16_t rest_[kNumKeys] = {};
  // sweep
  uint16_t keyExtreme_[kNumKeys] = {};
  bool keyDone_[kNumKeys] = {};
  uint16_t potMin_[kNumPots] = {}, potMax_[kNumPots] = {};
  bool potDone_[kNumPots] = {};
  uint16_t switchLevels_[kNumSwitches][kSwitchPositions] = {};  // indexed by position (0 = position 1)
  bool switchSeen_[kNumSwitches][kSwitchPositions] = {};
  int switchCount_[kNumSwitches] = {};
  uint16_t switchAnchor_[kNumSwitches] = {};       // reading at the start of the current steady stretch
  uint32_t switchSince_[kNumSwitches] = {};        // time that stretch started
  uint32_t switchSum_[kNumSwitches] = {}, switchN_[kNumSwitches] = {};
  bool switchChecked_[kNumSwitches] = {};          // this stretch was already evaluated
  bool switchDone_[kNumSwitches] = {};
  bool held_[kNumKeys] = {};                       // keys that looked pressed in the last WaitRelease frame
  int calKey_ = 0;
  uint32_t calKeyHeldSince_ = 0;
  bool calKeyHeld_ = false;
  // cues
  Cue cues_[16];
  int cueCount_ = 0;
};

}  // namespace lw
