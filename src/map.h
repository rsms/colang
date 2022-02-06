// map -- hash table with support for arbitrary key and value types
#pragma once
#include "mem.h"
ASSUME_NONNULL_BEGIN

typedef struct hmap    hmap;
typedef struct maptype maptype;

extern const maptype kMapType_i32_i32; // i32 => i32

// map_make implements map creation.
// h and/or bucket may be non-null.
// If h != NULL, the map can be created directly in h.
// If h->buckets != NULL, bucket pointed to can be used as the first bucket.
// Upon successful return, the resulting map can be found at h.
hmap* nullable map_make(const maptype* t, hmap* nullable h, Mem, usize hint);

// map_init_small initializes a caller-managed map when hint is known to be
// at most bucketCnt at compile time. Returns h.
hmap* map_init_small(hmap* h, Mem);

// map_new_small implements map creation when hint is known to be at most bucketCnt
// at compile time and the map needs to be allocated on the heap.
// Returns NULL if memory allocation failed.
hmap* nullable map_new_small(Mem);

// map_free deallocates a map and all of its internal resources.
// h is invalid after this call (unless a non-null h was initially passed to map_make.)
void map_free(const maptype* t, hmap* h);

// map_access returns a pointer to h[key].
// h can be NULL as a convenience (returns NULL early if so.)
// Returns pointer to value storage, or NULL if key is not present in the map.
void* nullable map_access(const maptype* t, hmap* nullable h, void* key);

// map_assign is like map_access, but allocates a slot for the key if it is not present
// in the map. Returns pointer to value storage.
void* nullable map_assign(const maptype* t, hmap* h, void* key);

// map_delete removes the entry for key.
// Returns a pointer to the removed value if found, or NULL if not found.
void* nullable map_delete(const maptype* t, hmap* h, void* key);

ASSUME_NONNULL_END
