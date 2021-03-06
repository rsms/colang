#pragma once
//
// PtrMap maps (const void*) => (void*)
//
#define HASHMAP_NAME PtrMap
#include "hashmap.h"
#undef HASHMAP_NAME

ASSUME_NONNULL_BEGIN

// PtrMapInit initializes a map structure. initbuckets is the number of initial buckets.
void PtrMapInit(PtrMap*, u32 initbuckets, Mem mem);

static bool PtrMapIsInit(const PtrMap*);

// PtrMapDispose frees heap memory used by a map, but leaves PtrMap untouched.
void PtrMapDispose(PtrMap*);

// Creates and initializes a new PtrMap in mem, or global memory if mem is NULL.
PtrMap* PtrMapNew(u32 initbuckets, Mem mem);

// PtrMapFree frees PtrMap along with its data.
void PtrMapFree(PtrMap*);

// PtrMapLen returns the number of entries currently in the map
static u32 PtrMapLen(const PtrMap*);

// PtrMapGet searches for key. Returns value, or NULL if not found.
void* nullable PtrMapGet(const PtrMap*, const void* key);

// PtrMapSet inserts key=value into m. Returns the replaced value or NULL if not found.
void* nullable PtrMapSet(PtrMap*, const void* key, void* value);

// PtrMapDel removes value for key. Returns the removed value or NULL if not found.
void* nullable PtrMapDel(PtrMap*, const void* key);

// PtrMapClear removes all entries. In contrast to PtrMapFree, map remains valid.
void PtrMapClear(PtrMap*);

// Iterator function type. Set stop=true to stop iteration.
typedef void(PtrMapIterator)(const void* key, void* value, bool* stop, void* userdata);

// PtrMapIter iterates over entries of the map.
void PtrMapIter(const PtrMap*, PtrMapIterator*, void* userdata);


inline static bool PtrMapIsInit(const PtrMap* m) { return m->buckets != NULL; }

ASSUME_NONNULL_END
