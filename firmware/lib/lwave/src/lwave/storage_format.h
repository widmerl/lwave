// L-wave firmware — the byte layout saved in EEPROM: calibration + settings,
// with a magic number, version and CRC-32.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lwave/calibration_data.h"
#include "lwave/settings.h"

namespace lw {

/// Everything that survives a power cycle.
struct PersistentBlob {
  uint32_t magic;            ///< kBlobMagic.
  uint16_t version;          ///< kBlobVersion.
  uint16_t size;             ///< sizeof(PersistentBlob).
  CalibrationData cal;       ///< Calibration.
  Settings settings;         ///< Settings.
  uint32_t crc;              ///< CRC-32 of all bytes before this field.
};

/// "LWV1" in little-endian.
constexpr uint32_t kBlobMagic = 0x3156574Cu;
/// Increase when CalibrationData or Settings change layout; old blobs are then ignored.
constexpr uint16_t kBlobVersion = 4;

/// Standard CRC-32 (IEEE, as used by zip).
uint32_t crc32(const void* data, size_t len);

/// Fills `blob` from calibration and settings, including the CRC.
void packBlob(const CalibrationData& cal, const Settings& s, PersistentBlob& blob);

/// Checks magic, version, size and CRC; on success copies the contents out
/// (settings are clamped to their valid ranges). Returns false if invalid.
bool unpackBlob(const PersistentBlob& blob, CalibrationData& cal, Settings& s);

}  // namespace lw
