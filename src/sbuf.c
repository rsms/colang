#include "coimpl.h"
#include "sbuf.h"
#include "unicode.h"
#include "str.h" // strfmtu64

#ifdef CO_WITH_LIBC
  #include <stdio.h>
#endif


void sbuf_append(SBuf* s, const char* p, usize len) {
  usize z = MIN(len, SBUF_AVAIL(s));
  memcpy(s->p, p, z);
  s->p += z;
  if (check_add_overflow(s->len, len, &s->len))
    s->len = USIZE_MAX;
}


void sbuf_appendu32(SBuf* s, u32 v, u32 base) {
  char buf[32];
  u32 n = strfmt_u32(buf, v, base);
  return sbuf_append(s, buf, n);
}


void sbuf_appendrepr(SBuf* s, const char* srcp, usize len) {
  static const char* hexchars = "0123456789abcdef";

  char* p = s->p;
  char* lastp = s->lastp;
  usize nwrite = 0;

  for (usize i = 0; i < len; i++) {
    char c = *srcp++;
    switch (c) {
      // \xHH
      case '\0'...'\x08':
      case '\x0E'...'\x1F':
      case 0x7f:
        if (LIKELY( p + 3 < lastp )) {
          p[0] = '\\';
          p[1] = 'x';
          if (c < 0x10) {
            p[2] = '0';
            p[3] = hexchars[(int)c];
          } else {
            p[2] = hexchars[(int)c >> 4];
            p[3] = hexchars[(int)c & 0xf];
          }
          p += 4;
        } else {
          p = lastp;
        }
        nwrite += 4;
        break;
      // \c
      case '\t'...'\x0D':
      case '\\':
      case '"':
        static const char t[] = {'t','n','v','f','r'};
        if (LIKELY( p + 1 < lastp )) {
          p[0] = '\\';
          p[1] = ((usize)c - '\t') <= sizeof(t) ? t[c - '\t'] : c;
          p += 2;
        } else {
          p = lastp;
        }
        nwrite++;
        break;
      // verbatim
      default:
        *p = c;
        p = MIN(p + 1, lastp);
        nwrite++;
        break;
    }
  }

  if (check_add_overflow(s->len, nwrite, &s->len))
    s->len = USIZE_MAX;
  s->p = p;
}


void sbuf_appendf64(SBuf* s, f64 v, int ndec) {
  #ifndef CO_WITH_LIBC
    #warning TODO implement for non-libc
    assert(!"not implemented");
    // TODO: consider using fmt_fp (stdio/vfprintf.c) in musl
  #else
    usize cap = SBUF_AVAIL(s);
    int n;
    if (ndec > -1) {
      n = snprintf(s->p, cap+1, "%.*f", ndec, v);
    } else {
      n = snprintf(s->p, cap+1, "%f", v);
    }
    if (UNLIKELY( n <= 0 ))
      return;
    if (ndec < 0) {
      // trim trailing zeros
      char* p = &s->p[MIN((usize)n, cap) - 1];
      while (*p == '0') {
        p--;
      }
      if (*p == '.')
        p++; // avoid "1.00" becoming "1." (instead, let it be "1.0")
      n = (int)(uintptr)(p - s->p) + 1;
      s->p[MIN((usize)n, cap)] = 0;
    }
    s->p += MIN((usize)n, cap);
    s->len += n;
  #endif
}


bool sbuf_endswith(const SBuf* s, const char* str, usize len) {
  return s->len >= len && memcmp(s->p - len, str, len) == 0;
}
