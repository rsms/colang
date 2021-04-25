#include "rbase.h"
#include <ctype.h> // isprint, isspace

#define ALLOC_MIN sizeof(size_t) /* smallest new string */

// DEBUG_STR_TRACE_MEM: define to trace memory allocations and deallocations
// #define DEBUG_STR_TRACE_MEM
#ifdef DEBUG_STR_TRACE_MEM
  #define memtrace dlog
#else
  #define memtrace(...)
#endif

// FIXME: memrealloc is buggy. Use Mem when we have fixed it or replaced dlmalloc
// #define MEMALLOC(size)        memalloc_raw(NULL, (size))
// #define MEMREALLOC(ptr, size) memrealloc(NULL, (ptr), (size))
// #define MEMFREE(ptr)          memfree(NULL, (ptr))
// For now, use libc malloc et al:
#define MEMALLOC(size)        malloc((size))
#define MEMREALLOC(ptr, size) realloc((ptr), (size))
#define MEMFREE(ptr)          free((ptr))

// memory size of StrHeader h
#define STR_HDR_SIZE(h) ((u32)(sizeof(struct StrHeader) + (h)->cap + 1))

Str str_new(u32 cap) {
  cap = MAX(cap + 1, ALLOC_MIN);
  auto h = (struct StrHeader*)MEMALLOC(sizeof(struct StrHeader) + cap);
  memtrace("str_alloc %p (size %u)", h, STR_HDR_SIZE(h));
  h->len = 0;
  h->cap = cap - 1;
  h->p[0] = 0;
  return &h->p[0];
}

Str str_cpy(const char* p, u32 len) {
  auto s = str_new(len);
  memcpy(s, p, (size_t)len);
  return str_setlen(s, len);
}

void str_free(Str s) {
  auto h = STR_HEADER(s);
  memtrace("str_free %p (size %u)", h, STR_HDR_SIZE(h));
  #ifdef DEBUG
    h->cap = 0;
    h->len = 0;
    h->p[0] = 0;
  #endif
  MEMFREE(h);
}

Str str_fmt(const char* fmt, ...) {
  Str s = str_new(strlen(fmt) * 4);
  va_list ap;
  va_start(ap, fmt);
  s = str_appendfmtv(s, fmt, ap);
  va_end(ap);
  return s;
}

Str str_makeroom(Str s, u32 addlen) {
  auto h = STR_HEADER(s);
  u32 avail = h->cap - h->len;
  if (avail >= addlen)
    return s;

  #if 1
  h->cap = h->len + addlen;
  if (h->cap < 4096)
    h->cap = align2(h->cap * 2, sizeof(size_t));
  // h->cap = h->cap + (addlen - avail);
  auto h2 = (struct StrHeader*)MEMREALLOC(h, STR_HDR_SIZE(h));
  #else
  auto h2 = (struct StrHeader*)MEMALLOC(STR_HDR_SIZE(h) + (addlen - avail));
  memcpy(h2, h, STR_HDR_SIZE(h));
  h2->cap = h->cap + (addlen - avail);
  #endif

  memtrace("str_realloc %p -> %p (%u)", h, h2, STR_HDR_SIZE(h2));
  // Note: memrealloc panics if memory can't be allocated rather than returning NULL
  return &h2->p[0];
}

char* str_reserve(Str* sp, u32 len) {
  Str s2 = str_makeroom(*sp, len);
  *sp = s2;
  char* p = s2;
  auto h = STR_HEADER(s2);
  h->len += len;
  h->p[h->len] = 0;
  return p;
}

Str str_append(Str s, const char* p, u32 len) {
  u32 curlen = str_len(s);
  s = str_makeroom(s, len);
  memcpy(s+curlen, p, len);
  str_setlen(s, curlen + len);
  return s;
}

Str str_appendc(Str s, char c) {
  auto h = STR_HEADER(s);
  if (h->cap - h->len == 0)
    s = str_makeroom(s, 1);
  memtrace("str_appendc %p", h);
  s[h->len++] = c;
  s[h->len] = 0;
  return s;
}

Str str_appendfmtv(Str s, const char* fmt, va_list ap) {
  // start by making an educated guess for space needed: 2x that of the format:
  size_t len = (strlen(fmt) * 2) + 1;
  u32 offs = str_len(s);
  va_list ap2;
  while (1) {
    assert(len <= INT_MAX);
    s = str_makeroom(s, (u32)len);
    va_copy(ap2,ap); // copy va_list as we might read it twice
    int n = vsnprintf(&s[offs], len, fmt, ap2);
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
  STR_HEADER(s)->len += len;
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
  auto h = STR_HEADER(s);
  memset(s + h->len, c, n);
  h->len += n;
  s[h->len] = 0;
  return s;
}

static inline bool ishexdigit(char c) {
  return (
    ('0' <= c && c <= '9') ||
    ('A' <= c && c <= 'F') ||
    ('a' <= c && c <= 'f')
  );
}


// str_appendrepr appends a human-readable representation of data to dst as C-format ASCII
// string literals, with "special" bytes escaped (e.g. \n, \xFE, etc.)
Str str_appendrepr(Str s, const char* data, u32 len) {
  assert((u64)len <= ((u64)UINT32_MAX) * 8);
  u32 morelen = len * 4;
  char* dst;
  bool prevesc = false; // true when an escape sequence was written
  while (1) {
    s = str_makeroom(s, morelen);
    dst = (char*)s + str_len(s);
    char* dstend = &dst[str_avail(s)];
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
      if (c == ' ' || (c != '"' && !isspace(c) && isprint(c))) {
        // In case we just wrote a hex escape sequence, make sure we don't write a
        // third hex digit. This confuses compilers when the result is used as a C literal.
        if (!prevesc || !ishexdigit(c)) {
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
          sprintf(dst, "x%X02", c);
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
  str_setlen(s, (u32)(dst - s));
  return s;
}


const char* str_splitn(StrSlice* st, char delim, const char* s, size_t slen) {
  const char* send = s + slen;
  const char* p;
  if (st->p == NULL) {
    st->p = s;
    p = s;
  } else {
    // skip past previous part
    p = st->p + st->len + 1;
    st->p = p;
  }
  while (p < send) {
    char c = *p;
    if (c == delim) {
      st->len = p - st->p;
      return st->p;
    }
    p++;
  }
  if (p > send)
    return NULL;
  st->len = p - st->p;
  return st->p;
}


bool str_hasprefixn(Str s, const char* prefix, u32 prefixlen) {
  if (str_len(s) < prefixlen)
    return false;
  return memcmp(s, prefix, prefixlen) == 0;
}


// -----------------------------------------------------------------------------------------------
// unit test

R_UNIT_TEST(str) {

  const int nfill = ALLOC_MIN * 2;
  const int iterations = 4;
  const size_t checkbufsize = ((size_t)nfill * (size_t)iterations) + 1;

  // used to build comparative data; ensured to be large enough
  char checkbuf[checkbufsize];
  size_t checkbuflen = 0;

  // test str_appendfmt where its space assumption will be wrong; it assumes the space
  // needed for formatting is 2xformat but in this case we use a filler format that will
  // require more space and thus cause str_appendfmt to fail its initial assumption.
  auto s = str_new(0);
  for (int i = 0; i < iterations; i++) {
    const char* format = "%-*s";

    size_t checkbufavail = checkbufsize - checkbuflen;
    int n = snprintf(&checkbuf[checkbuflen], checkbufavail, format, nfill, "");
    assert((ssize_t)n < (ssize_t)checkbufavail);
    checkbuflen += (size_t)n;

    s = str_appendfmt(s, format, nfill, "");
  }

  // verify that snprintf worked as expected, so we can rely on checkbuf
  assert(checkbuflen == checkbufsize - 1);

  // compare s with checkbuf
  assert(str_len(s) == checkbuflen);
  if (memcmp(s, checkbuf, checkbuflen) != 0) {
    auto s1 = str_new(checkbuflen * 2);
    auto s2 = str_new(checkbuflen * 2);
    dlog("FAIL: s != checkbuf\n--- s: ---\n\"%s\"\n\n--- checkbuf: ---\n\"%s\"\n",
      str_appendrepr(s1, s, str_len(s)),
      str_appendrepr(s2, checkbuf, checkbuflen) );
    str_free(s1);
    str_free(s2);
    assert(false);
  }

  str_free(s);
}
