#include "colib.h"

DEF_TEST(ascii_is) {
  for (char c = 0; c < '0'; c++) {
    assert(!ascii_isdigit(c));
    assert(!ascii_ishexdigit(c));
  }
  for (char c = '0'; c < '9'+1; c++) {
    assert(ascii_isdigit(c));
    assert(ascii_ishexdigit(c));
  }
  for (char c = 'A'; c < 'F'+1; c++) {
    assert(!ascii_isdigit(c));
    assert(ascii_ishexdigit(c));
  }
  for (char c = 'a'; c < 'f'+1; c++) {
    assert(!ascii_isdigit(c));
    assert(ascii_ishexdigit(c));
  }
  for (char c = 'f'+1; c < 'z'+1; c++) {
    assert(!ascii_isdigit(c));
    assert(!ascii_ishexdigit(c));
  }
}


DEF_TEST(utf8_len) {
  struct {
    const char* input;
    usize expected_len;
    usize expected_printlen;
    UnicodeLenFlags flags;
  } tests[] = {
    { "hello", 5, 5 },
    { "你好",   2, 2 },
    { "नमस्ते",  6, 6 },
    { "مرحبا", 5, 5 },
    { "hej   \x1B[31mredfg \x1B[44mbluebg\x1B[49m redfg\x1B[39m", 44, 40 },
    { "hello \x1B[31mredfg \x1B[44mbluebg\x1B[49m redfg\x1B[39m", 24, 24, UC_LFL_SKIP_ANSI },
    { "fancy \x1B[38;5;203mred\x1B[39m", 9, 9, UC_LFL_SKIP_ANSI },
  };
  char tmpbuf[512];
  for (usize i = 0; i < countof(tests); i++) {
    auto t = tests[i];

    usize len = utf8_len((const u8*)t.input, strlen(t.input), t.flags);
    if (len != t.expected_len) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t.input, strlen(t.input));
      assertf(0,"tests[%zu]: utf8_len(%s) => %zu (expected %zu)",
        i, tmpbuf, len, t.expected_len);
    }

    len = utf8_printlen((const u8*)t.input, strlen(t.input), t.flags);
    if (len != t.expected_printlen) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t.input, strlen(t.input));
      assertf(0,"tests[%zu]: utf8_printlen(%s) => %zu (expected %zu)",
        i, tmpbuf, len, t.expected_printlen);
    }
  }
}
