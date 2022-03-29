// String functions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define STRING_IMPLEMENTATION
#endif
#include "array.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

// sparse_TYPE parses a string as TYPE.
// These functions return err_invalid if the input is not valid, or err_overflow.
static error sparse_u64(const char* src, usize len, u32 base, u64* result);
static error sparse_i64(const char* src, usize len, u32 base, i64* result);
error sparse_u32(const char* src, usize len, u32 base, u32* result);
error sparse_i32(const char* src, usize len, u32 base, i32* result);

// sfmt_TYPE formats a value of TYPE, returning the number of bytes written.
// These functions do NOT append a terminating '\0'.
usize sfmt_u64(char buf[64], u64 value, u32 base);
static usize sfmt_u32(char buf[32], u32 value, u32 base);
static usize sfmt_u8(char buf[8], u8 value, u32 base);

// sreverse reverses s in place; returns s.
char* sreverse(char* s, usize len);

// shasprefix returns true if s starts with prefix_cstr
static bool shasprefix(const char* s, usize len, const char* prefix_cstr);

//———————————————————————————————————————————————————————————————————————————————————————
// ABuf is a string append buffer for implementing snprintf-style functions which
// writes to a limited buffer and separately keeps track of the number of bytes
// that are appended independent of the buffer's limit.
typedef struct ABuf ABuf;

// append functions
ABuf* abuf_append(ABuf* s, const char* p, usize len);
// static ABuf* abuf_str(ABuf* s, Str str); // = abuf_append(s, str.p, str.len)
static ABuf* abuf_cstr(ABuf* s, const char* cstr); // = abuf_append(s, cstr, strlen())
ABuf* abuf_c(ABuf* s, char c);
ABuf* abuf_u64(ABuf* s, u64 v, u32 base);
ABuf* abuf_u32(ABuf* s, u32 v, u32 base);
ABuf* abuf_f64(ABuf* s, f64 v, int ndecimals);
ABuf* abuf_fill(ABuf* s, char c, usize len); // like memset
ABuf* abuf_repr(ABuf* s, const void* p, usize len);    // "hello\nworld\0"
ABuf* abuf_reprhex(ABuf* s, const void* p, usize len); // "68656c6c6f0a776f726c6400"
ABuf* abuf_fmt(ABuf* s, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
ABuf* abuf_fmtv(ABuf* s, const char* fmt, va_list);

static usize abuf_terminate(ABuf* s); // sets last byte to \0 and returns s->len
static usize abuf_avail(const ABuf* s); // number of bytes available to write
bool abuf_endswith(const ABuf* s, const char* str, usize len);
// TODO: trim

// Here is a template for functions that uses ABuf:
//
// // It writes at most bufcap-1 of the characters to the output buf (the bufcap'th
// // character then gets the terminating '\0'). If the return value is greater than or
// // equal to the bufcap argument, buf was too short and some of the characters were
// // discarded. The output is always null-terminated, unless size is 0.
// // Returns the number of characters that would have been printed if bufcap was
// // unlimited (not including the final `\0').
// usize myfmt(char* buf, usize bufcap, int somearg) {
//   ABuf s = abuf_make(buf, bufcap);
//   // call abuf_append functions here
//   return abuf_terminate(&s);
// }
//
extern char abuf_zeroc;
#define abuf_make(p,size) ({ /* ABuf abuf_make(char* buf, usize bufcap)               */\
  usize z__ = (usize)(size); char* p__ = (p);                                           \
  UNLIKELY(z__ == 0) ? (ABuf){&abuf_zeroc,&abuf_zeroc,0} : (ABuf){ p__, p__+z__-1, 0 }; \
})

//———————————————————————————————————————————————————————————————————————————————————————
// Str: mutable growable string
typedef Array(char) Str;

// functions provided by array implementation (str_* is a thin veneer on array_*)
static Str str_make(char* nullable storage, usize storagesize);
static void str_init(Str* a, char* nullable storage, usize storagesize);
static void str_clear(Str* a);
static void str_free(Str* a);
static char str_at(Str* a, u32 index);
static char str_at_safe(Str* a, u32 index);
static bool str_push(Str* a, char c);
static bool str_append(Str* a, const char* src, u32 len);
static void str_remove(Str* a, u32 start, u32 len);
static void str_move(Str* a, u32 dst, u32 start, u32 end);
static bool str_reserve(Str* a, u32 addl);
static bool str_fill(Str* a, u32 start, const char c, u32 len);
static bool str_splice(Str* a, u32 start, u32 rmlen, u32 addlen, const char* nullable addv);

// extended functions (not provided by array implementation; implemented in this file)
bool str_appendfmt(Str*, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
bool str_appendfmtv(Str*, const char* fmt, va_list);
static bool str_appendcstr(Str*, const char* cstr);
bool str_appendfill(Str*, char c, u32 len); // like memset
bool str_appendrepr(Str*, const void* p, usize size);
bool str_appendu32(Str*, u32 value, u32 base);
bool str_appendu64(Str*, u32 value, u32 base);
bool str_appendf64(Str*, f64 value, int ndecimals);





//———————————————————————————————————————————————————————————————————————————————————————
// internal

error _sparse_u64(const char* src, usize len, u32 base, u64* result, u64 cutoff);
error _sparse_i64(const char* src, usize len, u32 base, i64* result, u64 cutoff);

inline static error sparse_u64(const char* src, usize len, u32 base, u64* result) {
  return _sparse_u64(src, len, base, result, 0xFFFFFFFFFFFFFFFF);
}
inline static error sparse_i64(const char* src, usize len, u32 base, i64* result) {
  return _sparse_i64(src, len, base, result, (u64)I64_MAX + 1);
}

inline static usize sfmt_u32(char buf[32], u32 v, u32 base) {
  return sfmt_u64(buf, (u64)v, base);
}
inline static usize sfmt_u8(char buf[8], u8 v, u32 base) {
  return sfmt_u64(buf, (u64)v, base);
}

inline static bool shasprefix(const char* s, usize len, const char* prefix_cstr) {
  return len >= strlen(prefix_cstr) && memcmp(s, prefix_cstr, strlen(prefix_cstr)) == 0;
}

//————

DEF_ARRAY_VENEER(Str, char, str_)

inline static bool str_appendcstr(Str* s, const char* cstr) {
  return str_append(s, cstr, strlen(cstr)) == 0;
}

//————

struct ABuf {
  char* p;
  char* lastp;
  usize len;
};

// inline static void abuf_str(ABuf* s, Str str) {
//   abuf_append(s, str.p, str.len); // TODO
// }
inline static ABuf* abuf_cstr(ABuf* s, const char* cstr) {
  return abuf_append(s, cstr, strlen(cstr));
}
inline static usize abuf_terminate(ABuf* s) {
  *s->p = 0; return s->len;
}
inline static usize abuf_avail(const ABuf* s) {
  return (usize)(uintptr)(s->lastp - s->p);
}

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef STRING_IMPLEMENTATION

#include "unicode.c"

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


char* sreverse(char* s, usize len) {
  for (usize i = 0, j = len - 1; i < j; i++, j--) {
    char tmp = s[i];
    s[i] = s[j];
    s[j] = tmp;
  }
  return s;
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


ABuf* abuf_repr(ABuf* s, const void* srcp, usize len) {
  char* p = s->p;
  char* lastp = s->lastp;
  usize nwrite = 0;

  for (usize i = 0; i < len; i++) {
    u8 c = *(u8*)srcp++;
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
        nwrite++;
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


ABuf* abuf_reprhex(ABuf* s, const void* srcp, usize len) {
  char* p = s->p;
  char* lastp = s->lastp;
  usize nwrite = 0;
  for (usize i = 0; i < len; i++) {
    u8 c = *(u8*)srcp++;
    if (LIKELY( p + 2 < lastp )) {
      if (i)
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
    if (i)
      nwrite++;
    nwrite += 2;
  }
  if (check_add_overflow(s->len, nwrite, &s->len))
    s->len = USIZE_MAX;
  s->p = p;
  return s;
}


ABuf* abuf_fmtv(ABuf* s, const char* fmt, va_list ap) {
  #if defined(CO_NO_LIBC) && !defined(__wasm__)
    dlog("abuf_fmtv not implemented");
    int n = vsnprintf(tmpbuf, sizeof(tmpbuf), format, ap);
  #else
    int n = vsnprintf(s->p, abuf_avail(s), fmt, ap);
    s->len += (usize)n;
    s->p = MIN(s->p + n, s->lastp);
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

#define STR_USE_ABUF_BEGIN(initnbytes) \
  for (usize nbytes__ = (initnbytes); ; ) { \
    if UNLIKELY(!str_reserve(s, nbytes__)) \
      return false; \
    ABuf buf = abuf_make(&s->v[s->len], nbytes__);
// use buf here
#define STR_USE_ABUF_END() \
    if LIKELY(buf.len < nbytes__) { \
      s->len += buf.len; \
      break; \
    } \
    /* we didn't have enough space; try again */ \
    nbytes__ = buf.len + 1; \
  }


bool str_appendfmtv(Str* s, const char* fmt, va_list ap) {
  va_list ap2;
  STR_USE_ABUF_BEGIN(strlen(fmt)*2)
    va_copy(ap2, ap); // copy va_list since we might read it twice
    abuf_fmtv(&buf, fmt, ap2);
    va_end(ap2);
  STR_USE_ABUF_END()
  return true;
}

bool str_appendfmt(Str* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  bool ok = str_appendfmtv(s, fmt, ap);
  va_end(ap);
  return ok;
}

bool str_appendrepr(Str* s, const void* p, usize size) {
  STR_USE_ABUF_BEGIN(size*2)
  abuf_repr(&buf, p, size);
  STR_USE_ABUF_END()
  return true;
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

bool str_appendf64(Str* s, f64 value, int ndecimals) {
  STR_USE_ABUF_BEGIN(20)
  abuf_f64(&buf, value, ndecimals);
  STR_USE_ABUF_END()
  return true;
}


bool str_appendfill(Str* s, char c, u32 len) {
  if UNLIKELY(!str_reserve(s, len))
    return false;
  memset(s->v + s->len, c, len);
  s->len += len;
  return true;
}

//———————————————————————————————————————————————————————————————————————————————————————
#endif // STRING_IMPLEMENTATION
