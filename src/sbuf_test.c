#include "coimpl.h"
#include "sbuf.h"
#include "test.h"

DEF_TEST(sbuf_f64) {
  char buf[64];
  {
    SBuf s = sbuf_make(buf, sizeof(buf));
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
    SBuf s = sbuf_make(buf, sizeof(buf));
    sbuf_appendf64(&s, tests[i].input, tests[i].ndec);
    sbuf_terminate(&s);
    assertcstreq(buf, tests[i].expect);
  }
}

DEF_TEST(sbuf_zero) {
  {
    // sbuf initialized using sbuf_make should handle zero-size buffer.
    // It does this by using a temporary "static char" for p in the case size==0.
    // Note that sbuf_init instead assert(size>0).
    char c = 0x7f;
    SBuf s = sbuf_make(&c, 0);
    sbuf_appendc(&s, 'a');
    sbuf_terminate(&s);
    asserteq(s.len, 1);
    asserteq(c, 0x7f); // untouched
  }
}


DEF_TEST(sbuf_append) {
  {
    char buf[6];
    SBuf s = sbuf_make(buf, sizeof(buf));
    sbuf_appendc(&s, 'a');
    sbuf_appendc(&s, 'b');
    sbuf_appendc(&s, 'c');
    sbuf_terminate(&s);
    asserteq(s.len, 3);
    asserteq(strlen(buf), 3);
  }
  {
    char buf[3];
    SBuf s = sbuf_make(buf, sizeof(buf));
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
    SBuf s = sbuf_make(buf, sizeof(buf));
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
    SBuf s = sbuf_make(buf, sizeof(buf));
    s.len = USIZE_MAX - 1;
    sbuf_append(&s, "abc", 3);
    sbuf_terminate(&s);
    asserteq(s.len, USIZE_MAX);
    asserteq(strlen(buf), 3);
    assert(memcmp(buf, "abc\0", 4) == 0);
  }
}


DEF_TEST(sbuf_appendrepr) {
  #define T(input, expect)                       \
    SBuf s = sbuf_make(buf, sizeof(buf));     \
    sbuf_appendrepr(&s, input, strlen(input));   \
    sbuf_terminate(&s);                          \
    assertcstreq(expect, buf);

  {
    char buf[32];
    T("ab\3c\ed\r\n", "ab\\x03c\\x1bd\\r\\n");
  }
  {
    // does not write partial escape sequence when buffer is short.
    // i.e. instead of ending in "\x" (for "\x1b"), end before the sequence.
    char buf[11];
    T("ab\3c\e", "ab\\x03c");
  }

  #undef T
}
