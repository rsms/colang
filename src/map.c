// hash map
// SPDX-License-Identifier: Apache-2.0
//
// Implemented with open addressing and linear probing.
// This code has been written, tested and tuned for a balance between small (mc) code size,
// a clear and simple implementation and lastly performance. The entire implementation is
// about 300 x86_64 instructions (20 branches).
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define MAP_IMPLEMENTATION
#endif
#include "mem.c"
#include "hash.h"
#include "test.h"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————
typedef struct HMap HMap;
typedef u8 HMapLF; // load factor

// smap -- string => uintptr
typedef struct SMapEnt { SSlice key; uintptr value; } SMapEnt;
HMap* nullable smap_init(HMap* m, Mem, u32 hint, HMapLF);
uintptr* nullable smap_assign(HMap* m, SSlice key);
uintptr* nullable smap_lookup(const HMap* m, SSlice key);
static bool smap_del(HMap* m, SSlice key);

// pmap -- void* => uintptr
typedef struct PMapEnt { SSlice key; uintptr value; } PMapEnt;
HMap* nullable pmap_init(HMap* m, Mem, u32 hint, HMapLF);
uintptr* nullable pmap_assign(HMap* m, void* key);
uintptr* nullable pmap_lookup(const HMap* m, void* key);
static bool pmap_del(HMap* m, void* key);

//————— low-level hmap interface —————

typedef bool(*HMapEqFn)(const void* entryp, const void* keyp);

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

// hmap_lookup retrieves the entry for keyp & hash. Returns NULL if not found.
void* nullable hmap_lookup(const HMap* m, const void* keyp, Hash hash);

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
static void* nullable hmap_itstart(HMap* m);
bool hmap_itnext(HMap* m, void* ep);
static const void* nullable hmap_citstart(const HMap* m);
static bool hmap_citnext(const HMap* m, const void* ep);

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
  u32      hash0;   // hash seed
  u32      entsize; // size of an entry, in bytes
  HMapLF   lf;      // growth watermark load factor (shift value; 1|2|3|4)
  Mem      mem;
  HMapEqFn eqfn;
  void*    entries; // [{ENT_TYPE, HMapMeta}, ...]  len=entsize*cap
};

inline static void* nullable hmap_itstart(HMap* m) { return m->entries; }
inline static const void* nullable hmap_citstart(const HMap* m) { return m->entries; }
inline static bool hmap_citnext(const HMap* m, const void* ep) {
  return hmap_itnext((HMap*)m, (void*)ep);
}

inline static bool smap_del(HMap* m, SSlice key) {
  return hmap_del(m, &key, hash_mem(key.p, key.len, (Hash)m->hash0));
}
// inline static bool pmap_del(HMap* m, SSlice key) {
//   return hmap_del(m, &key, hash_ptr(key.p, key.len, (Hash)m->hash0));
// }

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef MAP_IMPLEMENTATION
//———————————————————————————————————————————————————————————————————————————————————————
// smap impl

static bool smap_streq(const void* entryp, const void* keyp) {
  const SMapEnt* ent = entryp;
  const SSlice* key = keyp;
  return ent->key.len == key->len && memcmp(ent->key.p, key->p, key->len) == 0;
}

HMap* nullable smap_init(HMap* m, Mem mem, u32 hint, HMapLF lf) {
  return hmap_init(m, mem, hint, lf, sizeof(SMapEnt), &smap_streq);
}

uintptr* nullable smap_lookup(const HMap* m, SSlice key) {
  SMapEnt* e = hmap_lookup(m, &key, hash_mem(key.p, key.len, (Hash)m->hash0));
  return e ? &e->value : NULL;
}

uintptr* nullable smap_assign(HMap* m, SSlice key) {
  SMapEnt* e = hmap_assign(m, &key, hash_mem(key.p, key.len, (Hash)m->hash0));
  if (e == NULL)
    return NULL;
  e->key = key;
  return &e->value;
}

//———————————————————————————————————————————————————————————————————————————————————————
// hmap impl

typedef struct HMapMeta {
  Hash hash;
  u8   flags;
} HMapMeta;
static_assert(alignof(HMapMeta) == sizeof(void*), "");
static_assert(sizeof(HMapMeta) == sizeof(void*)*2, "");

// HMapMeta.flags
#define FL_FREE 0
#define FL_DEL  1
#define FL_USED 2


static u32 perfectcap(u32 len, HMapLF lf) {
  // captab maps MAPLF_1...4 to multipliers for cap -> gcap
  const double captab[] = { 0.5, 0.25, 0.125, 0.0625, 0.0 };
  assert(lf > 0 && lf <= countof(captab));
  len++; // must always have one free slot
  return CEIL_POW2(len + (u32)( (double)len*captab[lf-1] + 0.5 ));
}


static usize entries_nbytes(usize cap, usize entsize) {
  usize nbytes;
  if (check_mul_overflow(cap, entsize + sizeof(HMapMeta), &nbytes))
    return USIZE_MAX;
  if (nbytes > USIZE_MAX - sizeof(void*))
    return USIZE_MAX;
  return ALIGN2(nbytes, sizeof(void*));
}


HMap* nullable hmap_init(
  HMap* m, Mem mem, u32 hint, HMapLF lf, usize entsize, HMapEqFn eqfn)
{
  assert(entsize <= U32_MAX);
  m->cap = hint == 0 ? 32 : perfectcap(hint, lf);
  m->len = 0;
  // lf is a bit shift magnitude that does fast integer division
  // i.e. cap-(cap>>lf) == (u32)((double)cap*0.75)
  m->gcap = m->cap - (m->cap >> lf); assert(m->gcap > 0);
  m->lf = lf;
  m->hash0 = fastrand();
  m->entsize = entsize;
  m->mem = mem;
  m->eqfn = eqfn;

  usize nbytes = entries_nbytes((usize)m->cap, entsize);
  if (nbytes == USIZE_MAX)
    return NULL;
  m->entries = mem_allocz(mem, nbytes);
  if UNLIKELY(m->entries == NULL)
    return NULL;
  return m;
}


void hmap_dispose(HMap* m) {
  usize nbytes = ALIGN2((usize)m->cap * (m->entsize + sizeof(HMapMeta)), sizeof(void*));
  mem_free(m->mem, m->entries, nbytes);
  #ifdef DEBUG
  memset(m, 0, sizeof(*m));
  #endif
}


void hmap_clear(HMap* m) {
  m->len = 0;
  memset(m->entries, 0, m->cap * (m->entsize + sizeof(HMapMeta)));
}


// memory layout: [{ENT_TYPE, HMapMeta}, ...]
#define ENT_AT1(entries, entsize, index) ( entries + (entsize + sizeof(HMapMeta))*index )
#define ENT_AT(m, index)   ENT_AT1((m)->entries, (m)->entsize, index)


static void hmap_migrate_ent(HMap* m, void* dstentries, u32 dstcap, void* srcent, Hash hash) {
  usize index = hash & (dstcap - 1);
  void* dstent;
  HMapMeta* dstmeta;
  for (;;) {
    dstent = ENT_AT1(dstentries, m->entsize, index);
    dstmeta = dstent + m->entsize;
    if (dstmeta->flags == FL_FREE)
      break;
    if (++index == dstcap)
      index = 0;
  }
  memcpy(dstent, srcent, m->entsize);
  dstmeta->flags = FL_USED;
  dstmeta->hash = hash;
}


static bool hmap_grow(HMap* m) {
  u32 newcap; // = cap * 2
  if (check_mul_overflow(m->cap, (u32)2u, &newcap))
    return false;

  usize nbytes = entries_nbytes((usize)newcap, m->entsize);
  if (nbytes == USIZE_MAX)
    return false;

  // dlog("grow len=%u cap=%u => cap=%u (nbytes=%zu)", m->len, m->cap, newcap, nbytes);
  void* new_entries = mem_allocz(m->mem, nbytes);
  if (new_entries == NULL)
    return false;

  // relocate entries into their new slots
  for (u32 index = 0; index < m->cap; index++) {
    void* ent = ENT_AT(m, index);
    HMapMeta* meta = ent + m->entsize;
    if (meta->flags == FL_USED)
      hmap_migrate_ent(m, new_entries, newcap, ent, meta->hash);
  }

  mem_free(m->mem, m->entries, m->cap * sizeof(SMapEnt));
  m->entries = new_entries;
  m->cap = newcap;
  m->gcap = newcap - (newcap >> m->lf);
  return true;
}


void* nullable hmap_assign(HMap* m, const void* keyp, Hash hash) {
  if (UNLIKELY(m->len >= m->gcap) && !hmap_grow(m))
    return NULL;
  usize index = hash & (m->cap - 1);
  void* ent;
  HMapMeta* meta;
  for (;;) {
    ent = ENT_AT(m, index);
    meta = ent + (m)->entsize;
    if (meta->flags != FL_USED) {
      // use this slot (including recycling deleted slot)
      m->len++;
      break;
    }
    if (meta->hash == hash && m->eqfn(ent, keyp))
      break;
    if (++index == m->cap)
      index = 0;
  }
  meta->flags = FL_USED;
  meta->hash = hash;
  return ent;
}


void* nullable hmap_lookup(const HMap* m, const void* keyp, Hash hash) {
  usize index = hash & (m->cap - 1);
  for (;;) {
    void* ent = ENT_AT(m, index);
    HMapMeta* meta = ent + m->entsize;
    if (meta->flags == FL_FREE)
      return NULL; // end of possible entries of provided hash (= not found)
    if (meta->flags == FL_USED && meta->hash == hash && m->eqfn(ent, keyp))
      return ent;
    if (++index == m->cap)
      index = 0;
  }
  return NULL;
}


bool hmap_del(HMap* m, const void* keyp, Hash hash) {
  void* ent = hmap_lookup(m, keyp, hash);
  if UNLIKELY(ent == NULL)
    return false;
  // clear map when last item is removed (clear all FL_DEL entries)
  if (m->len == 1) {
    hmap_clear(m); // clear all FL_DEL entries
    return true;
  }
  // mark as deleted
  HMapMeta* meta = ent + m->entsize;
  m->len--;
  meta->flags = FL_DEL;
  return true;
}


bool hmap_itnext(HMap* m, void* ep) {
  void* end = ENT_AT(m, m->cap);
  void* ent = (*(void**)ep) + m->entsize + sizeof(HMapMeta); // next ent
  for (; ent != end; ent += m->entsize + sizeof(HMapMeta)) {
    HMapMeta* meta = ent + m->entsize;
    if (meta->flags == FL_USED) {
      *((void**)ep) = ent;
      return true;
    }
  }
  return false;
}


//———————————————————————————————————————————————————————————————————————————————————————
#endif
