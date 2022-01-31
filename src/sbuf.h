// SBuf -- limited string output buffer
#pragma once
ASSUME_NONNULL_BEGIN

// SBuf is a string output buffer for implementing snprintf-style functions which
// writes to a limited buffer and separately keeps track of the number of bytes
// that are appended independent of the buffer's limit.
//
// Here is a template for use with functions that uses SBuf:
//
// // It writes at most bufsize-1 of the characters to the output buf (the bufsize'th
// // character then gets the terminating '\0'). If the return value is greater than or
// // equal to the bufsize argument, buf was too short and some of the characters were
// // discarded. The output is always null-terminated, unless size is 0.
// usize myprint(char* buf, usize bufsize, int somearg) {
//   SBuf s = SBUF_INITIALIZER(buf, bufsize);
//   // call sbuf_append functions here
//   return sbuf_terminate(&s);
// }
//
typedef struct {
  char* p;
  char* lastp;
  usize len;
} SBuf;

#define SBUF_INITIALIZER(buf,bufsize) { (buf), (buf)+(bufsize)-1, 0 }

static usize sbuf_terminate(SBuf* s);
static void sbuf_appendc(SBuf* s, char c);
void sbuf_append(SBuf* s, const char* p, usize len);
void sbuf_appendu32(SBuf* s, u32 v, u32 base);
void sbuf_appendf64(SBuf* s, f64 v, int ndec);
static void sbuf_appendstr(SBuf* s, const char* cstr);

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
