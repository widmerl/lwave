#include "lwave/storage_format.h"
#include <string.h>

namespace lw {

uint32_t crc32(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void packBlob(const CalibrationData& cal, const Settings& s, PersistentBlob& blob) {
  memset(static_cast<void*>(&blob), 0, sizeof(blob));
  blob.magic = kBlobMagic;
  blob.version = kBlobVersion;
  blob.size = (uint16_t)sizeof(PersistentBlob);
  blob.cal = cal;
  blob.settings = s;
  blob.crc = crc32(&blob, offsetof(PersistentBlob, crc));
}

bool unpackBlob(const PersistentBlob& blob, CalibrationData& cal, Settings& s) {
  if (blob.magic != kBlobMagic || blob.version != kBlobVersion || blob.size != sizeof(PersistentBlob)) return false;
  if (crc32(&blob, offsetof(PersistentBlob, crc)) != blob.crc) return false;
  cal = blob.cal;
  s = blob.settings;
  sanitizeSettings(s);
  return true;
}

}  // namespace lw
