#include "colib.h"

//———————————————————————————————————————————————————————————————————————————————————————
// number parsing test cases

static struct { const char* input; u32 base; u64 expect_result; error expect_err; }
sparse_u64_tests[] = {
  {"0", 16, 0},
  {"000000000000000000000000000000000000000000000000000000000000000000", 16, 0},
  {"000000000000000000000000000000000000000000000000000000000000000001", 16, 1},

  {"00ffffffffffffffff",       16, U64_MAX},
  {"0018446744073709551615",   10, U64_MAX},
  {"001777777777777777777777", 8,  U64_MAX},
  {"003w5e11264sgsf",          36, U64_MAX},

  {"007fffffffffffffff",       16, I64_MAX},
  {"009223372036854775807",    10, I64_MAX},
  {"00777777777777777777777",  8,  I64_MAX},
  {"001y2p0ij32e8e7",          36, I64_MAX},
  {"00efffffffffffffff",       16, 0xefffffffffffffff},
  {"8ac7230335dc1bff",         16, 0x8ac7230335dc1bff},
};
static struct { const char* input; u32 base; i64 expect_result; error expect_err; }
sparse_i64_tests[] = {
  {"",  16, 0, err_invalid}, // empty input
  {"-", 16, 0, err_invalid}, // empty input
  {" ", 16, 0, err_invalid}, // invalid input
  {";", 16, 0, err_invalid}, // invalid input

  {"8000000000000000",  16, 0, err_overflow}, // I64_MAX+1
  {"-8000000000000001", 16, 0, err_overflow}, // I64_MIN-1

  {"007fffffffffffffff",      16, I64_MAX},
  {"009223372036854775807",   10, I64_MAX},
  {"00777777777777777777777", 8,  I64_MAX},
  {"001y2p0ij32e8e7",         36, I64_MAX},

  {"-8000000000000000",         16, I64_MIN},
  {"-009223372036854775808",    10, I64_MIN},
  {"-001000000000000000000000",  8, I64_MIN},
  {"-001y2p0ij32e8e8",          36, I64_MIN},

  {"100000000",        16, 0x100000000},
  {"53e2d6238da3",     16, 0x53e2d6238da3},
  {"346dc5d638865",    16, 0x346dc5d638865},
  {"20c49ba5e353f7",   16, 0x20c49ba5e353f7},
  {"147ae147ae147ae",  16, 0x147ae147ae147ae},
  {"ccccccccccccccc",  16, 0xccccccccccccccc},
  {"de0b6b3a763ffff",  16, 0xde0b6b3a763ffff},
  {"de0b6b3a7640000",  16, 0xde0b6b3a7640000},
};
static struct { const char* input; u32 base; u32 expect_result; error expect_err; }
sparse_u32_tests[] = {
  {"10000k000",  16, 0, err_invalid},
  {"100000000",  16, 0, err_overflow},

  {"FFAA3191",   16, 0xffaa3191},
  {"0",          16, 0},
  {"000000",     16, 0},
  {"007FFFFFFF", 16, 0x7fffffff},
  {"00EFFFFFFF", 16, 0xefffffff},
  {"00FFFFFFFF", 16, 0xffffffff},
};

//———————————————————————————————————————————————————————————————————————————————————————
// number formatting test cases

static struct { f64 input; int ndec; const char* expect; }
fmt_f64_tests[] = {
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

//———————————————————————————————————————————————————————————————————————————————————————
// test functions

DEF_TEST(sparse) {
  #define CHECK_SPARSE(SPARSE_FUN, RESULT_T, STRFMT, t) { \
    RESULT_T result = 0; \
    error err = SPARSE_FUN((t).input, strlen((t).input), (t).base, &result); \
    if ((t).expect_err) { \
      if (err == 0) { \
        assertf(err == (t).expect_err, \
          "%s(\"%s\", base=%u)\n  expected error #%d %s\n  got result " \
          STRFMT " (0x%llx, no error)\n", \
          #SPARSE_FUN, (t).input, (t).base, (t).expect_err, error_str((t).expect_err), \
          result + 1, (u64)result ); \
      } else { \
        assertf(err == (t).expect_err, \
          "%s(\"%s\", base=%u)\n  expected error #%d %s\n  got      error #%d %s\n", \
          #SPARSE_FUN, (t).input, (t).base, (t).expect_err, error_str((t).expect_err), \
          err, error_str(err) ); \
      } \
    } else { \
      assertf(err == 0, \
        "%s(\"%s\", base=%u)\n  returned error: #%d %s", \
        #SPARSE_FUN, (t).input, (t).base, err, error_str(err)); \
      assertf(result == (t).expect_result, \
        "%s(\"%s\", base=%u)\n  expected " STRFMT "\n  got      " STRFMT "\n", \
        #SPARSE_FUN, (t).input, (t).base, (t).expect_result, result ); \
    } \
  }
  for (usize i = 0; i < countof(sparse_u64_tests); i++)
    CHECK_SPARSE(sparse_u64, u64, "0x%llx", sparse_u64_tests[i])
  for (usize i = 0; i < countof(sparse_i64_tests); i++)
    CHECK_SPARSE(sparse_i64, i64, "%20lld", sparse_i64_tests[i])
  for (usize i = 0; i < countof(sparse_u32_tests); i++)
    CHECK_SPARSE(sparse_u32, u32, "0x%x", sparse_u32_tests[i])
  // error sparse_i32(const char* src, usize len, u32 base, i32* result);

  #undef CHECK_SPARSE
}


DEF_TEST(abuf_f64) {
  char buf[64];
  for (usize i = 0; i < countof(fmt_f64_tests); i++) {
    ABuf s = abuf_make(buf, sizeof(buf));
    abuf_f64(&s, fmt_f64_tests[i].input, fmt_f64_tests[i].ndec);
    abuf_terminate(&s);
    assertcstreq(buf, fmt_f64_tests[i].expect);
  }
}


DEF_TEST(abuf_zero) {
  {
    // abuf initialized using abuf_make should handle zero-size buffer.
    // It does this by using a temporary "static char" for p in the case size==0.
    // Note that abuf_init instead assert(size>0).
    char c = 0x7f;
    ABuf s = abuf_make(&c, 0);
    abuf_c(&s, 'a');
    abuf_terminate(&s);
    asserteq(s.len, 1);
    asserteq(c, 0x7f); // untouched
  }
}


DEF_TEST(abuf_append) {
  {
    char buf[6];
    ABuf s = abuf_make(buf, sizeof(buf));
    abuf_c(&s, 'a');
    abuf_c(&s, 'b');
    abuf_c(&s, 'c');
    abuf_terminate(&s);
    asserteq(s.len, 3);
    asserteq(strlen(buf), 3);
  }
  {
    char buf[3];
    ABuf s = abuf_make(buf, sizeof(buf));
    abuf_c(&s, 'a');
    abuf_c(&s, 'b');
    abuf_c(&s, 'c');
    asserteq(buf[2], 'c');
    abuf_c(&s, 'd');
    asserteq(buf[2], 'd');
    abuf_terminate(&s);
    asserteq(s.len, 4);
    asserteq(strlen(buf), 2);
    asserteq(buf[0], 'a');
    asserteq(buf[1], 'b');
    asserteq(buf[2], 0);
  }
  {
    char buf[6];
    ABuf s = abuf_make(buf, sizeof(buf));
    abuf_append(&s, "abcd", 4);
    abuf_append(&s, "efgh", 4);
    abuf_append(&s, "ijkl", 4);
    abuf_terminate(&s);
    asserteq(s.len, 12);
    asserteq(strlen(buf), 5);
    assert(memcmp(buf, "abcde\0", 6) == 0);
  }
  { // overflow
    char buf[6];
    ABuf s = abuf_make(buf, sizeof(buf));
    s.len = USIZE_MAX - 1;
    abuf_append(&s, "abc", 3);
    abuf_terminate(&s);
    asserteq(s.len, USIZE_MAX);
    asserteq(strlen(buf), 3);
    assert(memcmp(buf, "abc\0", 4) == 0);
  }
}


DEF_TEST(abuf_repr) {
  #define T(input, expect)                       \
    ABuf s = abuf_make(buf, sizeof(buf));     \
    abuf_repr(&s, input, strlen(input));   \
    abuf_terminate(&s);                          \
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
