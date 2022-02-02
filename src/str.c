#include "coimpl.h"
#include "str.h"
#include "sbuf.h"
#include "test.h"
#include "unicode.h"

#ifdef CO_WITH_LIBC
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

  Str s = (Str)memalloctv(mem, struct Str, p, cap);
  if (!s)
    return NULL;

  memtrace("str_alloc %p (%zu)", s, STRUCT_SIZE( ((struct Str*)0), p, cap ));
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
  s->cap = ALIGN2((s->len + addlen) * 2, sizeof(usize));
  usize z = STRUCT_SIZE( ((struct Str*)0), p, s->cap );
  if (z == USIZE_MAX)
    return NULL;
  s->cap--; // does not include the sentinel byte
  Str s2 = (Str)memrealloc(s->mem, s, z);
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
  memfree(s->mem, s);
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
  #ifndef CO_WITH_LIBC
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
  #endif // CO_WITH_LIBC
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
  assert((u64)len <= ((u64)UINT32_MAX) * 8);
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
          #ifndef CO_WITH_LIBC
            #warning TODO implement for non-libc
          #else
            sprintf(dst, "x%02X", c);
          #endif
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

char* strrevn(char* s, usize len) {
  for (usize i = 0, j = len - 1; i < j; i++, j--) {
    char tmp = s[i];
    s[i] = s[j];
    s[j] = tmp;
  }
  return s;
}

u32 strfmtu64(char buf[64], u64 v, u32 base) {
  static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  base = MIN(base, 62);
  char* p = buf;
  do {
    *p++ = chars[v % base];
    v /= base;
  } while (v);
  u32 len = (u32)(uintptr)(p - buf);
  p--;
  strrevn(buf, len);
  return len;
}

Str str_appendu64(Str s, u64 v, u32 base) {
  s = str_makeroom(s, 64);
  if (UNLIKELY( !s ))
    return s;
  u32 n = strfmtu64(&s->p[s->len], v, base);
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
    SBuf buf = SBUF_INITIALIZER(&s->p[s->len], z);
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

    #ifndef CO_WITH_LIBC
      Mem mem;
      MemBufAllocator ma;
      char mbuf[4096*8];
    #endif
  } _tmpstr = {0};

  #ifndef CO_WITH_LIBC
    if (_tmpstr.mem == NULL)
      _tmpstr.mem = mem_buf_allocator_init(&_tmpstr.ma, _tmpstr.mbuf, sizeof(_tmpstr.mbuf));
  #endif

  u32 bufindex = _tmpstr.index % STR_TMP_MAX;
  _tmpstr.index++;
  Str s = _tmpstr.bufs[bufindex];
  if (s) {
    str_trunc(s);
  } else {
    #ifdef CO_WITH_LIBC
      Mem mem = mem_libc_allocator();
    #else
      Mem mem = _tmpstr.mem;
    #endif
    _tmpstr.bufs[bufindex] = str_make(mem, 64);
  }
  return &_tmpstr.bufs[bufindex];
}
