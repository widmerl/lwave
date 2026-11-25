// L-wave firmware — the byte layout saved in EEPROM: calibration + settings,
// with a magic number, version and CRC-32.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lwave/calibration_data.h"
#include "lwave/settings.h"

namespace lw {

/// One stored setting: a hash of its console name and its value.
struct StoredSetting {
  uint32_t nameHash;  ///< settingNameHash() of the console name.
  float value;        ///< Value.
};

/// Most settings the blob can hold.
constexpr int kMaxStoredSettings = 64;

/// Everything that survives a power cycle. Settings are stored by name, so a
/// firmware with new or removed settings keeps the others and the calibration.
struct PersistentBlob {
  uint32_t magic;            ///< kBlobMagic.
  uint16_t version;          ///< kBlobVersion.
  uint16_t size;             ///< sizeof(PersistentBlob).
  CalibrationData cal;       ///< Calibration (same place in every version).
  uint16_t settingCount;     ///< Entries used in settings[].
  uint16_t reserved;         ///< Zero.
  StoredSetting settings[kMaxStoredSettings];  ///< Settings by name hash.
  uint32_t crc;              ///< CRC-32 of all bytes before this field.
};

/// "LWV1" in little-endian.
constexpr uint32_t kBlobMagic = 0x3156574Cu;
/// Increase when the blob layout changes. Older blobs (versions 1-4 stored the
/// settings as a raw struct) still give their calibration; settings then reset.
constexpr uint16_t kBlobVersion = 5;

/// Result of unpackBlob().
enum BlobLoad : uint8_t {
  kBlobInvalid = 0,       ///< Nothing usable (empty or corrupt storage).
  kBlobOk = 1,            ///< Calibration and settings loaded.
  kBlobCalibrationOnly = 2,  ///< Older layout: calibration loaded, settings are defaults.
};

/// 32-bit FNV-1a hash of a setting's console name.
uint32_t settingNameHash(const char* name);

/// Standard CRC-32 (IEEE, as used by zip).
uint32_t crc32(const void* data, size_t len);

/// Fills `blob` from calibration and settings, including the CRC.
void packBlob(const CalibrationData& cal, const Settings& s, PersistentBlob& blob);

/// Checks magic, version, size and CRC; on success copies the contents out.
/// Settings not in the blob keep their defaults; all are clamped to their
/// valid ranges. A blob from versions 1-4 gives only its calibration.
BlobLoad unpackBlob(const PersistentBlob& blob, CalibrationData& cal, Settings& s);

}  // namespace lw
