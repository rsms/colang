#include "coimpl.h"
#include "hash.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define XXH_INLINE_ALL
#include "xxhash.h"
#pragma GCC diagnostic pop

static u64 fastrand_state = 0;

void fastrand_seed(u64 seed) {
  fastrand_state = seed;
}

u32 fastrand() {
  #if 1
    // wyrand (https://github.com/wangyi-fudan/wyhash)
    fastrand_state += 0xa0761d6478bd642f;
    __uint128_t r =
      (__uint128_t)fastrand_state * (__uint128_t)(fastrand_state ^ 0xe7037ed1a0b428db);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      u64 hi = ((u64*)&r)[0], lo = ((u64*)&r)[1];
    #else
      u64 hi = ((u64*)&r)[1], lo = ((u64*)&r)[0];
    #endif
    return (u32)(hi ^ lo);
  #else
    // Implement xorshift64+: 2 32-bit xorshift sequences added together.
    // Shift triplet [17,7,16] was calculated as indicated in Marsaglia's
    // Xorshift paper: https://www.jstatsoft.org/article/view/v008i14/xorshift.pdf
    // This generator passes the SmallCrush suite, part of TestU01 framework:
    // http://simul.iro.umontreal.ca/testu01/tu01.html
    u32 s1 = ((u32*)&fastrand_state)[0];
    u32 s0 = ((u32*)&fastrand_state)[1];
    s1 ^= s1 << 17;
    s1 = s1 ^ s0 ^ s1>>7 ^ s0>>16;
    ((u32*)&fastrand_state)[0] = s0;
    ((u32*)&fastrand_state)[1] = s1;
    return s0 + s1;
  #endif
}

usize hash_i32(const i32* v, usize seed) {
  #if USIZE_MAX >= 0xFFFFFFFFFFFFFFFFu
    return (usize)XXH64(v, sizeof(i32), seed);
  #else
    return (usize)XXH32(v, sizeof(i32), seed);
  #endif
}
