#include "teensy_scanner.h"
#include <Arduino.h>

using namespace lw;

void TeensyScanner::begin() {
  analogReadResolution(12);
  analogReadAveraging(1);
  for (uint8_t pin : kMuxSelectPins) {
    pinMode(pin, OUTPUT);
    digitalWriteFast(pin, LOW);
  }
  pinMode(kLineOutDetectPin, INPUT);
  // Power button: high = pressed. The pull-down keeps it released while the
  // button is not fitted (first hardware: pin 32 unconnected).
  pinMode(kPowerButtonPin, INPUT_PULLDOWN);
  for (int m = 0; m < kNumMuxes; ++m)
    for (int ch = 0; ch < 16; ++ch) {
      Route r{None, 0};
      int i;
      if ((i = keyIndexAt(m, ch)) >= 0) r = Route{Key, (uint8_t)i};
      else if ((i = potIndexAt(m, ch)) >= 0) r = Route{Pot, (uint8_t)i};
      else if ((i = switchIndexAt(m, ch)) >= 0) r = Route{Switch, (uint8_t)i};
      route_[m][ch] = r;
    }
}

void TeensyScanner::scan(ScanFrame& f) {
  for (int ch = 0; ch < 16; ++ch) {
    digitalWriteFast(kMuxSelectPins[0], (ch & 1) ? HIGH : LOW);
    digitalWriteFast(kMuxSelectPins[1], (ch & 2) ? HIGH : LOW);
    digitalWriteFast(kMuxSelectPins[2], (ch & 4) ? HIGH : LOW);
    digitalWriteFast(kMuxSelectPins[3], (ch & 8) ? HIGH : LOW);
    delayNanoseconds(kSettleNs);
    uint32_t t = micros();
    for (int m = 0; m < kNumMuxes; ++m) {
      uint16_t v = (uint16_t)analogRead(kMuxPins[m]);
      const Route& r = route_[m][ch];
      switch (r.kind) {
        case Key: f.keys[r.index] = v; f.keyTimeUs[r.index] = t; break;
        case Pot: f.pots[r.index] = v; break;
        case Switch: f.switches[r.index] = v; break;
        default: break;
      }
    }
  }
  for (int s = 0; s < kNumSwitches; ++s)
    if (kSwitchSources[s].mux == kDirect) f.switches[s] = (uint16_t)analogRead(kSwitchSources[s].channel);
  f.battery = (uint16_t)analogRead(kBatteryPin);
  f.lineOut = digitalReadFast(kLineOutDetectPin) != 0;
  f.powerButton = digitalReadFast(kPowerButtonPin) != 0;
  f.timeUs = micros();
}
