#include "coimpl.h"
#include "str.h"
#include "test.c"


#define check_err(fun, input, base, err)                             \
  assertf(err==0, #fun "(base=%d) failed with input \"%s\": #%d %s", \
    err, error_str(err), base, input);

#define check_uint_result(fun, input, base, err, expect, result) {   \
  check_err(fun, input, base, err);                                  \
  assertf(result == expect,                                          \
    "\ninput:   \"%s\" base=%d\nexpected %20llu\ngot      %20llu\n", \
    input, base, expect, result);                                    \
}

#define check_sint_result(fun, input, base, err, expect, result) {   \
  check_err(fun, input, base, err);                                  \
  assertf(result == expect,                                          \
    "\ninput:   \"%s\" base=%d\nexpected %20lld\ngot      %20lld\n", \
    input, base, expect, result);                                    \
}

static void test_parse_u64(const char* cstr, int base, u64 expectnum) {
  u64 result = 0;
  error err = strparse_u64(cstr, strlen(cstr), base, &result);
  check_uint_result(strparse_u64, cstr, base, err, expectnum, result);
  if (base == 10) {
    // test the generic implementation for base-10
    error err = _strparse_u64(cstr, strlen(cstr), base, &result, 0xFFFFFFFFFFFFFFFF);
    check_uint_result(_strparse_u64, cstr, base, err, expectnum, result);
  }
}

static void test_parse_i64(const char* cstr, int base, i64 expectnum) {
  i64 result = 0;
  error err = strparse_i64(cstr, strlen(cstr), base, &result);
  check_sint_result(strparse_i64, cstr, base, err, expectnum, result);
}

static void test_parse_u32(const char* cstr, int base, u32 expectnum) {
  u32 result = 0;
  error err = strparse_u32(cstr, strlen(cstr), base, &result);
  check_uint_result(strparse_u32, cstr, base, err, (u64)expectnum, (u64)result);
  if (base == 10) {
    // test the generic implementation for base-10
    u64 result = 0;
    error err = _strparse_u64(cstr, strlen(cstr), base, &result, 0xFFFFFFFF);
    check_uint_result(_strparse_u64_as_u32, cstr, base, err, (u64)expectnum, result);
  }
}

DEF_TEST(strparse) {
  u32 res_u32;
  assert(strparse_u32("100000000", strlen("100000000"), 16, &res_u32) == err_overflow);
  assert(strparse_u32("10000k000", strlen("10000k000"), 16, &res_u32) == err_invalid);

  test_parse_u32("FFAA3191",   16, 0xFFAA3191);
  test_parse_u32("0",          16, 0);
  test_parse_u32("000000",     16, 0);
  test_parse_u32("007FFFFFFF", 16, 0x7FFFFFFF);
  test_parse_u32("00EFFFFFFF", 16, 0xEFFFFFFF);
  test_parse_u32("00FFFFFFFF", 16, 0xFFFFFFFF);

  test_parse_i64("007fffffffffffffff",       16, 0x7FFFFFFFFFFFFFFF);
  test_parse_i64("009223372036854775807",    10, 0x7FFFFFFFFFFFFFFF);
  test_parse_i64("00777777777777777777777",  8,  0x7FFFFFFFFFFFFFFF);
  test_parse_i64("001y2p0ij32e8e7",          36, 0x7FFFFFFFFFFFFFFF);

  test_parse_i64("-0008000000000000000",     16, -0x8000000000000000);
  test_parse_i64("-009223372036854775808",     10, -0x8000000000000000);
  test_parse_i64("-001000000000000000000000",  8, -0x8000000000000000);
  test_parse_i64("-001y2p0ij32e8e8",      36, -0x8000000000000000);

  test_parse_u64("007fffffffffffffff",       16, 0x7FFFFFFFFFFFFFFF);
  test_parse_u64("009223372036854775807",    10, 0x7FFFFFFFFFFFFFFF);
  test_parse_u64("00777777777777777777777",  8,  0x7FFFFFFFFFFFFFFF);
  test_parse_u64("001y2p0ij32e8e7",          36, 0x7FFFFFFFFFFFFFFF);

  test_parse_u64("00efffffffffffffff",       16, 0xEFFFFFFFFFFFFFFF); // caught a bug once

  test_parse_u64("00ffffffffffffffff",       16, 0xFFFFFFFFFFFFFFFF);
  test_parse_u64("0018446744073709551615",   10, 0xFFFFFFFFFFFFFFFF);
  test_parse_u64("001777777777777777777777", 8,  0xFFFFFFFFFFFFFFFF);
  test_parse_u64("003w5e11264sgsf",          36, 0xFFFFFFFFFFFFFFFF);
}
