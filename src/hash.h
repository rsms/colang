// hash -- hashing and PRNG
#pragma once
ASSUME_NONNULL_BEGIN

void fastrand_seed(u64 seed);
u32 fastrand();

uintptr hash_mem(const void* p, uintptr size, uintptr seed); // size bytes at p

// optimized implementations for fixed sizes (in bytes) known at compile time
uintptr hash_2(const void* p, uintptr seed); // 2 bytes (eg. i16, u16)
uintptr hash_4(const void* p, uintptr seed); // 4 bytes (eg. i32, u32)
uintptr hash_8(const void* p, uintptr seed); // 8 bytes (eg. i64, u64)
uintptr hash_f32(const f32* p, uintptr seed); // f32, supports ±0 and NaN
uintptr hash_f64(const f64* p, uintptr seed); // f64, supports ±0 and NaN

// uintptr hash_ptr(const void* p, uintptr seed)
// Must be a macro rather than inline function so that we can take its address.
#if UINTPTR_MAX >= 0xFFFFFFFFFFFFFFFFu
  #define hash_ptr hash_8
#else
  #define hash_ptr hash_4
#endif

ASSUME_NONNULL_END
