// L-wave firmware (Teensy only) — reads the five muxes, the direct switch
// pins, the battery and the line-out detect into a ScanFrame.
#pragma once
#include <stdint.h>
#include "lwave/hw_map.h"
#include "lwave/input.h"

/// Mux scanner. One scan = 16 select steps × 5 mux reads + 3 direct reads.
class TeensyScanner {
 public:
  /// Configures ADC resolution, select pins and the routing table.
  void begin();
  /// Performs one complete scan into f (keys get a timestamp per select step).
  void scan(lw::ScanFrame& f);

  /// Settling time after changing the select lines, in nanoseconds.
  static constexpr uint32_t kSettleNs = 1500;

 private:
  enum Kind : uint8_t { None, Key, Pot, Switch };
  struct Route { Kind kind; uint8_t index; };
  Route route_[lw::kNumMuxes][16] = {};
};
