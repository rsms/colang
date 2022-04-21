// hash map
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef struct HMap HMap;
typedef u8 HMapLF; // load factor

// SMap {string => uintptr}
typedef HMap SMap;
typedef struct SMapEnt { StrSlice key; uintptr value; } SMapEnt;
SMap* nullable smap_init(SMap* m, Mem, u32 hint, HMapLF);
uintptr* nullable smap_assign(SMap* m, StrSlice key);
uintptr* nullable smap_find(const SMap* m, StrSlice key);
static bool smap_del(SMap* m, StrSlice key);

// PMap {void* => uintptr}
typedef HMap PMap;
typedef struct PMapEnt { const void* key; uintptr value; } PMapEnt;
PMap* nullable pmap_init(PMap* m, Mem, u32 hint, HMapLF);
uintptr* nullable pmap_assign(PMap* m, const void* key);
uintptr* nullable pmap_find(const PMap* m, const void* key);
static bool pmap_del(PMap* m, const void* key);

// PSet {void*}
typedef HMap PSet;
typedef struct PSetEnt { const void* key; } PSetEnt;
PSet* nullable pset_init(PSet* m, Mem, u32 hint, HMapLF);
static void pset_free(PSet*);
bool pset_add(PSet* m, const void* key); // true if added
bool pset_has(const PSet* m, const void* key);
static bool pset_del(PSet* m, const void* key); // true if found & removed

//————— low-level hmap interface —————

typedef bool(*HMapEqFn)(const void* restrict entryp, const void* restrict keyp);

// hmap_init initializes a new map.
// hint provides a hint as to how many entries will initially be stored.
// A hint of 0 causes hmap_init to use a small initial default value.
// Returns m on success, NULL on memory-allocation failure or overflow (from large hint.)
HMap* nullable hmap_init(
  HMap* m, Mem mem, u32 hint, HMapLF lf, usize entsize, HMapEqFn eqfn);

// hmap_dispose frees m->entries. Invalidates m (you can use hmap_init with m to reuse it.)
void hmap_dispose(HMap* m);

// hmap_clear empties m; removes all entries
void hmap_clear(HMap* m);

// hmap_find retrieves the entry for keyp & hash. Returns NULL if not found.
void* nullable hmap_find(const HMap* m, const void* keyp, Hash hash);

// hmap_assign adds or updates the map.
// Returns the entry for keyp & hash, which might be an existing entry with equivalent key.
// Returns NULL if memory allocation failed during growth.
void* nullable hmap_assign(HMap* m, const void* keyp, Hash hash);

// hmap_del removes an entry. Returs true if an entry was found & deleted.
bool hmap_del(HMap* m, const void* keyp, Hash hash);

// hmap_itstart and hmap_itnext iterates over a map.
// You can change the value of an entry during iteration but must not change the key.
// Any mutation to the map during iteration will invalidate the iterator.
// Example use:
//   for (const MyEnt* e = hmap_itstart(m); hmap_itnext(m, &e); )
//     log("%.*s => %lx", e->key, e->value);
//
void* nullable hmap_itstart(HMap* m);
bool hmap_itnext(HMap* m, void* ep);
static const void* nullable hmap_citstart(const HMap* m);
static bool hmap_citnext(const HMap* m, const void* ep);

// hmap_isvalid returns true if m has been initialized
static bool hmap_isvalid(const HMap* m);

enum HMapLF {
  MAPLF_1 = 1, // grow when 50% full; recommended for maps w/ balanced hit & miss lookups
  MAPLF_2 = 2, // grow when 75% full; recommended for maps of mostly hit lookups
  MAPLF_3 = 3, // grow when 88% full; miss (no match) lookups are expensive
  MAPLF_4 = 4, // grow when 94% full; miss lookups are very expensive
} END_ENUM(HMapLF)

//———————————————————————————————————————————————————————————————————————————————————————
// internal interface

struct HMap {
  u32      cap;     // capacity of entries
  u32      len;     // number of items currently stored in the map (count)
  u32      gcap;    // growth watermark cap
  u32      entsize; // size of an entry, in bytes
  Hash     hash0;   // hash seed
  HMapLF   lf;      // growth watermark load factor (shift value; 1|2|3|4)
  Mem      mem;
  HMapEqFn eqfn;
  void*    entries; // [{ENT_TYPE, HMapMeta}, ...]  len=entsize*cap
};

inline static const void* nullable hmap_citstart(const HMap* m) {
  return hmap_itstart((HMap*)m);
}
inline static bool hmap_citnext(const HMap* m, const void* ep) {
  return hmap_itnext((HMap*)m, (void*)ep);
}
static bool hmap_isvalid(const HMap* m) {
  return m->entries != NULL;
}

inline static bool smap_del(HMap* m, StrSlice key) {
  return hmap_del(m, &key, hash_mem(key.p, key.len, m->hash0));
}
inline static bool pmap_del(HMap* m, const void* key) {
  return hmap_del(m, &key, hash_ptr(&key, m->hash0));
}

inline static bool pset_del(PSet* m, const void* key) {
  return hmap_del(m, &key, hash_ptr(&key, m->hash0));
}
inline static void pset_free(PSet* m) {
  hmap_dispose(m);
}


END_INTERFACE
