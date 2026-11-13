#include "lwave/dsp/basics.h"

namespace lw {

float gSineTable[kSineTableSize + 1];

void initDspTables() {
  for (int i = 0; i <= kSineTableSize; ++i)
    gSineTable[i] = (float)sin(2.0 * 3.14159265358979323846 * (double)i / (double)kSineTableSize);
}

}  // namespace lw
