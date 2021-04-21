#include "rbase.h"
#ifdef DEBUG

const char* debug_tmpsprintf(int buffer, const char* fmt, ...) {
  static char bufs[6][256];
  char* buf = bufs[MIN(6, MAX(0, buffer))];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(bufs[0]), fmt, ap);
  va_end(ap);
  return buf;
}

#endif
