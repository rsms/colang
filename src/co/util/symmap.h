#pragma once

// SymMap is a hash map that maps Sym => pointer
#define HASHMAP_NAME SymMap
#include "hashmap.h"
#undef HASHMAP_NAME

ASSUME_NONNULL_BEGIN

// Creates and initializes a new SymMap in mem, or global memory if mem is NULL.
SymMap* SymMapNew(u32 initbuckets, Mem nullable mem);

// SymMapInit initializes a map structure. initbuckets is the number of initial buckets.
void SymMapInit(SymMap*, u32 initbuckets, Mem nullable mem);

// SymMapFree frees SymMap along with its data.
void SymMapFree(SymMap*);

// SymMapDispose frees heap memory used by a map, but not free the SymMap struct itself.
void SymMapDispose(SymMap*);

// SymMapGet searches for key. Returns value, or NULL if not found.
void* nullable SymMapGet(const SymMap*, Sym key);

// SymMapSet inserts key=value into m. Returns the replaced value or NULL if not found.
void* nullable SymMapSet(SymMap*, Sym key, void* value);

// SymMapDel removes value for key. Returns the removed value or NULL if not found.
void* nullable SymMapDel(SymMap*, Sym key);

// SymMapClear removes all entries. In contrast to SymMapFree, map remains valid.
void SymMapClear(SymMap*);

// Iterator function type. Set stop=true to stop iteration.
typedef void(SymMapIterator)(Sym key, void* value, bool* stop, void* nullable userdata);

// SymMapIter iterates over entries of the map.
void SymMapIter(const SymMap*, SymMapIterator*, void* nullable userdata);

ASSUME_NONNULL_END
