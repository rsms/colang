// hashing and PRNG
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// Hash -- storage type for hash results
#if defined(__wasm__)
  typedef u64 Hash;
  #define HASHCODE_MAX U64_MAX
#else
  typedef usize Hash;
  #define HASHCODE_MAX USIZE_MAX
#endif

// fastrand updates the PRNG and returns the next "random" number
u32 fastrand();
void fastrand_seed(u64 seed); // (re)sets the seed of fastrand

// hash computes a hash code for data p of size bytes length
static Hash hash(const void* p, usize size, Hash seed);

// type-specific hash functions
Hash hash_2(const void* p, Hash seed); // 2 bytes (eg. i16, u16)
Hash hash_4(const void* p, Hash seed); // 4 bytes (eg. i32, u32)
Hash hash_8(const void* p, Hash seed); // 8 bytes (eg. i64, u64)
Hash hash_f32(const f32* p, Hash seed); // f32, supports ±0 and NaN
Hash hash_f64(const f64* p, Hash seed); // f64, supports ±0 and NaN
Hash hash_mem(const void* p, usize size, Hash seed); // size bytes at p
// uintptr hash_ptr(const void** p, uintptr seed)
// (Must be a macro rather than inline function so that we can take its address.)
#if UINTPTR_MAX >= 0xFFFFFFFFFFFFFFFFu
  #define hash_ptr hash_8
#else
  #define hash_ptr hash_4
#endif

//———————————————————————————————————————————————————————————————————————————————————————
// internal interface

inline static Hash hash(const void* p, usize size, Hash seed) {
  switch (size) {
    case 2:  return hash_2(p, seed);
    case 4:  return hash_4(p, seed);
    case 8:  return hash_8(p, seed);
    default: return hash_mem(p, size, seed);
  }
}

END_INTERFACE
