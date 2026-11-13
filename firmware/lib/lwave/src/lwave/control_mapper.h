// L-wave firmware — panel positions → engine parameters, using the curves
// printed on the panel.
#pragma once
#include "lwave/config.h"
#include "lwave/engine_types.h"
#include "lwave/hw_map.h"
#include "lwave/settings.h"

namespace lw {

/// Panel state after calibration and filtering.
struct PanelState {
  float pot[kNumPots] = {};     ///< Positions 0..1, indexed by Pot.
  int sw[kNumSwitches] = {};    ///< Positions 0..7 (panel 1..8), indexed by Switch.
};

/// Fills every panel-derived field of `out` (all fields except keyDepth).
/// maxDelayS is the longest delay the delay buffer allows; the delay-time knob
/// is scaled so its full travel stays usable.
void mapControls(const PanelState& panel, const Settings& s, float maxDelayS, EngineParams& out);

/// Semitone shift for an octave-switch position 0..7 (panel 1..8): position 4 → 0.
int octaveShiftSemitones(int octavePos);

/// Time-knob curve shared by attack, decay, release, LFO period and LFO delay:
/// fully logarithmic, kTimeKnobMinS (10 ms) at CCW, 0.32 s at centre,
/// kTimeKnobMaxS (10 s) at CW.
float timeKnobCurve(float p);

/// Fully logarithmic curve from lo (p = 0) to hi (p = 1).
float curveLogFull(float p, float lo, float hi);

/// Detune curve: 0 c, 10 c at 70 %, 30 c at CW.
float detuneCurve(float p);

/// The engine-unit value pot `pot` drives in `e`: seconds, Hz, cents, Q, 0..1,
/// or dB for the level sliders and master volume. Detune gives cents, or the
/// chorus amount 0..1 when detuneCopies = 1.
float potEngineValue(const EngineParams& e, Pot pot);

/// Writes that value with its unit (e.g. "164 ms", "2.91 kHz", "-6.0 dB") into buf.
void formatPotValue(const EngineParams& e, Pot pot, char* buf, int size);

}  // namespace lw
