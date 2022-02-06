// hash -- hashing and PRNG
#pragma once
ASSUME_NONNULL_BEGIN

void fastrand_seed(u64 seed);
u32 fastrand();

usize hash_i32(const i32* v, usize seed);

ASSUME_NONNULL_END
