#include "rbase.h"
#include "unicode.h"

Rune utf8decode(const u8* buf, size_t len, u32* out_width) {
  u8 b = *buf;
  if (b < RuneSelf) {
    *out_width = 1;
    return b;
  }
  if ((b >> 5) == 0x6) {
    *out_width = 2;
    return len < 2 ? RuneErr
                   : ((b << 6) & 0x7ff) +
                     ((buf[1]) & 0x3f);
  } else if ((b >> 4) == 0xE) {
    *out_width = 3;
    return len < 3 ? RuneErr
                  : ((b << 12) & 0xffff) +
                    ((buf[1] << 6) & 0xfff) +
                    ((buf[2]) & 0x3f);
  } else if ((b >> 3) == 0x1E) {
    *out_width = 4;
    return len < 4 ? RuneErr
                   : ((b << 18) & 0x1fffff) +
                     ((buf[1] << 12) & 0x3ffff) +
                     ((buf[2] << 6) & 0xfff) +
                     ((buf[3]) & 0x3f);
  }
  *out_width = 1;
  return RuneErr;
}
