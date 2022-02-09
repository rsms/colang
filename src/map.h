// map -- hash table with support for arbitrary key and value types
#pragma once
#include "mem.h"
#include "hash.h"
ASSUME_NONNULL_BEGIN

typedef struct HMap     HMap;     // hash map
typedef struct HMapType HMapType; // describes types of keys and values of a map

struct HMap {
  usize count;     // # live cells == size of map
  u8    flags;     // (hflag)
  u8    B;         // log2 of # of buckets (can hold up to loadFactor * 2^B items)
  u16   noverflow; // approximate number of overflow buckets; see incrnoverflow
  u32   hash0;     // hash seed
  void* buckets;   // (bmap*) array of 2^B Buckets. may be nil if count==0.
  void* nullable oldbuckets; // (bmap*)
    // previous bucket array of half the size, non-nil only when growing
  uintptr nevacuate;
    // progress counter for evacuation (buckets less than this have been evacuated)
  struct HMapExtra* nullable extra; // optional fields
};
static_assert(offsetof(HMap,count) == 0, "count must be first field of HMap");

// predefined map types
extern const HMapType kMapType_i32_i32; // i32 => i32
extern const HMapType kMapType_ptr_ptr; // void* => void*

// map_make implements map creation.
// If h != NULL, the map can be created directly in h.
// If h->buckets != NULL, bucket pointed to can be used as the first bucket.
// Returns NULL if memory allocation failed.
HMap* nullable map_make(const HMapType* t, HMap* nullable h, Mem, usize hint);

// map_init_small initializes a caller-managed map when hint is known to be
// at most 8 (bucketCnt) at compile time. h must be zeroed-initialized or be a reused map.
// Returns h as a convenience.
static HMap* map_init_small(HMap* h);

// map_new_small implements map creation when hint is known to be at most 8 (bucketCnt)
// and the map needs to be allocated with mem.
// Returns NULL if memory allocation failed.
HMap* nullable map_new_small(Mem);

// map_len returns the number of entries stored at h
inline static usize map_len(const HMap* h) { return h->count; }

// map_access returns a pointer to h[key].
// h can be NULL as a convenience (returns NULL early if so.)
// Returns pointer to value storage, or NULL if key is not present in the map.
void* nullable map_access(const HMapType* t, const HMap* nullable h, void* key);

// map_assign is like map_access, but allocates a slot for the key if it is not present
// in the map. Returns pointer to value storage, which may contain an existing value to
// be replaced (caller should manage externally-stored data as appropriate.)
// Returns NULL on memory allocation failure.
void* nullable map_assign(const HMapType* t, HMap* h, void* key, Mem);

// map_delete removes the entry for key.
// Returns a pointer to the removed value if found, or NULL if not found.
// Caller should manage externally-stored data as appropriate (eg. free memory.)
void* nullable map_delete(const HMapType* t, HMap* nullable h, void* key, Mem);

// map_clear removes all entries from a map.
// After this call the map can be reused with the same HMapType directly
// (e.g. map_assign) or with a different HMapType by calling map_make, which will use
// existing resources of h.
// If the keys and/or values contain heap-allocated memory, the caller should free
// that heap memory before calling this function.
void map_clear(const HMapType* t, HMap* nullable h, Mem);

// map_free deallocates a map and all of its internal resources.
// h is invalid after this call (unless a non-null h was initially passed to map_make.)
// If the keys and/or values contain heap-allocated memory, the caller should free
// that heap memory before calling this function.
void map_free(const HMapType* t, HMap* h, Mem);

// map_bucketsize calculates the memory needed to store count entries, in bytes,
// excluding the HMap structure when count is provided up front to map_make,
// assuming no collisions. alloc_overhead is added to each logical allocation.
// Note that for map_make calls without a preallocated HMap, additional space is
// required for the HMap struct. In that case, add sizeof(HMap)+alloc_overhead
// to the result. Returns 0 if the result would overflow usize.
usize map_bucketsize(const HMapType* t, usize count, usize alloc_overhead);

// map_make_deterministic works like map_make but configures the map to have
// reproducible deterministic behavior, useful for testing. Disables attack mitigations.
HMap* nullable map_make_deterministic(
  const HMapType*, HMap* nullable h, Mem, usize hint, u32 hash_seed);

// map_set_deterministic enables or disables h to behave in a reproducible,
// deterministic behavior, useful for testing. Disables attack mitigations.
// Returns previous state.
bool map_set_deterministic(HMap* h, bool enabled);

// --------------------------------------------------------------------------------------
// inline implementations

inline static HMap* map_init_small(HMap* h) {
  h->hash0 = fastrand();
  return h;
}

ASSUME_NONNULL_END
