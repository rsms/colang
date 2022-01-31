#include "coimpl.h"
#include "sbuf.h"
#include "test.h"
#include "str.h" // strfmtu64

#ifdef CO_WITH_LIBC
  #include <stdio.h>
#endif


// SBUF_AVAIL: available space at s->p, not including null terminator
#define SBUF_AVAIL(s) ((usize)(uintptr)((s)->lastp - (s)->p))


void sbuf_append(SBuf* s, const char* p, usize len) {
  usize z = MIN(len, SBUF_AVAIL(s));
  memcpy(s->p, p, z);
  s->p += z;
  if (check_add_overflow(s->len, len, &s->len))
    s->len = USIZE_MAX;
}


void sbuf_appendu32(SBuf* s, u32 v, u32 base) {
  char buf[32];
  u32 n = strfmtu32(buf, v, base);
  return sbuf_append(s, buf, n);
}


void sbuf_appendf64(SBuf* s, f64 v, int ndec) {
  #ifndef CO_WITH_LIBC
    #warning TODO implement for non-libc
    assert(!"not implemented");
    // TODO: consider using fmt_fp (stdio/vfprintf.c) in musl
  #else
    usize cap = SBUF_AVAIL(s);
    int n;
    if (ndec > -1) {
      n = snprintf(s->p, cap+1, "%.*f", ndec, v);
    } else {
      n = snprintf(s->p, cap+1, "%f", v);
    }
    if (UNLIKELY( n <= 0 ))
      return;
    if (ndec < 0) {
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
}


DEF_TEST(sbuf_f64) {
  char buf[64];
  {
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_appendf64(&s, 123.456, -1);
    sbuf_terminate(&s);
    assertcstreq(buf, "123.456");
  }
  struct { f64 input; int ndec; const char* expect; } tests[] = {
    {0.0, -1, "0.0"},
    {-0.0, -1, "-0.0"},
    {1.0, -1, "1.0"},
    {-1.0, -1, "-1.0"},
    {1.0, 4, "1.0000"},
    {0.123456789, -1, "0.123457"},
    {123456789.123456789, -1, "123456789.123457"},
    {123.456, -1, "123.456"},
    {123.456,  1, "123.5"},
    {123.456,  2, "123.46"},
    {123.456,  3, "123.456"},
    {123.456,  4, "123.4560"},
  };
  for (usize i = 0; i < countof(tests); i++) {
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_appendf64(&s, tests[i].input, tests[i].ndec);
    sbuf_terminate(&s);
    assertcstreq(buf, tests[i].expect);
  }
}


DEF_TEST(sbuf) {
  {
    char buf[6];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_appendc(&s, 'a');
    sbuf_appendc(&s, 'b');
    sbuf_appendc(&s, 'c');
    sbuf_terminate(&s);
    asserteq(s.len, 3);
    asserteq(strlen(buf), 3);
  }
  {
    char buf[3];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_appendc(&s, 'a');
    sbuf_appendc(&s, 'b');
    sbuf_appendc(&s, 'c');
    asserteq(buf[2], 'c');
    sbuf_appendc(&s, 'd');
    asserteq(buf[2], 'd');
    sbuf_terminate(&s);
    asserteq(s.len, 4);
    asserteq(strlen(buf), 2);
    asserteq(buf[0], 'a');
    asserteq(buf[1], 'b');
    asserteq(buf[2], 0);
  }
  {
    char buf[6];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_append(&s, "abcd", 4);
    sbuf_append(&s, "efgh", 4);
    sbuf_append(&s, "ijkl", 4);
    sbuf_terminate(&s);
    asserteq(s.len, 12);
    asserteq(strlen(buf), 5);
    assert(memcmp(buf, "abcde\0", 6) == 0);
  }
  { // overflow
    char buf[6];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    s.len = USIZE_MAX - 1;
    sbuf_append(&s, "abc", 3);
    sbuf_terminate(&s);
    asserteq(s.len, USIZE_MAX);
    asserteq(strlen(buf), 3);
    assert(memcmp(buf, "abc\0", 4) == 0);
  }
}
