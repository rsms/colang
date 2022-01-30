// parse integers in text
#pragma once
ASSUME_NONNULL_BEGIN

// these functions return err_invalid if the input is not valid, err_overflow on overflow

static error parseint_u32(const char* src, usize srclen, int base, u32* result);
static error parseint_u64(const char* src, usize srclen, int base, u64* result);
       error parseint_i64(const char* src, usize srclen, int base, i64* result);

// --------------------------------------------------------------------------------------
// implementation

error _parseint_u64(const char* pch, usize srclen, int base, u64* result, u64 cutoff);
error _parseint_u64_base10(const char* src, usize srclen, u64* result);

inline static error parseint_u64(const char* src, usize srclen, int base, u64* result) {
  if (base == 10)
    return _parseint_u64_base10(src, srclen, result);
  return _parseint_u64(src, srclen, base, result, 0xFFFFFFFFFFFFFFFF);
}

inline static error parseint_u32(const char* src, usize srclen, int base, u32* result) {
  u64 r;
  error err;
  if (base == 10) {
    err = _parseint_u64_base10(src, srclen, &r);
    if (err == 0 && r > 0xFFFFFFFF)
      err = err_overflow;
  } else {
    err = _parseint_u64(src, srclen, base, &r, 0xFFFFFFFF);
  }
  *result = (u32)r;
  return err;
}

ASSUME_NONNULL_END
