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

// str_makeroom ensures that str_avail(s) >= addlen. Calls str_grow if needed.
static Str nullable str_makeroom(Str s, u32 addlen);

// str_grow grows the capacity of s so that str_avail(s) >= addlen.
// Returns NULL if memory allocation failed in which case the input s is still valid.
Str nullable str_grow(Str s, u32 addlen);

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
Str nullable str_appendf64(Str dst, f64 v, int ndec);

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

// --- end of Str functions
// --- what follows are string-related functionality not operating on Str

// strrevn reverses s in place. Returns s.
char* strrevn(char* s, usize len);

// strfmt_TYPE formats a value of TYPE, returning the number of bytes written.
// These functions does not append a terminating '\0'.
usize strfmt_u64(char buf[64], u64 v, u32 base);
static usize strfmt_u32(char buf[32], u32 v, u32 base);
static usize strfmt_u8(char buf[8], u8 v, u32 base);

// strparse_TYPE parses a string as TYPE.
// These functions return err_invalid if the input is not valid, or err_overflow.
static error strparse_u64(const char* src, usize len, int base, u64* result);
error strparse_u32(const char* src, usize len, int base, u32* result);
error strparse_i64(const char* src, usize len, int base, i64* result);

// strrepr appends a printable representation of src to dst, escaping characters which
// are non-printable (e.g. line feed), '"' and '\'.
// strrepr writes at most dstcap-1 of the characters to the output dst (the dstcap'th
// character then gets the terminating '\0'). If the return value is greater than or
// equal to the dstcap argument, dst was too short and some of the characters were
// discarded. The output is always null-terminated, unless size is 0.
// Returns the number of characters that would have been printed if dstcap was unlimited
// (not including the final `\0').
usize strrepr(char* dst, usize dstcap, const char* src, usize srclen);


// --- inline implementations ---

inline static Str str_make_cstr(Mem mem, const char* src_cstr) {
  return str_make_copy(mem, src_cstr, strlen(src_cstr));
}
inline static Str nullable str_makeroom(Str s, u32 addlen) {
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
inline static usize strfmt_u32(char buf[32], u32 v, u32 base) {
  return strfmt_u64(buf, (u64)v, base);
}
inline static usize strfmt_u8(char buf[8], u8 v, u32 base) {
  return strfmt_u64(buf, (u64)v, base);
}

error _strparse_u64(const char* src, usize len, int base, u64* result, u64 cutoff);
error _strparse_u64_base10(const char* src, usize len, u64* result);
inline static error strparse_u64(const char* src, usize len, int base, u64* result) {
  if (base == 10)
    return _strparse_u64_base10(src, len, result);
  return _strparse_u64(src, len, base, result, 0xFFFFFFFFFFFFFFFF);
}

ASSUME_NONNULL_END
