#include "coimpl.h"
#include "str.h"
#include "sbuf.h"
#include "unicode.c"

#ifndef CO_NO_LIBC
  #include <stdio.h>
#endif

// DEBUG_STR_TRACE_MEM: define to trace memory allocations and deallocations
//#define DEBUG_STR_TRACE_MEM
#ifdef DEBUG_STR_TRACE_MEM
  #define memtrace dlog
#else
  #define memtrace(...) ((void)0)
#endif

#define ALLOC_MIN sizeof(usize) /* smallest new string */


Str str_make(Mem mem, u32 cap) {
  cap = MAX( ALIGN2(cap + 1, sizeof(usize)), ALLOC_MIN );

  usize size = STRUCT_SIZE((struct Str*)0, p, cap);
  if UNLIKELY(size == USIZE_MAX)
    return NULL;
  Str s = (Str)mem_alloc(mem, size);
  if (!s)
    return NULL;

  memtrace("str_alloc %p (%zu)", s, size);
  s->mem = mem;
  s->len = 0;
  s->cap = cap - 1; // does not include the sentinel byte
  s->p[0] = 0;
  return s;
}

Str nullable str_grow(Str s, u32 addlen) {
  #ifdef DEBUG_STR_TRACE_MEM
    usize oldz = STRUCT_SIZE( ((struct Str*)0), p, s->cap + 1 );
  #endif
  usize oldsize = s->cap;
  s->cap = ALIGN2((s->len + addlen) * 2, sizeof(usize));
  usize z = STRUCT_SIZE( ((struct Str*)0), p, s->cap );
  if (z == USIZE_MAX)
    return NULL;
  s->cap--; // does not include the sentinel byte
  Str s2 = (Str)mem_resize(s->mem, s, oldsize, z);
  memtrace("str_realloc %p (%zu) -> %p (%zu)", s, oldz, s2, z);
  return s2;
}

Str str_make_copy(Mem mem, const char* p, u32 len) {
  Str s = str_make(mem, len);
  if (!s)
    return NULL;
  memcpy(s->p, p, (usize)len);
  return str_setlen(s, len);
}

Str str_make_fmt(Mem mem, const char* fmt, ...) {
  u64 len = (u64)strlen(fmt) * 4;
  if (len > U32_MAX)
    return NULL;
  Str s = str_make(mem, len);
  if (!s)
    return NULL;
  va_list ap;
  va_start(ap, fmt);
  s = str_appendfmtv(s, fmt, ap);
  va_end(ap);
  return s;
}

Str nullable str_make_hex(Mem mem, const u8* data, u32 len) {
  return str_appendhex(str_make(mem, len * 2), data, len);
}

Str nullable str_make_hex_lc(Mem mem, const u8* data, u32 len) {
  return str_appendhex_lc(str_make(mem, len * 2), data, len);
}

void str_free(Str s) {
  memtrace("str_free %p", s);
  mem_free(s->mem, s, s->cap + 1);
}

Str str_appendn(Str s, const char* p, u32 len) {
  s = str_makeroom(s, len);
  memcpy(&s->p[s->len], p, len);
  s->len += len;
  s->p[s->len] = 0;
  return s;
}

Str str_appendc(Str s, char c) {
  if (s->cap - s->len == 0)
    s = str_makeroom(s, 1);
  s->p[s->len++] = c;
  s->p[s->len] = 0;
  return s;
}

Str str_appendfmtv(Str s, const char* fmt, va_list ap) {
  #ifdef CO_NO_LIBC
    #warning TODO implement str_appendfmtv for non-libc
    assert(!"not implemented");
  #else
    // start by making an educated guess for space needed: 2x that of the format:
    usize len = (strlen(fmt) * 2) + 1;
    u32 offs = s->len;
    va_list ap2;
    while (1) {
      assert(len <= 0x7fffffff);
      s = str_makeroom(s, (u32)len);
      va_copy(ap2, ap); // copy va_list as we might read it twice
      int n = vsnprintf(&s->p[offs], len, fmt, ap2);
      va_end(ap2);
      if (n < (int)len) {
        // ok; result fit in buf.
        // Theoretically vsnprintf might return -1 on error, but AFAIK no implementation does
        // unless len > INT_MAX, so we are likely fine with ignoring that case here.
        len = (u32)n;
        break;
      }
      // vsnprintf tells us how much space it needs
      len = (u32)n + 1;
    }
    // update len (vsnprintf wrote terminating \0 already)
    s->len += len;
  #endif // CO_NO_LIBC
  return s;
}

Str str_appendfmt(Str s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s = str_appendfmtv(s, fmt, ap);
  va_end(ap);
  return s;
}

Str str_appendfill(Str s, u32 n, char c) {
  s = str_makeroom(s, n);
  memset(&s->p[s->len], c, n);
  s->len += n;
  s->p[s->len] = 0;
  return s;
}


// str_appendrepr appends a human-readable representation of data to dst as C-format ASCII
// string literals, with "special" bytes escaped (e.g. \n, \xFE, etc.)
Str str_appendrepr(Str s, const char* data, u32 len) {
  // TODO: replace this with sbuf_appendrepr
  assert((u64)len <= ((u64)U32_MAX) * 8);
  u32 morelen = len * 4;
  char* dst;
  bool prevesc = false; // true when an escape sequence was written
  while (1) {
    s = str_makeroom(s, morelen);
    dst = &s->p[s->len];
    char* dstend = &dst[s->cap - s->len];
    char* dstlinestart = dst;
    for (u32 srci = 0; srci < len; srci++) {
      if (dst == dstend)
        goto retry;
      if (dst - dstlinestart >= 80) {
        *dst++ = '\n';
        dstlinestart = dst;
        if (dst == dstend)
          goto retry;
      }
      char c = data[srci];
      if (c == ' ' || (c != '"' && !ascii_isspace(c) && ascii_isprint(c))) {
        // In case we just wrote a hex escape sequence, make sure we don't write a
        // third hex digit. This confuses compilers when the result is used as a C literal.
        if (!prevesc || !ascii_ishexdigit(c)) {
          *dst++ = c;
          prevesc = false;
          continue;
        }
      }
      if (dst + 2 > dstend)
        goto retry;
      *dst++ = '\\';
      // prevesc = false;
      switch (c) {
        case '\t': *dst++ = 't'; break;
        case '\n': *dst++ = 'n'; break;
        case '\r': *dst++ = 'r'; break;
        case '"':  *dst++ = c; break;
        default: // \xHH
          if (dst + 3 > dstend)
            goto retry;
          dst[0] = 'x';
          static const char* hexchars = "0123456789abcdef";
          dst[1] = hexchars[c >> 4];
          dst[2] = hexchars[c & 0xf];
          dst += 3;
          prevesc = true;
          break;
      }
    }
    break;
retry:
    // dst not large enough
    morelen *= 2;
  }
  s->len = (u32)(uintptr)(dst - s->p);
  s->p[s->len] = 0;
  return s;
}


static Str _str_appendhex(Str s, const u8* data, u32 len, const char* alphabet) {
  s = str_makeroom(s, len * 2);
  char* p = &s->p[s->len];
  for (u32 i = 0; i < len; i++){
    *p++ = alphabet[(*data >> 4) & 0xF];
    *p++ = alphabet[(*data) & 0xF];
    data++;
  }
  s->len += len * 2;
  s->p[s->len] = 0;
  return s;
}

Str str_appendhex(Str s, const u8* data, u32 len) {
  return _str_appendhex(s, data, len, "0123456789ABCDEF");
}

Str str_appendhex_lc(Str s, const u8* data, u32 len) {
  return _str_appendhex(s, data, len, "0123456789abcdef");
}

Str str_appendu64(Str s, u64 v, u32 base) {
  s = str_makeroom(s, 64);
  if (UNLIKELY( !s ))
    return s;
  u32 n = strfmt_u64(&s->p[s->len], v, base);
  s->len += n;
  s->p[s->len] = 0;
  return s;
}


Str nullable str_appendf64(Str s, f64 v, int ndec) {
  u32 z = MAX(ndec*4, 32);
  while (z < 4096) {
    s = str_makeroom(s, z);
    if (UNLIKELY( !s ))
      return s;
    SBuf buf = sbuf_make(&s->p[s->len], z);
    sbuf_appendf64(&buf, v, ndec);
    if (LIKELY( buf.len <= (usize)z) ) {
      s->len += buf.len;
      s->p[s->len] = 0;
      return s;
    }
    if (UNLIKELY( buf.len > 0xFFFFFFFF ))
      break;
    z = (u32)buf.len;
  }
  return s;
}


Str* str_tmp() {
  // _tmpstr holds per-thread temporary string buffers for use by fmtnode and fmtast.
  static thread_local struct {
    u32 index; // next buffer index (effective index = index % STR_TMP_MAX)
    Str bufs[STR_TMP_MAX];

    #ifdef CO_NO_LIBC
      Mem mem;
      FixBufAllocator ma;
      char mbuf[4096*8];
    #endif
  } _tmpstr = {0};

  #ifdef CO_NO_LIBC
    if (_tmpstr.mem == NULL)
      _tmpstr.mem = mem_buf_allocator_init(
        &_tmpstr.ma, _tmpstr.mbuf, sizeof(_tmpstr.mbuf));
  #endif

  u32 bufindex = _tmpstr.index % STR_TMP_MAX;
  _tmpstr.index++;
  Str s = _tmpstr.bufs[bufindex];
  if (s) {
    str_trunc(s);
  } else {
    #ifdef CO_NO_LIBC
      Mem mem = _tmpstr.mem;
    #else
      Mem mem = mem_mkalloc_libc();
    #endif
    _tmpstr.bufs[bufindex] = str_make(mem, 64);
  }
  return &_tmpstr.bufs[bufindex];
}


// -- end of Str functions


char* strrevn(char* s, usize len) {
  for (usize i = 0, j = len - 1; i < j; i++, j--) {
    char tmp = s[i];
    s[i] = s[j];
    s[j] = tmp;
  }
  return s;
}

usize strfmt_u64(char buf[64], u64 v, u32 base) {
  static const char chars[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  base = MIN(base, 62);
  char* p = buf;
  do {
    *p++ = chars[v % base];
    v /= base;
  } while (v);
  usize len = (usize)(uintptr)(p - buf);
  p--;
  strrevn(buf, len);
  return len;
}


usize strrepr(char* dst, usize dstcap, const char* src, usize srclen) {
  SBuf buf = sbuf_make(dst, dstcap);
  sbuf_appendrepr(&buf, src, srclen);
  return sbuf_terminate(&buf);
}


error _strparse_u64_base10(const char* src, usize srclen, u64* result) {
  u64 n = 0;
  const char* end = src + srclen;
  while (src != end && ascii_isdigit(*src))
    n = 10*n + (*src++ - '0');
  *result = n;
  return src == end ? 0 : err_invalid;
}


error strparse_u32(const char* src, usize srclen, int base, u32* result) {
  u64 r;
  error err;
  if (base == 10) {
    err = _strparse_u64_base10(src, srclen, &r);
    if (err == 0 && r > 0xFFFFFFFF)
      err = err_overflow;
  } else {
    err = _strparse_u64(src, srclen, base, &r, 0xFFFFFFFF);
  }
  *result = (u32)r;
  return err;
}


error _strparse_u64(const char* src, usize z, int base, u64* result, u64 cutoff) {
  assert(base >= 2 && base <= 36);
  const char* s = src;
  const char* end = src + z;
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
    if (c >= base)
      return err_invalid;
    if (any < 0 || acc > cutoff || (acc == cutoff && (u64)c > cutlim)) {
      any = -1;
    } else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if (any < 0 || // more digits than what fits in acc
      any == 0)
  {
    return err_overflow;
  }
  *result = acc;
  return 0;
}


error strparse_i64(const char* src, usize z, int base, i64* result) {
  while (z > 0 && *src == '-') {
    src++;
    z--;
  }
  i64 r;
  error err = strparse_u64(src, z, base, (u64*)&r);
  if (err)
    return err;
  if (*src == '-')
    r = -r;
  *result = r;
  return 0;
}
