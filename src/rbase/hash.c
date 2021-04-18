#include "hash.h"

u32 hash_fnv1a32(const u8* buf, size_t len) {
  const u32 prime = 0x01000193; // pow(2,24) + pow(2,8) + 0x93
  u32 hash = 0x811C9DC5; // seed
  const u8* end = buf + len;
  while (buf < end) {
    hash = (*buf++ ^ hash) * prime;
  }
  return hash;
}

u64 hash_fnv1a64(const u8* buf, size_t len) {
  const u64 prime = 0x100000001B3; // pow(2,40) + pow(2,8) + 0xb3
  u64 hash = 0xCBF29CE484222325; // seed
  const u8* end = buf + len;
  while (buf < end) {
    hash = (*buf++ ^ hash) * prime;
  }
  return hash;
}
