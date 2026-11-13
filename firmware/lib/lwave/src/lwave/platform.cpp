#include "lwave/platform.h"
#include <stdio.h>
#include <string.h>

namespace lw {

void TextOut::print(const char* s) { write(s, strlen(s)); }

void TextOut::printf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  write(buf, (size_t)n);
}

}  // namespace lw
