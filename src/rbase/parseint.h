#pragma once
ASSUME_NONNULL_BEGIN

bool parseu32(const char* ptr, size_t len, int base, u32* result);
bool parseu64(const char* ptr, size_t len, int base, u64* result);

bool parsei32(const char* ptr, size_t len, int base, i32* result);
bool parsei64(const char* ptr, size_t len, int base, i64* result);

ASSUME_NONNULL_END
