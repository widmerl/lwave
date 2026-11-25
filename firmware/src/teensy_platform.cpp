#include "teensy_platform.h"
#include <Arduino.h>
#include <AudioStream.h>
#include <EEPROM.h>
#include <stdio.h>
#include "lwave/hw_map.h"

extern "C" uint8_t external_psram_size;  // set by the Teensy 4.1 start-up code (MB)

void SerialOut::write(const char* s, size_t n) {
  if (Serial) Serial.write(reinterpret_cast<const uint8_t*>(s), n);
}

void TeensyPlatform::begin() {
  for (uint8_t pin : lw::kLedPins) {
    pinMode(pin, OUTPUT);
    digitalWriteFast(pin, LOW);
  }
  // Power hold: low until the controller latches the power on (2 s after start).
  pinMode(lw::kPowerHoldPin, OUTPUT);
  digitalWriteFast(lw::kPowerHoldPin, LOW);
}

void TeensyPlatform::setPowerHold(bool on) { digitalWriteFast(lw::kPowerHoldPin, on ? HIGH : LOW); }

uint32_t TeensyPlatform::micros() { return ::micros(); }

void TeensyPlatform::setLed(int index, bool on) {
  // LEDs are wired pin → LED → GND: high = on.
  if (index >= 0 && index < lw::kNumLeds) digitalWriteFast(lw::kLedPins[index], on ? HIGH : LOW);
}

bool TeensyPlatform::loadBlob(lw::PersistentBlob& blob) {
  uint8_t* p = reinterpret_cast<uint8_t*>(&blob);
  for (size_t i = 0; i < sizeof(blob); ++i) p[i] = EEPROM.read((int)i);
  return true;
}

bool TeensyPlatform::saveBlob(const lw::PersistentBlob& blob) {
  if (sizeof(blob) > (size_t)EEPROM.length()) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&blob);
  for (size_t i = 0; i < sizeof(blob); ++i) EEPROM.update((int)i, p[i]);
  // Read back to confirm.
  for (size_t i = 0; i < sizeof(blob); ++i)
    if (EEPROM.read((int)i) != p[i]) return false;
  return true;
}

void TeensyPlatform::reboot() {
  Serial.flush();
  delay(50);
  SCB_AIRCR = 0x05FA0004;  // system reset request
}

float TeensyPlatform::audioCpuPercent() {
  float v = AudioProcessorUsageMax();
  AudioProcessorUsageMaxReset();
  return v;
}

const char* TeensyPlatform::describe() {
  snprintf(desc_, sizeof(desc_), "Teensy 4.1 @ %lu MHz, PSRAM %u MB", (unsigned long)(F_CPU_ACTUAL / 1000000),
           (unsigned)psramMegabytes());
  return desc_;
}

uint8_t TeensyPlatform::psramMegabytes() { return external_psram_size; }
