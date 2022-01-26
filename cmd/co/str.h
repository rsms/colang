// Str -- dynamic C strings
#pragma once
#include "mem.h"
ASSUME_NONNULL_BEGIN

typedef struct Str* Str;

struct Str {
  Mem  mem;
  u32  len;
  u32  cap;
  char p[];
} __attribute__ ((__packed__));

// str functions which return nullable Str returns NULL on memalloc failure or overflow

Str nullable str_make(Mem, u32 cap);
Str nullable str_make_copy(Mem, const char* src, u32 srclen);
static Str nullable str_make_cstr(Mem, const char* src_cstr);
Str nullable str_make_fmt(Mem, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
Str nullable str_make_hex(Mem, const u8* data, u32 len);
Str nullable str_make_hex_lc(Mem, const u8* data, u32 len);
void str_free(Str);

inline static u32 str_avail(const Str s) { return s->cap - s->len; }
static Str nullable str_makeroom(Str s, u32 addlen); // ensures that str_avail()>=addlen
Str nullable str_grow(Str s, u32 addlen); // grow s->cap by at least addlen

Str nullable str_appendn(Str dst, const char* src, u32 srclen);
Str nullable str_appendc(Str dst, char c);
static Str nullable str_appendstr(Str dst, const Str src);
static Str nullable str_appendcstr(Str dst, const char* cstr);
Str nullable str_appendfmt(Str dst, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
Str nullable str_appendfmtv(Str dst, const char* fmt, va_list);
Str nullable str_appendfill(Str dst, u32 n, char c);
Str nullable str_appendhex(Str dst, const u8* data, u32 len);
Str nullable str_appendhex_lc(Str dst, const u8* data, u32 len); // lower-case
Str nullable str_appendu64(Str dst, u64 v, u32 base);

// str_appendrepr appends a human-readable representation of data to dst as C-format ASCII
// string literals, with "special" bytes escaped (e.g. \n, \xFE, etc.)
// See str_appendhex as an alternative for non-text data.
Str str_appendrepr(Str s, const char* data, u32 len);

// Str str_append<T>(Str dst, T src) where T is a char or char*
#define str_append(dst_str, src) _Generic((src), \
  char:        str_appendc, \
  int:         str_appendc, \
  const char*: str_appendcstr, \
  char*:       str_appendcstr \
)(dst_str, src)

inline static Str str_setlen(Str s, u32 len) {
  assert(len <= s->cap);
  s->len = len;
  s->p[len] = 0;
  return s;
}
inline static Str str_trunc(Str s) { return str_setlen(s, 0); }


// --- inline implementation ---

inline static Str str_make_cstr(Mem mem, const char* src_cstr) {
  return str_make_copy(mem, src_cstr, strlen(src_cstr));
}
inline static Str str_makeroom(Str s, u32 addlen) {
  if (s->cap - s->len < addlen)
    return str_grow(s, addlen);
  return s;
}
inline static Str str_appendstr(Str s, const Str suffix) {
  return str_appendn(s, suffix->p, suffix->len);
}
inline static Str str_appendcstr(Str s, const char* cstr) {
  return str_appendn(s, cstr, strlen(cstr));
}

ASSUME_NONNULL_END
