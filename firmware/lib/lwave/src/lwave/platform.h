// L-wave firmware — what the controller needs from the hardware (or from a
// simulator / test harness). The Teensy implementation is in src/teensy_platform.*.
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "lwave/storage_format.h"

namespace lw {

/// Text output (USB serial on the Teensy, stdout on a PC).
class TextOut {
 public:
  virtual ~TextOut() = default;
  /// Writes n bytes.
  virtual void write(const char* s, size_t n) = 0;
  /// Writes a C string.
  void print(const char* s);
  /// printf-style formatted output (lines up to 511 characters).
  void printf(const char* fmt, ...)
#if defined(__GNUC__)
      __attribute__((format(printf, 2, 3)))
#endif
      ;
};

/// Hardware services used by the Controller.
class Platform {
 public:
  virtual ~Platform() = default;
  /// Microseconds since start (wraps after ~71 minutes; all users subtract).
  virtual uint32_t micros() = 0;
  /// Switches LED `index` (0..8) on or off.
  virtual void setLed(int index, bool on) = 0;
  /// Reads the stored blob. False if storage could not be read (contents are checked by the caller).
  virtual bool loadBlob(PersistentBlob& blob) = 0;
  /// Writes the blob. False on failure.
  virtual bool saveBlob(const PersistentBlob& blob) = 0;
  /// Restarts the processor.
  virtual void reboot() = 0;
  /// Audio CPU load in percent (maximum since last call), or -1 if unknown.
  virtual float audioCpuPercent() = 0;
  /// One line describing the hardware (e.g. "Teensy 4.1, PSRAM 8 MB").
  virtual const char* describe() = 0;
  /// Console output.
  virtual TextOut& out() = 0;
};

}  // namespace lw
