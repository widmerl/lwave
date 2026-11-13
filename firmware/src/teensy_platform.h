// L-wave firmware (Teensy only) — Platform services: USB serial, LEDs,
// EEPROM storage, reboot, audio CPU load, PSRAM detection.
#pragma once
#include <stdint.h>
#include "lwave/platform.h"

/// TextOut on the USB serial port.
class SerialOut : public lw::TextOut {
 public:
  /// Writes to Serial (drops output when no terminal is connected).
  void write(const char* s, size_t n) override;
};

/// The Teensy implementation of lw::Platform.
class TeensyPlatform : public lw::Platform {
 public:
  /// Configures the LED pins.
  void begin();
  uint32_t micros() override;
  void setLed(int index, bool on) override;
  bool loadBlob(lw::PersistentBlob& blob) override;
  bool saveBlob(const lw::PersistentBlob& blob) override;
  void reboot() override;
  float audioCpuPercent() override;
  const char* describe() override;
  lw::TextOut& out() override { return out_; }

  /// Size of the external PSRAM in MB (0 if none is fitted).
  static uint8_t psramMegabytes();

 private:
  SerialOut out_;
  char desc_[64] = {};
};
