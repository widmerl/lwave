#include "lwave/controller.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwave/curves.h"
#include "lwave/hw_map.h"

namespace lw {

namespace {
const char* modeName(Mode m) {
  switch (m) {
    case Mode::Normal: return "normal";
    case Mode::Calibration: return "calibration";
    default: return "debug";
  }
}
}  // namespace

// ---- start-up ----------------------------------------------------------------
void Controller::begin(Platform& platform, Engine& engine) {
  pf_ = &platform;
  engine_ = &engine;
  cal_.setDefaults();
  settings_ = Settings();
  PersistentBlob blob;
  BlobLoad loaded = pf_->loadBlob(blob) ? unpackBlob(blob, cal_, settings_) : kBlobInvalid;
  if (loaded != kBlobInvalid) {
    int k, p, s;
    cal_.counts(k, p, s);
    pf_->out().printf("L-wave %s: loaded calibration (%d/%d keys, %d/%d pots, %d/%d switches)\r\n", kFirmwareVersion,
                      k, kNumKeys, p, kNumPots, s, kNumSwitches);
    if (loaded == kBlobCalibrationOnly)
      pf_->out().print("Stored settings were from an older firmware and are reset to defaults ('save' stores them in the new format).\r\n");
  } else {
    cal_.setDefaults();
    settings_ = Settings();
    pf_->out().printf("L-wave %s: no stored calibration, using defaults\r\n", kFirmwareVersion);
  }
}

void Controller::boot(const ScanFrame& first) {
  bool held[kNumKeys];
  detectHeldKeys(first.keys, cal_, held);
  int calKey = keyIndexByName(kCalibrationKey), dbgKey = keyIndexByName(kDebugKey);
  last_ = first;
  haveFrame_ = true;
  lastFrameT_ = first.timeUs;
  lastLineOut_ = first.lineOut;

  if (held[calKey]) {
    startCalibration();
  } else {
    mode_ = held[dbgKey] ? Mode::Debug : Mode::Normal;
    if (mode_ == Mode::Debug) {
      stream_ = true;
      eventLog_ = true;
      view_ = DebugView::Stats;
      pf_->out().print("Debug mode (D3 held at power-up). Type 'help' for commands.\r\n");
    }
    if (!cal_.complete()) {
      scheduleCue(220.0f, 0.4f, 0.0f);  // low warning tone
      pf_->out().print("Warning: calibration incomplete - hold C4 (key D5) at power-up, or type 'cal'.\r\n");
    }
  }
  // Prime filters and switches with the first frame so nothing jumps.
  processControls(first, 0.001f);
  for (int s = 0; s < kNumSwitches; ++s) lastSwitch_[s] = panel_.sw[s];
  for (int p = 0; p < kNumPots; ++p) potRef_[p] = panel_.pot[p];
  publishParams();
}

// ---- per-frame ---------------------------------------------------------------
void Controller::onFrame(const ScanFrame& f) {
  uint32_t dtUs = haveFrame_ ? (uint32_t)(f.timeUs - lastFrameT_) : 1000u;
  if (dtUs == 0) dtUs = 1;
  float dtS = (float)dtUs * 1e-6f;
  float rate = 1.0f / dtS;
  scanRate_ = scanRate_ <= 0.0f ? rate : scanRate_ + (rate - scanRate_) * 0.01f;
  lastFrameT_ = f.timeUs;
  last_ = f;
  haveFrame_ = true;

  battery_.update(f.battery, dtS, settings_);
  if (f.lineOut != lastLineOut_) {
    lastLineOut_ = f.lineOut;
    if (stream_ || mode_ == Mode::Debug) pf_->out().printf("EV line_out %d\r\n", f.lineOut ? 1 : 0);
  }

  if (mode_ == Mode::Calibration) {
    calibrator_.update(f);
    Cue c[8];
    int n = calibrator_.drainCues(c, 8);
    for (int i = 0; i < n; ++i) scheduleCue(c[i].hz, c[i].seconds, c[i].delayS);
    if (calibrator_.phase() == Calibrator::Phase::Done) finishCalibration();
    else reportCalibration(f.timeUs);
  } else {
    processKeys(f);
    processControls(f, dtS);
    checkPotUse();
    publishParams();
  }

  updatePower(f, dtUs);
  updateCpuGuard(f.timeUs);
  runCues(f.timeUs);
  updateLeds(f.timeUs);
  if (stream_) printDebug(f);
}

void Controller::checkPotUse() {
  bool moved = false;
  for (int p = 0; p < kNumPots && !moved; ++p) moved = fabsf(panel_.pot[p] - potRef_[p]) > kActivityPotStep;
  if (!moved) return;
  used_ = true;
  for (int p = 0; p < kNumPots; ++p) potRef_[p] = panel_.pot[p];
}

void Controller::updatePower(const ScanFrame& f, uint32_t dtUs) {
  const bool used = used_ || mode_ == Mode::Calibration;  // no auto-off during calibration
  used_ = false;
  switch (power_.update(dtUs, f.powerButton, used, battery_.critical(), settings_.autoOffMin)) {
    case PowerManager::Event::Latched:
      pf_->out().print("Power: on (hold output, pin 33, high).\r\n");
      break;
    case PowerManager::Event::SwitchedOff: {
      NoteEvent off;
      off.type = NoteEvent::Type::AllOff;
      engine_->post(off);
      scheduleCue(783.99f, 0.12f, 0.0f);  // falling three notes: switching off
      scheduleCue(523.25f, 0.12f, 0.15f);
      scheduleCue(392.0f, 0.25f, 0.3f);
      if (power_.offReason() == PowerOffReason::Battery)
        pf_->out().printf("Power: switching off (battery critically low, %.2f V).\r\n", battery_.volts());
      else
        pf_->out().printf("Power: switching off (%s).\r\n", powerOffReasonText(power_.offReason()));
      break;
    }
    case PowerManager::Event::Woke:
      pf_->out().print("Power: still running after switching off (USB power, or no power circuit), so on again.\r\n");
      break;
    default:
      break;
  }
  const bool hold = power_.holdPower();
  if (hold != powerHold_ || !powerHoldInit_) {
    powerHold_ = hold;
    powerHoldInit_ = true;
    pf_->setPowerHold(hold);
  }
}

void Controller::updateCpuGuard(uint32_t t) {
  if ((int32_t)(t - nextCpuT_) < 0) return;
  nextCpuT_ = t + 100000u;
  cpu_ = pf_->audioCpuPercent();
  if (cpu_ < 0.0f) return;  // not measurable (simulator, host)
  int cap = voiceCap_;
  int busy = engine_->activeVoices();
  if (cpu_ > kCpuHighPercent && busy > kMinVoiceCap) {
    // Overloaded: drop two voices below what is sounding now (frees ~2 x 1.5-3 %).
    int base = busy < cap ? busy : cap;
    cap = base - 2 < kMinVoiceCap ? kMinVoiceCap : base - 2;
  } else if (cpu_ < kCpuLowPercent && cap < kMaxVoices && (int32_t)(t - capChangeT_) >= 500000) {
    ++cap;
  }
  if (cap == voiceCap_) return;
  if (cap < voiceCap_ && (eventLog_ || stream_ || mode_ == Mode::Debug))
    pf_->out().printf("EV voice_cap %d (cpu %.0f %%)\r\n", cap, cpu_);
  voiceCap_ = cap;
  capChangeT_ = t;
}

void Controller::processKeys(const ScanFrame& f) {
  int octave = octaveShiftSemitones(panel_.sw[static_cast<int>(Switch::Octave)]);
  for (int k = 0; k < kNumKeys; ++k) {
    KeyEvent ev;
    float travel = cal_.keyTravel(k, f.keys[k]);
    if (!keys_.update(k, travel, f.keyTimeUs[k], settings_, ev)) continue;
    NoteEvent ne;
    ne.key = (uint8_t)k;
    ne.depth = ev.depth;
    if (ev.on) {
      // The octave is captured at note-on, so held notes keep their pitch.
      keyNote_[k] = (float)kKeys[k].note + (float)octave;
      used_ = true;
      ne.type = NoteEvent::Type::On;
      ne.note = keyNote_[k];
      ne.velocity = ev.velocity;
    } else {
      ne.type = NoteEvent::Type::Off;
      ne.note = keyNote_[k];
    }
    engine_->post(ne);
    if (eventLog_) logEvent(ev, keyNote_[k]);
  }
}

void Controller::processControls(const ScanFrame& f, float dtS) {
  for (int p = 0; p < kNumPots; ++p)
    panel_.pot[p] = pots_[p].update(cal_.potPosition(p, f.pots[p]), dtS, settings_);
  for (int s = 0; s < kNumSwitches; ++s) {
    panel_.sw[s] = switches_[s].update(s, f.switches[s], f.timeUs, cal_, settings_);
    if (panel_.sw[s] != lastSwitch_[s]) {
      if (eventLog_) pf_->out().printf("EV switch %s %d\r\n", kSwitchNames[s], panel_.sw[s] + 1);
      used_ = true;
      lastSwitch_[s] = panel_.sw[s];
    }
  }
}

void Controller::publishParams() {
  EngineParams& p = engine_->params().writeBuffer();
  mapControls(panel_, settings_, engine_->maxDelaySeconds(), p);
  if (p.voices > voiceCap_) p.voices = voiceCap_;
  for (int k = 0; k < kNumKeys; ++k) p.keyDepth[k] = keys_.depth(k);
  lastParams_ = p;
  engine_->params().publish();
}

// ---- calibration -------------------------------------------------------------
void Controller::startCalibration() {
  mode_ = Mode::Calibration;
  stream_ = false;
  NoteEvent off;
  off.type = NoteEvent::Type::AllOff;
  engine_->post(off);
  calibrator_.start(cal_, pf_->micros());
  calPhaseShown_ = static_cast<int>(Calibrator::Phase::WaitRelease);
  calPrintT_ = pf_->micros();
  pf_->out().print(
      "Calibration: release all keys and keep hands off for 2 s.\r\n"
      "Then press every key fully, turn every dial and slider to both ends,\r\n"
      "and turn every rotary switch through all 8 positions.\r\n"
      "Hold C4 (key D5) for 3 s to finish early.\r\n");
}

void Controller::reportCalibration(uint32_t t) {
  const Calibrator::Phase ph = calibrator_.phase();
  char buf[400];
  if (static_cast<int>(ph) != calPhaseShown_) {
    calPhaseShown_ = static_cast<int>(ph);
    calPrintT_ = t;
    if (ph == Calibrator::Phase::Rest)
      pf_->out().print("Calibration: measuring the keys at rest (2 s), keep hands off.\r\n");
    else if (ph == Calibrator::Phase::Sweep)
      pf_->out().print(
          "Calibration: rest stored. Now press every key fully, turn every dial and slider to both ends,\r\n"
          "and turn every rotary switch slowly through all 8 positions (each new position beeps).\r\n"
          "Progress is printed every 5 s. It saves by itself when everything is done;\r\n"
          "hold C4 (key D5) for 3 s to finish and save early.\r\n");
    return;
  }
  if ((int32_t)(t - calPrintT_) < 5000000) return;
  calPrintT_ = t;
  if (ph == Calibrator::Phase::WaitRelease) {
    calibrator_.describeHeld(buf, sizeof(buf));
    pf_->out().printf("Calibration: waiting for all keys to be released (looks pressed: %s).\r\n", buf);
  } else if (ph == Calibrator::Phase::Sweep) {
    calibrator_.describeMissing(buf, sizeof(buf));
    pf_->out().printf("Calibration: %d/%d keys, %d/%d pots, %d/%d switches. Missing: %s\r\n", calibrator_.keysDone(),
                      kNumKeys, calibrator_.potsDone(), kNumPots, calibrator_.switchesDone(), kNumSwitches, buf);
  }
}

void Controller::finishCalibration() {
  cal_ = calibrator_.result();
  int k, p, s;
  cal_.counts(k, p, s);
  bool ok = save();
  pf_->out().printf("Calibration finished: %d/%d keys, %d/%d pots, %d/%d switches. %s\r\n", k, kNumKeys, p, kNumPots,
                    s, kNumSwitches, ok ? "Saved." : "SAVE FAILED.");
  for (int i = 0; i < kNumSwitches; ++i) {
    if (!cal_.switchValid[i]) continue;
    int minGap = 1 << 30;
    for (int j = 1; j < kSwitchPositions; ++j) {
      int gap = (int)cal_.switchLevel[i][j - 1] - (int)cal_.switchLevel[i][j];
      if (gap < minGap) minGap = gap;
    }
    if (minGap < 300) pf_->out().printf("Note: switch %s has positions only %d counts apart.\r\n", kSwitchNames[i], minGap);
  }
  keys_.reset();
  for (PotFilter& pf : pots_) pf = PotFilter();
  for (SwitchDecoder& sd : switches_) sd = SwitchDecoder();
  mode_ = Mode::Normal;
  processControls(last_, 0.001f);
  for (int p = 0; p < kNumPots; ++p) potRef_[p] = panel_.pot[p];
  publishParams();
}

bool Controller::save() {
  PersistentBlob blob;
  packBlob(cal_, settings_, blob);
  return pf_->saveBlob(blob);
}

// ---- cues and LEDs -----------------------------------------------------------
void Controller::scheduleCue(float hz, float s, float delayS) {
  uint32_t now = pf_->micros();
  for (PendingCue& c : cues_)
    if (!c.used) {
      c = PendingCue{now + (uint32_t)(delayS * 1e6f), hz, s, true};
      return;
    }
}

void Controller::runCues(uint32_t t) {
  for (PendingCue& c : cues_) {
    if (!c.used || (int32_t)(t - c.due) < 0) continue;
    NoteEvent e;
    e.type = NoteEvent::Type::Cue;
    e.cueHz = c.hz;
    e.cueSeconds = c.s;
    engine_->post(e);
    c.used = false;
  }
}

void Controller::updateLeds(uint32_t t) {
  bool want[kNumLeds] = {};
  if (ledTest_ >= 0) {
    if ((uint32_t)(t - ledTestT_) > 300000u) { ++ledTest_; ledTestT_ = t; }
    if (ledTest_ >= kNumLeds) ledTest_ = -1;
    else want[ledTest_] = true;
  } else if (power_.state() == PowerManager::State::Off) {
    // switched off: all LEDs dark (the power is going, or the synth waits on USB power)
  } else if (mode_ == Mode::Calibration) {
    int lit = (int)(calibrator_.progress() * 8.0f + 0.5f);  // progress bar
    for (int i = 0; i < 8; ++i) want[i] = i < lit;
  } else {
    float db = engine_->outputLevelDb();
    for (int i = 0; i < 7; ++i) want[i] = db >= settings_.meterFloorDb + settings_.meterStepDb * (float)i;
    want[7] = db >= -1.0f || engine_->limiterReductionDb() > 0.5f;
  }
  if (ledTest_ < 0 && power_.state() != PowerManager::State::Off) {
    if (battery_.critical()) want[8] = ((t / 250000u) & 1u) != 0;  // blink at 2 Hz
    else want[8] = battery_.low();
  }
  for (int i = 0; i < kNumLeds; ++i)
    if (!ledsInit_ || want[i] != ledState_[i]) {
      ledState_[i] = want[i];
      pf_->setLed(i, want[i]);
    }
  ledsInit_ = true;
}

// ---- debug output ------------------------------------------------------------
void Controller::noteName(float note, char* buf, size_t n) {
  static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int m = (int)lroundf(note);
  snprintf(buf, n, "%s%d", names[((m % 12) + 12) % 12], m / 12 - 1);
}

void Controller::logEvent(const KeyEvent& ev, float note) {
  char nn[16];
  noteName(note, nn, sizeof(nn));
  if (ev.on)
    pf_->out().printf("EV on  %s %-3s vel=%.2f t=%.2fms depth=%.2f\r\n", kKeys[ev.key].name, nn, ev.velocity,
                      ev.travelMs, ev.depth);
  else
    pf_->out().printf("EV off %s %-3s frozen_depth=%.2f\r\n", kKeys[ev.key].name, nn, ev.depth);
}

void Controller::printDebug(const ScanFrame& f) {
  uint32_t t = f.timeUs;
  if ((int32_t)(t - nextDebugT_) < 0) return;
  nextDebugT_ = t + (uint32_t)(1e6f / settings_.debugRateHz);
  TextOut& o = pf_->out();
  const char* sep = csv_ ? "," : " ";
  switch (view_) {
    case DebugView::Raw:
      o.print(csv_ ? "raw" : "RAW K:");
      for (int k = 0; k < kNumKeys; ++k) o.printf("%s%u", sep, f.keys[k]);
      o.print(csv_ ? "" : " | P:");
      for (int p = 0; p < kNumPots; ++p) o.printf("%s%u", sep, f.pots[p]);
      o.print(csv_ ? "" : " | S:");
      for (int s = 0; s < kNumSwitches; ++s) o.printf("%s%u", sep, f.switches[s]);
      o.printf(csv_ ? ",%u,%d\r\n" : " | B: %u | LO: %d\r\n", f.battery, f.lineOut ? 1 : 0);
      break;
    case DebugView::Calibrated:
      o.print(csv_ ? "cal" : "CAL K%:");
      for (int k = 0; k < kNumKeys; ++k) o.printf("%s%d", sep, (int)(100.0f * cal_.keyTravel(k, f.keys[k]) + 0.5f));
      o.print(csv_ ? "" : " | P%:");
      for (int p = 0; p < kNumPots; ++p) o.printf("%s%d", sep, (int)(100.0f * panel_.pot[p] + 0.5f));
      o.print(csv_ ? "" : " | S:");
      for (int s = 0; s < kNumSwitches; ++s) o.printf("%s%d", sep, panel_.sw[s] + 1);
      o.print("\r\n");
      break;
    case DebugView::Keys: {
      o.print("KEYS");
      int shown = 0;
      for (int k = 0; k < kNumKeys; ++k) {
        float tr = cal_.keyTravel(k, f.keys[k]);
        if (tr < 0.02f && !keys_.on(k)) continue;
        o.printf(" %s:%d%%%s", kKeys[k].name, (int)(100.0f * tr + 0.5f), keys_.on(k) ? "*" : "");
        ++shown;
      }
      o.print(shown ? "\r\n" : " (none)\r\n");
      break;
    }
    case DebugView::Pots:
      // One line per pot or switch that changed (0.5 % steps), with its engine value.
      for (int p = 0; p < kNumPots; ++p) {
        int shown = (int)(200.0f * panel_.pot[p] + 0.5f);
        if (shown == potShown_[p]) continue;
        potShown_[p] = shown;
        char val[40];
        formatPotValue(lastParams_, static_cast<Pot>(p), val, sizeof(val));
        o.printf("POT %-12s %5.1f %%  %s\r\n", kPotNames[p], shown * 0.5f, val);
      }
      for (int s = 0; s < kNumSwitches; ++s) {
        if (panel_.sw[s] == switchShown_[s]) continue;
        switchShown_[s] = panel_.sw[s];
        o.printf("SW  %-12s %d\r\n", kSwitchNames[s], panel_.sw[s] + 1);
      }
      break;
    case DebugView::Stats:
      o.printf("STATS mode=%s scan=%.0fHz cpu=%.1f%% voices=%d cap=%d level=%.1fdB limit=%.1fdB batt=%.2fV%s%s line_out=%d"
               " pwr_btn=%d idle=%.1fmin\r\n",
               modeName(mode_), scanRate_, cpu_, engine_->activeVoices(), voiceCap_, engine_->outputLevelDb(),
               engine_->limiterReductionDb(), battery_.volts(), battery_.low() ? " LOW" : "",
               battery_.critical() ? " CRIT" : "", f.lineOut ? 1 : 0, f.powerButton ? 1 : 0, power_.idleSeconds() / 60.0f);
      break;
    case DebugView::Monitor:
      printMonitor(f);
      break;
  }
}

void Controller::printMonitor(const ScanFrame& f) {
  // Fixed-width hex fields, so the browser monitor can parse without guessing.
  static const char hex[] = "0123456789abcdef";
  char line[720];
  int n = 0;
  auto put = [&](const char* s) { while (*s && n < (int)sizeof(line) - 1) line[n++] = *s++; };
  auto hex2 = [&](unsigned v) { line[n++] = hex[(v >> 4) & 15]; line[n++] = hex[v & 15]; };
  auto hex3 = [&](unsigned v) { line[n++] = hex[(v >> 8) & 15]; line[n++] = hex[(v >> 4) & 15]; line[n++] = hex[v & 15]; };
  char tmp[160];
  snprintf(tmp, sizeof(tmp), "MON t=%lu mode=%c k=", (unsigned long)(f.timeUs / 1000u),
           mode_ == Mode::Normal ? 'n' : (mode_ == Mode::Debug ? 'd' : 'c'));
  put(tmp);
  for (int k = 0; k < kNumKeys; ++k) hex2((unsigned)(cal_.keyTravel(k, f.keys[k]) * 255.0f + 0.5f));
  put(" n=");
  for (int k = 0; k < kNumKeys; ++k) line[n++] = keys_.on(k) ? '1' : '0';
  put(" p=");
  for (int p = 0; p < kNumPots; ++p) hex3((unsigned)(panel_.pot[p] * 4095.0f + 0.5f));
  put(" s=");
  for (int s = 0; s < kNumSwitches; ++s) line[n++] = (char)('1' + panel_.sw[s]);
  put(" rk=");
  for (int k = 0; k < kNumKeys; ++k) hex3(f.keys[k]);
  put(" rp=");
  for (int p = 0; p < kNumPots; ++p) hex3(f.pots[p]);
  put(" rs=");
  for (int s = 0; s < kNumSwitches; ++s) hex3(f.switches[s]);
  put(" rb=");
  hex3(f.battery);
  put(" led=");
  for (int i = 0; i < kNumLeds; ++i) line[n++] = ledState_[i] ? '1' : '0';
  line[n] = 0;
  snprintf(tmp, sizeof(tmp), " v=%d lvl=%.1f lim=%.1f cpu=%.1f bat=%.2f lo=%d scan=%.0f\r\n", engine_->activeVoices(),
           engine_->outputLevelDb(), engine_->limiterReductionDb(), cpu_, battery_.volts(),
           f.lineOut ? 1 : 0, scanRate_);
  put(tmp);
  pf_->out().write(line, (size_t)n);
}

// ---- console -----------------------------------------------------------------
void Controller::printHelp() {
  pf_->out().print(
      "Commands:\r\n"
      "  help                 this list\r\n"
      "  info                 firmware, hardware, calibration status\r\n"
      "  cal                  start calibration\r\n"
      "  debug [on|off]       start/stop the debug stream (toggles without argument)\r\n"
      "  r | c | k | s        stream view: raw, calibrated, keys, stats\r\n"
      "  p                    print each pot and switch, then one line whenever one changes\r\n"
      "  m                    machine-readable stream for the browser monitor\r\n"
      "  e                    toggle the event log (note on/off, switches, line out)\r\n"
      "  f                    toggle stream format: table / CSV\r\n"
      "  get [name]           show one or all settings\r\n"
      "  set <name> <value>   change a setting (use 'save' to keep it)\r\n"
      "  save                 store settings and calibration\r\n"
      "  defaults             reset settings to defaults (calibration kept)\r\n"
      "  tone [hz]            1 s test tone (default 440 Hz)\r\n"
      "  leds                 light each LED in turn\r\n"
      "  reboot               restart\r\n");
}

void Controller::printInfo() {
  int k, p, s;
  cal_.counts(k, p, s);
  pf_->out().printf(
      "L-wave firmware %s on %s\r\n"
      "mode=%s sample_rate=%.1f max_delay=%.2fs voices=%d/%d scan=%.0fHz\r\n"
      "calibration: %d/%d keys, %d/%d pots, %d/%d switches\r\n",
      kFirmwareVersion, pf_->describe(), modeName(mode_), engine_->sampleRate(), engine_->maxDelaySeconds(),
      (int)settings_.voices, kMaxVoices, scanRate_, k, kNumKeys, p, kNumPots, s, kNumSwitches);
  const char* st = power_.state() == PowerManager::State::On ? "on" : (power_.state() == PowerManager::State::Off ? "off" : "starting");
  if (settings_.autoOffMin > 0.0f)
    pf_->out().printf("power: %s, unused for %.1f min, switches off after %.0f min unused\r\n", st,
                      power_.idleSeconds() / 60.0f, settings_.autoOffMin);
  else
    pf_->out().printf("power: %s, unused for %.1f min, auto-off disabled\r\n", st, power_.idleSeconds() / 60.0f);
}

void Controller::printSettings() {
  for (int i = 0; i < kNumSettings; ++i)
    pf_->out().printf("  %-20s %10.4g   %s\r\n", kSettingInfos[i].name, settings_.*(kSettingInfos[i].member),
                      kSettingInfos[i].help);
}

void Controller::onLine(const char* line) {
  char buf[96];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char* argv[4] = {};
  int argc = 0;
  for (char* tok = strtok(buf, " \t\r\n"); tok && argc < 4; tok = strtok(nullptr, " \t\r\n")) argv[argc++] = tok;
  if (argc == 0) return;
  used_ = true;  // typing in the console counts as use (no auto-off while debugging)
  const char* cmd = argv[0];
  TextOut& o = pf_->out();

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) printHelp();
  else if (!strcmp(cmd, "info")) printInfo();
  else if (!strcmp(cmd, "cal")) startCalibration();
  else if (!strcmp(cmd, "debug")) {
    if (argc > 1) stream_ = !strcmp(argv[1], "on");
    else stream_ = !stream_;
    if (stream_) eventLog_ = true;
    o.printf("debug stream %s\r\n", stream_ ? "on" : "off");
  } else if (strlen(cmd) == 1 && strchr("rckpsm", cmd[0])) {
    switch (cmd[0]) {
      case 'r': view_ = DebugView::Raw; break;
      case 'c': view_ = DebugView::Calibrated; break;
      case 'k': view_ = DebugView::Keys; break;
      case 'p':
        view_ = DebugView::Pots;
        for (int& v : potShown_) v = -1;  // print every pot once, then only changes
        for (int& v : switchShown_) v = -1;
        break;
      case 'm': view_ = DebugView::Monitor; eventLog_ = true; break;
      default: view_ = DebugView::Stats; break;
    }
    stream_ = true;
  } else if (!strcmp(cmd, "e")) {
    eventLog_ = !eventLog_;
    o.printf("event log %s\r\n", eventLog_ ? "on" : "off");
  } else if (!strcmp(cmd, "f")) {
    csv_ = !csv_;
    o.printf("format %s\r\n", csv_ ? "csv" : "table");
  } else if (!strcmp(cmd, "get")) {
    if (argc < 2) printSettings();
    else if (const SettingInfo* si = findSetting(argv[1])) o.printf("%s = %g\r\n", si->name, settings_.*(si->member));
    else o.printf("unknown setting '%s'\r\n", argv[1]);
  } else if (!strcmp(cmd, "set")) {
    if (argc < 3) o.print("usage: set <name> <value>\r\n");
    else if (setSetting(settings_, argv[1], (float)atof(argv[2])))
      o.printf("%s = %g\r\n", argv[1], settings_.*(findSetting(argv[1])->member));
    else o.printf("unknown setting '%s'\r\n", argv[1]);
  } else if (!strcmp(cmd, "save")) {
    o.print(save() ? "saved\r\n" : "SAVE FAILED\r\n");
  } else if (!strcmp(cmd, "defaults")) {
    settings_ = Settings();
    o.print("settings reset to defaults (not saved)\r\n");
  } else if (!strcmp(cmd, "tone")) {
    float hz = argc > 1 ? (float)atof(argv[1]) : 440.0f;
    if (hz < 20.0f || hz > 20000.0f) hz = 440.0f;
    scheduleCue(hz, 1.0f, 0.0f);
    o.printf("tone %.0f Hz\r\n", hz);
  } else if (!strcmp(cmd, "leds")) {
    ledTest_ = 0;
    ledTestT_ = pf_->micros();
  } else if (!strcmp(cmd, "reboot")) {
    pf_->reboot();
  } else {
    o.printf("unknown command '%s' - type 'help'\r\n", cmd);
  }
}

}  // namespace lw
