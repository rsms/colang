#pragma once
#include "defs.h"

u32 hash_fnv1a32(const u8* buf, size_t len);
u64 hash_fnv1a64(const u8* buf, size_t len);
