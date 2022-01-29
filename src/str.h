// Str -- dynamic C strings
#pragma once
#include "mem.h"
ASSUME_NONNULL_BEGIN

// STR_TMP_MAX is the limit of concurrent valid buffers returned by str_tmp.
// For example, if STR_TMP_MAX==2 then:
//   Str* a = str_tmp();
//   Str* b = str_tmp();
//   Str* c = str_tmp(); // same as a
//
#define STR_TMP_MAX 8

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

// str_hasprefix returns true if s begins with prefix
static bool str_hasprefix(Str s, const char* prefix);
static bool str_hasprefixn(Str s, const char* prefix, u32 len);

// strrevn reverses s in place. Returns s.
char* strrevn(char* s, usize len);

// strfmtu64 writes a u64 value to buf, returning the length
// (does NOT add a null terminator)
u32 strfmtu64(char buf[64], u64 v, u32 base);

// str_tmp allocates the next temporary string buffer.
// It is thread safe.
//
// Strs returned by this function are managed in a circular-buffer fashion; calling str_tmp
// many times will eventually return the same Str, limited by STR_TMP_MAX.
//
// If you return a temporary string to a caller, make sure to annotate its type as
// "const Str" (or "cons char*" if you return s.p) to communicate that the user must
// not modify it.
//
// Example 1:
//   const Str fmtnode(const Node* n) {
//     Str* sp = str_tmp();   // allocate
//     *sp = NodeStr(*sp, n); // use and update pointer
//     return *sp;            // return immutable Str to user
//   }
//
// Example 2:
//   const char* fmtnode(const Node* n) {
//     Str* sp = str_tmp();   // allocate
//     *sp = NodeStr(*sp, n); // use and update pointer
//     return (*sp)->p;       // return c-string pointer to user
//   }
//
Str* str_tmp();


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
inline static bool str_hasprefixn(Str s, const char* prefix, u32 prefixlen) {
  return s->len >= prefixlen && memcmp(s, prefix, prefixlen) == 0;
}
inline static bool str_hasprefix(Str s, const char* prefix) {
  return str_hasprefixn(s, prefix, strlen(prefix));
}

ASSUME_NONNULL_END
