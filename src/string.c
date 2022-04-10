// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


char abuf_zeroc = 0;
static const char* alphabet62 =
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static const char* hexchars = "0123456789abcdef";


error _sparse_u64(const char* src, usize srclen, u32 base, u64* result, u64 cutoff) {
  assert(base >= 2 && base <= 36);
  const char* s = src;
  const char* end = src + srclen;
  u64 acc = 0;
  u64 cutlim = cutoff % base;
  cutoff /= base;
  int any = 0;
  for (char c = *s; s != end; c = *++s) {
    if (ascii_isdigit(c)) {
      c -= '0';
    } else if (ascii_isupper(c)) {
      c -= 'A' - 10;
    } else if (ascii_islower(c)) {
      c -= 'a' - 10;
    } else {
      return err_invalid;
    }
    if (c >= (int)base)
      return err_invalid;
    if (any < 0 || acc > cutoff || (acc == cutoff && (u64)c > cutlim)) {
      any = -1;
    } else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if UNLIKELY(any < 0 || any == 0) {
    // more digits than what fits in acc, or empty input (srclen == 0)
    return any ? err_overflow : err_invalid;
  }
  *result = acc;
  return 0;
}


error _sparse_i64(const char* src, usize z, u32 base, i64* result, u64 cutoff) {
  assert(
    cutoff == (u64)I64_MAX + 1 ||
    cutoff == (u64)I32_MAX + 1 ||
    cutoff == (u64)I16_MAX + 1 ||
    cutoff == (u64)I8_MAX + 1 );

  const char* start = src;
  if (z > 0 && *src == '-') {
    start++;
    z--;
  }

  u64 uresult;
  error err = _sparse_u64(start, z, base, &uresult, cutoff);
  if (err)
    return err;

  if (*src == '-') {
    *result = uresult == cutoff ? (i64)uresult : -(i64)uresult;
  } else {
    if UNLIKELY(uresult == cutoff)
      return err_overflow;
    *result = (i64)uresult;
  }

  return 0;
}


error sparse_u32(const char* src, usize srclen, u32 base, u32* result) {
  u64 r;
  error err = _sparse_u64(src, srclen, base, &r, 0xFFFFFFFF);
  *result = (u32)r;
  return err;
}


error sparse_i32(const char* src, usize srclen, u32 base, i32* result) {
  i64 r;
  error err = _sparse_i64(src, srclen, base, &r, (u64)I32_MAX + 1);
  *result = (i32)r;
  return err;
}


usize sfmt_u64(char buf[64], u64 v, u32 base) {
  base = MAX(2, MIN(base, 62));
  char* p = buf;
  do {
    *p++ = alphabet62[v % base];
    v /= base;
  } while (v);
  usize len = (usize)(uintptr)(p - buf);
  p--;
  sreverse(buf, len);
  return len;
}


usize sfmt_i64(char buf[65], i64 value, u32 base) {
  if (value >= 0)
    return sfmt_u64(buf, (u64)value, base);
  buf[0] = '-';
  buf++;
  return 1 + sfmt_u64(buf, (u64)-value, base);
}


usize sfmt_repr(char* buf, usize bufcap, const void* data, usize len) {
  ABuf s = abuf_make(buf, bufcap);
  abuf_repr(&s, data, len);
  return abuf_terminate(&s);
}


char* sreverse(char* s, usize len) {
  for (usize i = 0, j = len - 1; i < j; i++, j--) {
    char tmp = s[i];
    s[i] = s[j];
    s[j] = tmp;
  }
  return s;
}

const char* strim_begin(const char* s, usize len, char trimc) {
  usize i = 0;
  for (; s[i] == trimc && i < len; i++) {
  }
  return s + i;
}


usize strim_end(const char* s, usize len, char trimc) {
  if (len == 0)
    return 0;
  while (s[--len] == trimc && len != 0) {
  }
  return len + 1;
}


bool shasprefixn(const char* s, usize len, const char* prefix, usize prefix_len) {
  return len >= prefix_len && memcmp(s, prefix, prefix_len) == 0;
}

bool shassuffixn(const char* s, usize len, const char* suffix, usize suffix_len) {
  return len >= suffix_len && memcmp(s + len - suffix_len, suffix, suffix_len) == 0;
}


isize sindexofn(const char* src, usize len, char c) {
  if UNLIKELY(len > ISIZE_MAX)
    len = ISIZE_MAX;

  /* BEGIN musl licensed code
  This block of code has been adapted from musl and is licensed as follows:

  Copyright © 2005-2020 Rich Felker, et al.

  Permission is hereby granted, free of charge, to any person obtaining a copy of this
  software and associated documentation files (the "Software"), to deal in the Software
  without restriction, including without limitation the rights to use, copy, modify,
  merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to the following
  conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
  OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  */
  const u8* s = (const u8*)src;
  u8 b = (u8)c;

  #if __has_attribute(__may_alias__)
  #define SS         (sizeof(usize))
  #define ONES       ((usize)-1/U8_MAX)
  #define HIGHS      (ONES * (U8_MAX/2 + 1))
  #define HASZERO(x) ((x) - ONES & ~(x) & HIGHS)

  usize n = len;
  for (; ((uintptr)s & (sizeof(usize)-1)) && n && *s != b; s++, n--) {}

  if (n && *s != b) {
    typedef usize __attribute__((__may_alias__)) AliasSize;
    const AliasSize* w;
    usize k = ((usize) - 1/U8_MAX) * b;
    for (w = (const void*)s; n>=SS && !HASZERO(*w^k); w++, n-=SS);
    s = (const void*)w;
  }

  #undef SS
  #undef ONES
  #undef HIGHS
  #undef HASZERO
  #endif

  for (; n && *s != b; s++, n--) {}
  return n ? (isize)(len - n) : -1;
  /* END musl licensed code */
}


isize slastindexofn(const char* s, usize len, char c) {
  if UNLIKELY(len > ISIZE_MAX)
    len = ISIZE_MAX;
  while (len--) {
    if (s[len] == c)
      return (isize)len;
  }
  return -1;
}


isize sindexof(const char* s, char c) {
  char* p = strchr(s, c);
  return p ? (isize)(uintptr)(p - s) : -1;
}


isize slastindexof(const char* s, char c) {
  return slastindexofn(s, strlen(s), c);
}


void swrap_simple(char* buf, usize len, usize column_limit) {
  assert(column_limit > 0);
  if (len < column_limit)
    return;

  usize col = 0;
  char* lastspace = NULL;
  char* end = buf + len;

  for (; buf != end; buf++) {
    col++;
    if (*buf == '\n') {
      col = 0;
      lastspace = NULL;
    } else if (col > column_limit) {
      if (lastspace) {
        *lastspace = '\n';
        col = (usize)(uintptr)(buf - lastspace);
      } else {
        *buf = '\n';
        col = 0;
      }
      lastspace = NULL;
    } else if (ascii_isspace(*buf)) {
      lastspace = buf;
    }
  }
}


//———————————————————————————————————————————————————————————————————————————————————————
// ABuf impl

ABuf* abuf_c(ABuf* s, char c) {
  *s->p = c;
  s->p = MIN(s->p + 1, s->lastp);
  s->len++;
  return s;
}

ABuf* abuf_append(ABuf* s, const char* p, usize len) {
  usize z = MIN(len, abuf_avail(s));
  memcpy(s->p, p, z);
  s->p += z;
  if (check_add_overflow(s->len, len, &s->len))
    s->len = USIZE_MAX;
  return s;
}


ABuf* abuf_u64(ABuf* s, u64 v, u32 base) {
  char buf[64];
  usize len = sfmt_u64(buf, v, base);
  return abuf_append(s, buf, len);
}

ABuf* abuf_i64(ABuf* s, i64 v, u32 base) {
  char buf[65];
  usize len = sfmt_i64(buf, v, base);
  return abuf_append(s, buf, len);
}

ABuf* abuf_u32(ABuf* s, u32 v, u32 base) {
  char buf[32];
  usize len = sfmt_u32(buf, v, base);
  return abuf_append(s, buf, len);
}


ABuf* abuf_f64(ABuf* s, f64 v, int ndecimals) {
  #ifdef CO_NO_LIBC
    #warning TODO implement for non-libc
    assert(!"not implemented");
    // TODO: consider using fmt_fp (stdio/vfprintf.c) in musl
  #else
    usize cap = abuf_avail(s);
    int n;
    if (ndecimals > -1) {
      n = snprintf(s->p, cap+1, "%.*f", ndecimals, v);
    } else {
      n = snprintf(s->p, cap+1, "%f", v);
    }
    if (UNLIKELY( n <= 0 ))
      return s;
    if (ndecimals < 0) {
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
  return s;
}


ABuf* abuf_fill(ABuf* s, char c, usize len) {
  if (check_add_overflow(s->len, len, &s->len))
    s->len = USIZE_MAX;
  memset(s->p, c, MIN(len, abuf_avail(s)));
  s->p += len;
  return s;
}


ABuf* abuf_repr(ABuf* s, const void* src, usize len) {
  char* p = s->p;
  char* lastp = s->lastp;
  usize nwrite = 0;

  for (const void* end = src + len; src != end; src++) {
    u8 c = *(u8*)src;
    switch (c) {
      // \xHH
      case '\1'...'\x08':
      case 0x0E ... 0x1F:
      case 0x7f ... 0xFF:
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
      case '\0': {
        static const char t[] = {'t','n','v','f','r'};
        if (LIKELY( p + 1 < lastp )) {
          p[0] = '\\';
          if      (c == 0)                         p[1] = '0';
          else if (((usize)c - '\t') <= sizeof(t)) p[1] = t[c - '\t'];
          else                                     p[1] = c;
          p += 2;
        } else {
          p = lastp;
        }
        nwrite += 2;
        break;
      }
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
  return s;
}


ABuf* _abuf_reprhex(ABuf* s, const void* srcp, usize len, bool spaced) {
  char* p = s->p;
  char* lastp = s->lastp;
  usize nwrite = 0;
  for (usize i = 0; i < len; i++) {
    u8 c = *(u8*)srcp++;
    if (LIKELY( p + 1 + spaced < lastp )) {
      if (i && spaced)
        *p++ = ' ';
      if (c < 0x10) {
        p[0] = '0';
        p[1] = hexchars[c];
      } else {
        p[0] = hexchars[c >> 4];
        p[1] = hexchars[c & 0xf];
      }
      p += 2;
    } else {
      p = lastp;
    }
    nwrite += 2 + (i && spaced);
  }
  if (check_add_overflow(s->len, nwrite, &s->len))
    s->len = USIZE_MAX;
  s->p = p;
  return s;
}


ABuf* abuf_fmtv(ABuf* s, const char* fmt, va_list ap) {
  #if defined(CO_NO_LIBC) && !defined(__wasm__)
    dlog("abuf_fmtv not implemented");
  #else
    int n = vsnprintf(s->p, abuf_avail(s)+1, fmt, ap);
    assert(n >= 0);
    s->len += (usize)n;
    s->p = MIN(s->p + (usize)n, s->lastp);
  #endif
  return s;
}


ABuf* abuf_fmt(ABuf* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  abuf_fmtv(s, fmt, ap);
  va_end(ap);
  return s;
}


bool abuf_endswith(const ABuf* s, const char* str, usize len) {
  return s->len >= len && memcmp(s->p - len, str, len) == 0;
}


//———————————————————————————————————————————————————————————————————————————————————————
// Str impl


Str str_dup(const char* src, usize len) {
  Str s = str_make(NULL, 0);
  str_append(&s, src, len);
  return s;
}


#define STR_USE_ABUF_BEGIN(initnbytes) \
  for (usize nbytes__ = (initnbytes); ; ) { \
    if UNLIKELY(!str_reserve(s, nbytes__)) \
      return false; \
    ABuf buf = abuf_make(&s->v[s->len], str_availcap(s));
// use buf here
#define STR_USE_ABUF_END() \
    if LIKELY(buf.len < str_availcap(s)) { \
      s->len += buf.len; \
      break; \
    } \
    /* we didn't have enough space; try again */ \
    nbytes__ = buf.len + 1; \
  }


bool str_appendrepr(Str* s, const void* p, usize size) {
  STR_USE_ABUF_BEGIN(size*2)
  abuf_repr(&buf, p, size);
  STR_USE_ABUF_END()
  return true;
}


bool str_appendreprhex(Str* s, const void* p, usize size) {
  STR_USE_ABUF_BEGIN(size*2)
  abuf_reprhex(&buf, p, size);
  STR_USE_ABUF_END()
  return true;
}


bool str_appendf64(Str* s, f64 value, int ndecimals) {
  STR_USE_ABUF_BEGIN(20)
  abuf_f64(&buf, value, ndecimals);
  STR_USE_ABUF_END()
  return true;
}


bool str_appendfmtv(Str* s, const char* fmt, va_list ap) {
  va_list ap2;
  u32 nbytes = strlen(fmt)*2;

again:
  if UNLIKELY(!str_reserve(s, nbytes + 1)) // +1 for vsnprintf's \0 terminator
    return false;

  u32 avail = s->cap - s->len;
  assert(avail >= nbytes + 1);
  // ^ should not be possible, but prevent inifinite loop just in case

  va_copy(ap2, ap); // copy va_list since we might read it twice
  int n = vsnprintf(&s->v[s->len], avail, fmt, ap2);
  va_end(ap2);
  if UNLIKELY(n < 0)
    return false;

  nbytes = (u32)n;
  if (nbytes >= avail) // not enough space
    goto again;

  s->len += nbytes;
  return true;
}


bool str_appendfmt(Str* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  bool ok = str_appendfmtv(s, fmt, ap);
  va_end(ap);
  return ok;
}


bool str_appendu32(Str* s, u32 value, u32 base) {
  if UNLIKELY(!str_reserve(s, 32))
    return false;
  s->len += sfmt_u32(s->v + s->len, value, base);
  return true;
}


bool str_appendu64(Str* s, u32 value, u32 base) {
  if UNLIKELY(!str_reserve(s, 64))
    return false;
  s->len += sfmt_u64(s->v + s->len, value, base);
  return true;
}


char* nullable str_cstr(Str* s) {
  if (s->cap == 0 && UNLIKELY(!_array_grow((VoidArray*)s, mem_ctx(), 1, 1)))
    return NULL;
  s->v[s->len] = 0;
  return s->v;
}


bool str_appendfill(Str* s, char c, u32 len) {
  if UNLIKELY(!str_reserve(s, len))
    return false;
  memset(s->v + s->len, c, len);
  s->len += len;
  return true;
}
