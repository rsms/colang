// SBuf -- limited string-output buffer
#pragma once
ASSUME_NONNULL_BEGIN

// SBuf is a string output buffer for implementing snprintf-style functions which
// writes to a limited buffer and separately keeps track of the number of bytes
// that are appended independent of the buffer's limit.
//
// Here is a template for use with functions that uses SBuf:
//
// // It writes at most bufcap-1 of the characters to the output buf (the bufcap'th
// // character then gets the terminating '\0'). If the return value is greater than or
// // equal to the bufcap argument, buf was too short and some of the characters were
// // discarded. The output is always null-terminated, unless size is 0.
// // Returns the number of characters that would have been printed if bufcap was
// // unlimited (not including the final `\0').
// usize myprint(char* buf, usize bufcap, int somearg) {
//   SBuf s = sbuf_make(buf, bufcap);
//   // call sbuf_append functions here
//   return sbuf_terminate(&s);
// }
//
typedef struct {
  char* p;
  char* lastp;
  usize len;
} SBuf;

// sbuf_make supports using zero size
#define sbuf_make(p,size) ({                        \
  usize z__ = (usize)(size);                        \
  char* p__ = (p);                                  \
  static char x__;                                  \
  UNLIKELY(z__ == 0) ? (SBuf){ &x__, &x__, 0 }      \
                     : (SBuf){ p__, p__+z__-1, 0 }; \
})
static SBuf* sbuf_init(SBuf* s, char* buf, usize bufsize); // bufsize must be >0
static usize sbuf_terminate(SBuf* s);
static void sbuf_appendc(SBuf* s, char c);
void sbuf_append(SBuf* s, const char* p, usize len);
void sbuf_appendu64(SBuf* s, u64 v, u32 base);
void sbuf_appendu32(SBuf* s, u32 v, u32 base);
void sbuf_appendf64(SBuf* s, f64 v, int ndec);
static void sbuf_appendstr(SBuf* s, const char* cstr);
bool sbuf_endswith(const SBuf* s, const char* str, usize len);

// SBUF_AVAIL returns available space at s->p, not including null terminator
#define SBUF_AVAIL(s) ((usize)(uintptr)((s)->lastp - (s)->p))

// sbuf_appendrepr appends a printable representation of bytes, escaping characters which
// are non-printable (e.g. line feed), '"' and '\'.
void sbuf_appendrepr(SBuf* s, const char* bytes, usize len);

inline static SBuf* sbuf_init(SBuf* s, char* buf, usize bufsize) {
  assert(bufsize > 0);
  s->p = buf;
  s->lastp = buf + bufsize - 1;
  s->len = 0;
  return s;
}

inline static usize sbuf_terminate(SBuf* s) {
  *s->p = 0;
  return s->len;
}

inline static void sbuf_appendc(SBuf* s, char c) {
  *s->p = c;
  s->p = MIN(s->p + 1, s->lastp);
  s->len++;
}

inline static void sbuf_appendstr(SBuf* s, const char* cstr) {
  sbuf_append(s, cstr, strlen(cstr));
}

ASSUME_NONNULL_END
