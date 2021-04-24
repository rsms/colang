#include "rbase.h"

static bool _parseu64(const char* pch, size_t z, int base, u64* result, u64 cutoff) {
  assert(base >= 2 && base <= 36);
  const char* s = pch;
  const char* end = pch + z;
  u64 acc = 0;
  u64 cutlim = cutoff % base;
  cutoff /= base;
  int any = 0;
  for (char c = *s; s != end; c = *++s) {
    if (c >= '0' && c <= '9') {
      c -= '0';
    } else if (c >= 'A' && c <= 'Z') {
      c -= 'A' - 10;
    } else if (c >= 'a' && c <= 'z') {
      c -= 'a' - 10;
    } else {
      return false;
    }
    if (c >= base) {
      return false;
    }
    if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
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


bool parseu64(const char* ptr, size_t z, int base, u64* result) {
  return _parseu64(ptr, z, base, result, 0xFFFFFFFFFFFFFFFFull);
}

bool parseu32(const char* ptr, size_t z, int base, u32* result) {
  u64 r;
  bool ok = _parseu64(ptr, z, base, &r, 0xFFFFFFFFu);
  *result = (u32)r;
  return ok;
}

bool parsei64(const char* ptr, size_t z, int base, i64* result) {
  while (z > 0 && *ptr == '-') {
    ptr++;
    z--;
  }
  i64 r;
  if (!_parseu64(ptr, z, base, (u64*)&r, 0xFFFFFFFFFFFFFFFFull))
    return false;
  if (*ptr == '-')
    r = -r;
  *result = r;
  return true;
}

bool parsei32(const char* ptr, size_t z, int base, i32* result) {
  // if (len == 0)
  //   return false;
  // if (*ptr == '-') {
  //   ptr++;
  //   len--;
  // }
  // if (!parseu64(ptr, len, base, (*u64)result))
  //   return false;
  // if (*ptr == '-')
  //   *result = -(*result)
  return true;
}

R_UNIT_TEST(parseint) {
  #define T32(cstr, base, expectnum) (({ \
    u32 result = 0; \
    bool ok = parseu32(cstr, strlen(cstr), base, &result); \
    assert(ok || !cstr); \
    if (result != expectnum) { fprintf(stderr, "result: 0x%X\n", result); } \
    assert(result == expectnum || !("got: "&& result)); \
  }))

  #define T64(cstr, base, expectnum) (({ \
    u64 result = 0; \
    bool ok = parseu64(cstr, strlen(cstr), base, &result); \
    assert(ok || !cstr); \
    if (result != expectnum) { fprintf(stderr, "result: 0x%llX\n", result); } \
    assert(result == expectnum || !("got: "&& result)); \
  }))

  T32("FFAA3191", 16, 0xFFAA3191);
  T32("0", 16, 0);
  T32("000000", 16, 0);
  T32("7FFFFFFF", 16, 0x7FFFFFFF);
  T32("EFFFFFFF", 16, 0xEFFFFFFF);
  T32("FFFFFFFF", 16, 0xFFFFFFFF);

  // fits in s64
  T64("7fffffffffffffff",       16, 0x7FFFFFFFFFFFFFFF);
  T64("9223372036854775807",    10, 0x7FFFFFFFFFFFFFFF);
  T64("777777777777777777777",  8,  0x7FFFFFFFFFFFFFFF);
  T64("1y2p0ij32e8e7",          36, 0x7FFFFFFFFFFFFFFF);

  T64("efffffffffffffff",       16, 0xEFFFFFFFFFFFFFFF); // this caught a bug once

  T64("ffffffffffffffff",       16, 0xFFFFFFFFFFFFFFFF);
  T64("18446744073709551615",   10, 0xFFFFFFFFFFFFFFFF);
  T64("1777777777777777777777", 8,  0xFFFFFFFFFFFFFFFF);
  T64("3w5e11264sgsf",          36, 0xFFFFFFFFFFFFFFFF);
}
