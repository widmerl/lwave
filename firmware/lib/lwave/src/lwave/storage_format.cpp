#include "lwave/storage_format.h"
#include <string.h>

namespace lw {

static_assert(offsetof(PersistentBlob, cal) == 8, "calibration must stay at offset 8 (older blobs are read there)");

uint32_t crc32(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

uint32_t settingNameHash(const char* name) {
  uint32_t h = 2166136261u;
  for (; *name; ++name) {
    h ^= (uint8_t)*name;
    h *= 16777619u;
  }
  return h;
}

void packBlob(const CalibrationData& cal, const Settings& s, PersistentBlob& blob) {
  memset(static_cast<void*>(&blob), 0, sizeof(blob));
  blob.magic = kBlobMagic;
  blob.version = kBlobVersion;
  blob.size = (uint16_t)sizeof(PersistentBlob);
  blob.cal = cal;
  int n = kNumSettings < kMaxStoredSettings ? kNumSettings : kMaxStoredSettings;
  for (int i = 0; i < n; ++i) {
    blob.settings[i].nameHash = settingNameHash(kSettingInfos[i].name);
    blob.settings[i].value = s.*(kSettingInfos[i].member);
  }
  blob.settingCount = (uint16_t)n;
  blob.crc = crc32(&blob, offsetof(PersistentBlob, crc));
}

BlobLoad unpackBlob(const PersistentBlob& blob, CalibrationData& cal, Settings& s) {
  if (blob.magic != kBlobMagic) return kBlobInvalid;
  if (blob.version == kBlobVersion) {
    if (blob.size != sizeof(PersistentBlob)) return kBlobInvalid;
    if (crc32(&blob, offsetof(PersistentBlob, crc)) != blob.crc) return kBlobInvalid;
    cal = blob.cal;
    s = Settings();
    int n = blob.settingCount < kMaxStoredSettings ? blob.settingCount : kMaxStoredSettings;
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < kNumSettings; ++i)
        if (settingNameHash(kSettingInfos[i].name) == blob.settings[j].nameHash) {
          s.*(kSettingInfos[i].member) = blob.settings[j].value;
          break;
        }
    sanitizeSettings(s);
    return kBlobOk;
  }
  // Versions 1-4: header, calibration at offset 8, the old raw settings, CRC in
  // the last 4 bytes of `size`. Keep the calibration, reset the settings.
  if (blob.version >= 1 && blob.version < kBlobVersion) {
    const size_t minSize = offsetof(PersistentBlob, cal) + sizeof(CalibrationData) + 4;
    if (blob.size < minSize || blob.size > sizeof(PersistentBlob)) return kBlobInvalid;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&blob);
    uint32_t stored;
    memcpy(&stored, raw + blob.size - 4, 4);
    if (crc32(raw, blob.size - 4) != stored) return kBlobInvalid;
    cal = blob.cal;
    s = Settings();
    return kBlobCalibrationOnly;
  }
  return kBlobInvalid;
}

}  // namespace lw
