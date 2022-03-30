// byte string functions (See unicode.h for text processing)
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

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
// StrSlice is an immutable view into string data stored elsewhere
typedef struct StrSlice { const char* p; usize len; } StrSlice;
#define strslice_make(cstr) ((StrSlice){ (cstr), strlen(cstr) })

//———————————————————————————————————————————————————————————————————————————————————————
// Str is a mutable growable string, based on a dynamic array
typedef Array(char) Str;

// functions provided by array implementation (str_* is a thin veneer on array_*)
static Str  str_make(char* nullable storage, usize storagesize);
static void str_init(Str* a, char* nullable storage, usize storagesize);
static void str_clear(Str* a);
static void str_free(Str* a);
static char str_at(Str* a, u32 index);
static char str_at_safe(Str* a, u32 index);
static bool str_push(Str* a, char c);
#define     str_appendc str_push
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
bool str_terminate(Str*); // writes \0 to v[len] but does not increment len

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
static ABuf* abuf_reprhex(ABuf* s, const void* p, usize len); // "68656c6c6f0a776f726c6400"
static ABuf* abuf_reprhexsp(ABuf* s, const void* p, usize len); // "68 65 6c 6c 6f 0a ..."
ABuf* abuf_fmt(ABuf* s, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
ABuf* abuf_fmtv(ABuf* s, const char* fmt, va_list);

static usize abuf_terminate(ABuf* s); // sets last byte to \0 and returns s->len
static usize abuf_avail(const ABuf* s); // bytes available to write (not including final \0)
bool abuf_endswith(const ABuf* s, const char* str, usize len);
// TODO: trim

// Here is a template for functions that uses ABuf:
//
// // It writes at most bufcap-1 of the characters to the output buf (the bufcap'th
// // character then gets the terminating '\0'). If the return value is greater than or
// // equal to the bufcap argument, buf was too short and some of the characters were
// // discarded. The output is always null-terminated, unless bufcap is 0.
// // Returns the number of characters that would have been written if bufcap was
// // unlimited (not including the final `\0').
// usize myfmt(char* buf, usize bufcap, int somearg) {
//   ABuf s = abuf_make(buf, bufcap);
//   // call abuf_append functions here
//   return abuf_terminate(&s);
// }
//
// Typical use:
//   usize n = myfmt(buf, bufcap, 123);
//   if (n >= bufcap)
//     printf("not enough room in buf\n");
//
extern char abuf_zeroc;
#define abuf_make(p,size) ({ /* ABuf abuf_make(char* buf, usize bufcap)               */\
  usize z__ = (usize)(size); char* p__ = (p);                                           \
  UNLIKELY(z__ == 0) ? (ABuf){&abuf_zeroc,&abuf_zeroc,0} : (ABuf){ p__, p__+z__-1, 0 }; \
})



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
  *s->p = 0;
  return s->len;
}
inline static usize abuf_avail(const ABuf* s) { // SANS terminating \0
  return (usize)(uintptr)(s->lastp - s->p);
}

ABuf* _abuf_reprhex(ABuf* s, const void* p, usize len, bool spaced);
inline static ABuf* abuf_reprhex(ABuf* s, const void* p, usize len) {
  return _abuf_reprhex(s, p, len, false);
}
inline static ABuf* abuf_reprhexsp(ABuf* s, const void* p, usize len) {
  return _abuf_reprhex(s, p, len, true);
}


END_INTERFACE
