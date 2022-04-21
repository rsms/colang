// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
// Hash map implemented with open addressing and linear probing.
// This code has been written, tested and tuned for a balance between small (mc) code size,
// a clear and simple implementation and lastly performance. The entire implementation is
// about 300 x86_64 instructions (20 branches).
//
#include "colib.h"


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

// memory layout: [{ENT_TYPE, HMapMeta}, ...]
#define ENT_AT1(entries, entsize, index) ( entries + (entsize + sizeof(HMapMeta))*index )
#define ENT_AT(m, index)   ENT_AT1((m)->entries, (m)->entsize, index)

//———————————————————————————————————————————————————————————————————————————————————————
// HMap impl

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
    if (meta->flags == FL_USED) {
      if (meta->hash == hash && m->eqfn(ent, keyp))
        break; // replace equivalent entry
    } else {
      // use free slot or recycle deleted slot
      assert(meta->flags == FL_FREE || meta->flags == FL_DEL);
      m->len++;
      break;
    }
    if (++index == m->cap)
      index = 0;
  }
  meta->flags = FL_USED;
  meta->hash = hash;
  return ent;
}


void* nullable hmap_find(const HMap* m, const void* keyp, Hash hash) {
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
  void* ent = hmap_find(m, keyp, hash);
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


void* nullable hmap_itstart(HMap* m) {
  void* end = ENT_AT(m, m->cap);
  void* ent = m->entries;
  while (ent != end) {
    HMapMeta* meta = ent + m->entsize;
    if (meta->flags == FL_USED)
      return ent;
    ent += m->entsize + sizeof(HMapMeta);
  }
  return NULL;
}


bool hmap_itnext(HMap* m, void* ep) {
  uintptr end = (uintptr)ENT_AT(m, m->cap);
  void* ent = (*(void**)ep) + (m->entsize + sizeof(HMapMeta)); // next ent
  while ((uintptr)ent < end) {
    HMapMeta* meta = ent + m->entsize;
    // usize index = (uintptr)(ent - m->entries) / (m->entsize + sizeof(HMapMeta));
    if (meta->flags == FL_USED) {
      *((void**)ep) = ent;
      return true;
    }
    ent += m->entsize + sizeof(HMapMeta);
  }
  return false;
}



//———————————————————————————————————————————————————————————————————————————————————————
// SMap impl

static bool smap_eq(const void* restrict entryp, const void* restrict keyp) {
  const SMapEnt* ent = entryp;
  const StrSlice* key = keyp;
  return ent->key.len == key->len && memcmp(ent->key.p, key->p, key->len) == 0;
}

SMap* nullable smap_init(SMap* m, Mem mem, u32 hint, HMapLF lf) {
  return hmap_init(m, mem, hint, lf, sizeof(SMapEnt), &smap_eq);
}

uintptr* nullable smap_find(const SMap* m, StrSlice key) {
  SMapEnt* e = hmap_find(m, &key, hash_mem(key.p, key.len, m->hash0));
  return e ? &e->value : NULL;
}

uintptr* nullable smap_assign(SMap* m, StrSlice key) {
  SMapEnt* e = hmap_assign(m, &key, hash_mem(key.p, key.len, m->hash0));
  if (e == NULL)
    return NULL;
  e->key = key;
  return &e->value;
}

//———————————————————————————————————————————————————————————————————————————————————————
// PMap impl

static bool pmap_eq(const void* restrict entryp, const void* restrict keyp) {
  const void* k1 = ((const PMapEnt*)entryp)->key;
  const void* k2 = *(const void**)keyp;
  return k1 == k2;
}

PMap* nullable pmap_init(PMap* m, Mem mem, u32 hint, HMapLF lf) {
  return hmap_init(m, mem, hint, lf, sizeof(PMapEnt), &pmap_eq);
}

uintptr* nullable pmap_find(const PMap* m, const void* key) {
  assertnotnull(key);
  usize index = hash_ptr(&key, m->hash0) & (m->cap - 1);
  for (;;) {
    PMapEnt* ent = ENT_AT(m, index);
    HMapMeta* meta = ((void*)ent) + m->entsize;
    if (meta->flags == FL_FREE)
      return NULL; // end of possible entries of provided hash (= not found)
    if (meta->flags == FL_USED && ent->key == key)
      return &ent->value;
    if (++index == m->cap)
      index = 0;
  }
  return NULL;
}

uintptr* nullable pmap_assign(PMap* m, const void* key) {
  PMapEnt* e = hmap_assign(m, &key, hash_ptr(&key, m->hash0));
  if (e == NULL)
    return NULL;
  e->key = key;
  return &e->value;
}

//———————————————————————————————————————————————————————————————————————————————————————
// PSet impl

// make sure we can reuse pmap_eq
static_assert(offsetof(PMapEnt,key) == offsetof(PSetEnt,key), "");
static_assert(sizeof(((PMapEnt*)0)->key) == sizeof(((PSetEnt*)0)->key), "");

PSet* nullable pset_init(PSet* m, Mem mem, u32 hint, HMapLF lf) {
  return hmap_init(m, mem, hint, lf, sizeof(PSetEnt), &pmap_eq);
}

bool pset_has(const HMap* m, const void* key) {
  assertnotnull(key);
  usize index = hash_ptr(&key, m->hash0) & (m->cap - 1);
  for (;;) {
    PSetEnt* ent = ENT_AT(m, index);
    HMapMeta* meta = ((void*)ent) + m->entsize;
    if (meta->flags == FL_FREE)
      return false; // end of possible entries of provided hash (= not found)
    if (meta->flags == FL_USED && ent->key == key)
      return true;
    if (++index == m->cap)
      index = 0;
  }
  return false;
}

bool pset_add(HMap* m, const void* key) {
  if (UNLIKELY(m->len >= m->gcap) && !hmap_grow(m))
    return false;
  Hash hash = hash_ptr(&key, m->hash0);
  usize index = hash & (m->cap - 1);
  for (;;) {
    PSetEnt* ent = ENT_AT(m, index);
    HMapMeta* meta = ((void*)ent) + (m)->entsize;
    if (meta->flags == FL_USED) {
      if (ent->key == key) // already in set
        return false;
    } else {
      m->len++;
      ent->key = key;
      meta->flags = FL_USED;
      meta->hash = hash;
      return true; // added
    }
    if (++index == m->cap)
      index = 0;
  }
}
