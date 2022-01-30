#include "coimpl.h"
#include "parseint.h"
#include "test.h"


#define isdigit(c) (((unsigned)(c) - '0') < 10)
#define isalpha_uc(c) (((unsigned)(c) - 'A') < ('Z'-'A'))
#define isalpha_lc(c) (((unsigned)(c) - 'a') < ('z'-'a'))
// #define ishexdigit(c) (isdigit(c) || ((unsigned)c|32)-'a' < 6)


bool _parseint_u64_base10(const char* src, usize srclen, u64* result) {
  u64 n = 0;
  const char* end = src + srclen;
  while (src != end && isdigit(*src))
    n = 10*n + (*src++ - '0');
  *result = n;
  return src == end;
}


bool _parseint_u64(const char* src, usize z, int base, u64* result, u64 cutoff) {
  assert(base >= 2 && base <= 36);
  const char* s = src;
  const char* end = src + z;
  u64 acc = 0;
  u64 cutlim = cutoff % base;
  cutoff /= base;
  int any = 0;
  for (char c = *s; s != end; c = *++s) {
    if (isdigit(c)) {
      c -= '0';
    } else if (isalpha_uc(c)) {
      c -= 'A' - 10;
    } else if (isalpha_lc(c)) {
      c -= 'a' - 10;
    } else {
      return false;
    }
    if (c >= base) {
      return false;
    }
    if (any < 0 || acc > cutoff || (acc == cutoff && (u64)c > cutlim)) {
      any = -1;
    } else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if (any < 0 ||  /* more digits than what fits in acc */
      any == 0)
  {
    return false;
  }
  *result = acc;
  return true;
}


bool parseint_i64(const char* src, usize z, int base, i64* result) {
  while (z > 0 && *src == '-') {
    src++;
    z--;
  }
  i64 r;
  if (!parseint_u64(src, z, base, (u64*)&r))
    return false;
  if (*src == '-')
    r = -r;
  *result = r;
  return true;
}


#ifdef CO_TESTING_ENABLED

#define check_uresult(fun, input, base, ok, expect, result) { \
  assertf(ok, #fun "(base=%d) failed with input \"%s\"", base, input); \
  assertf(result == expect, \
    "\ninput:   \"%s\" base=%d\nexpected %20llu\ngot      %20llu\n", \
    input, base, expect, result); \
}

#define check_sresult(fun, input, base, ok, expect, result) { \
  assertf(ok, #fun "(base=%d) failed with input \"%s\"", base, input); \
  assertf(result == expect, \
    "\ninput:   \"%s\" base=%d\nexpected %20lld\ngot      %20lld\n", \
    input, base, expect, result); \
}

static void test_u64(const char* cstr, int base, u64 expectnum) {
  u64 result = 0;
  bool ok = parseint_u64(cstr, strlen(cstr), base, &result);
  check_uresult(parseint_u64, cstr, base, ok, expectnum, result);
  if (base == 10) {
    // test the generic implementation for base-10
    bool ok = _parseint_u64(cstr, strlen(cstr), base, &result, 0xFFFFFFFFFFFFFFFF);
    check_uresult(_parseint_u64, cstr, base, ok, expectnum, result);
  }
}

static void test_i64(const char* cstr, int base, i64 expectnum) {
  i64 result = 0;
  bool ok = parseint_i64(cstr, strlen(cstr), base, &result);
  check_sresult(parseint_i64, cstr, base, ok, expectnum, result);
}

static void test_u32(const char* cstr, int base, u32 expectnum) {
  u32 result = 0;
  bool ok = parseint_u32(cstr, strlen(cstr), base, &result);
  check_uresult(parseint_u32, cstr, base, ok, (u64)expectnum, (u64)result);
  if (base == 10) {
    // test the generic implementation for base-10
    u64 result = 0;
    bool ok = _parseint_u64(cstr, strlen(cstr), base, &result, 0xFFFFFFFF);
    check_uresult(_parseint_u64_as_u32, cstr, base, ok, (u64)expectnum, result);
  }
}

DEF_TEST(parseint) {
  for (int i = 0; i < 'Z'-'A'; i++) {
    assertf(isalpha_uc('A'+i), "'%c'", 'A'+i);
    assertf(isalpha_lc('a'+i), "'%c'", 'a'+i);
  }
  assert(!isalpha_uc('Z'+1));
  assert(!isalpha_lc('z'+1));

  test_u32("FFAA3191", 16, 0xFFAA3191);
  test_u32("0", 16, 0);
  test_u32("000000", 16, 0);
  test_u32("7FFFFFFF", 16, 0x7FFFFFFF);
  test_u32("EFFFFFFF", 16, 0xEFFFFFFF);
  test_u32("FFFFFFFF", 16, 0xFFFFFFFF);

  test_i64("7fffffffffffffff",       16, 0x7FFFFFFFFFFFFFFF);
  test_i64("9223372036854775807",    10, 0x7FFFFFFFFFFFFFFF);
  test_i64("777777777777777777777",  8,  0x7FFFFFFFFFFFFFFF);
  test_i64("1y2p0ij32e8e7",          36, 0x7FFFFFFFFFFFFFFF);

  test_i64("-8000000000000000",       16, -0x8000000000000000);
  test_i64("-9223372036854775808",    10, -0x8000000000000000);
  test_i64("-1000000000000000000000",  8, -0x8000000000000000);
  test_i64("-1y2p0ij32e8e8",          36, -0x8000000000000000);

  test_u64("7fffffffffffffff",       16, 0x7FFFFFFFFFFFFFFF);
  test_u64("9223372036854775807",    10, 0x7FFFFFFFFFFFFFFF);
  test_u64("777777777777777777777",  8,  0x7FFFFFFFFFFFFFFF);
  test_u64("1y2p0ij32e8e7",          36, 0x7FFFFFFFFFFFFFFF);

  test_u64("efffffffffffffff",       16, 0xEFFFFFFFFFFFFFFF); // this caught a bug once

  test_u64("ffffffffffffffff",       16, 0xFFFFFFFFFFFFFFFF);
  test_u64("18446744073709551615",   10, 0xFFFFFFFFFFFFFFFF);
  test_u64("1777777777777777777777", 8,  0xFFFFFFFFFFFFFFFF);
  test_u64("3w5e11264sgsf",          36, 0xFFFFFFFFFFFFFFFF);
}

#endif // CO_TESTING_ENABLED
