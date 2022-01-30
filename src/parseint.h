// parse integers in text
#pragma once
ASSUME_NONNULL_BEGIN

static bool parseint_u32(const char* src, usize srclen, int base, u32* result);
static bool parseint_u64(const char* src, usize srclen, int base, u64* result);
bool parseint_i64(const char* src, usize srclen, int base, i64* result);

// --------------------------------------------------------------------------------------
// implementation

bool _parseint_u64(const char* pch, usize srclen, int base, u64* result, u64 cutoff);
bool _parseint_u64_base10(const char* src, usize srclen, u64* result);

inline static bool parseint_u64(const char* src, usize srclen, int base, u64* result) {
  if (base == 10)
    return _parseint_u64_base10(src, srclen, result);
  return _parseint_u64(src, srclen, base, result, 0xFFFFFFFFFFFFFFFF);
}

inline static bool parseint_u32(const char* src, usize srclen, int base, u32* result) {
  u64 r;
  bool ok = (
    base == 10 ? (_parseint_u64_base10(src, srclen, &r) && r <= 0xFFFFFFFF) :
    _parseint_u64(src, srclen, base, &r, 0xFFFFFFFF) );
  *result = (u32)r;
  return ok;
}

ASSUME_NONNULL_END
