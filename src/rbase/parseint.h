#pragma once
ASSUME_NONNULL_BEGIN

static bool parseu32(const char* ptr, size_t len, int base, u32* result);
static bool parseu64(const char* ptr, size_t len, int base, u64* result);

bool parsei64(const char* ptr, size_t len, int base, i64* result);

// ---------------------------------------------------------------------------------------------
// implementation

bool _parseu64(const char* pch, size_t z, int base, u64* result, u64 cutoff);

inline static bool parseu64(const char* ptr, size_t z, int base, u64* result) {
  return _parseu64(ptr, z, base, result, 0xFFFFFFFFFFFFFFFFull);
}

inline static bool parseu32(const char* ptr, size_t z, int base, u32* result) {
  u64 r;
  bool ok = _parseu64(ptr, z, base, &r, 0xFFFFFFFFu);
  *result = (u32)r;
  return ok;
}

ASSUME_NONNULL_END
