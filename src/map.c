/* This file implements a hash table with support for arbitrary key and value types.

-----------------------------------------------------------------------------------------
The implementation is based on the Go map; this source file is licensed as follows:

Copyright (c) 2009 The Go Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
     copyright notice, this list of conditions and the following disclaimer
     in the documentation and/or other materials provided with the
     distribution.
   * Neither the name of Google Inc. nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-----------------------------------------------------------------------------------------

A map is just a hash table. The data is arranged into an array of buckets. Each bucket
contains up to 8 key/elem pairs. The low-order bits of the hash are used to select a
bucket. Each bucket contains a few high-order bits of each hash to distinguish the
entries within a single bucket.

If more than 8 keys hash to a bucket, we chain on extra buckets.

When the hashtable grows, we allocate a new array of buckets twice as big. Buckets are
incrementally copied from the old bucket array to the new bucket array.

Map iterators walk through the array of buckets and return the keys in walk order
(bucket #, then overflow chain order, then bucket index).  To maintain iteration
semantics, we never move keys within their bucket (if we did, keys might be returned 0 or
2 times).  When growing the table, iterators remain iterating through the old table and
must check the new table if the bucket they are iterating through has been moved
("evacuated") to the new table.

-----------------------------------------------------------------------------------------

Picking loadFactor: too large and we have lots of overflow buckets, too small and we
waste a lot of space. I wrote a simple program to check some stats for different loads:

(64-bit, 8 byte keys and elems)
 loadFactor    %overflow  bytes/entry     hitprobe    missprobe
       4.00         2.13        20.77         3.00         4.00
       4.50         4.05        17.30         3.25         4.50
       5.00         6.85        14.77         3.50         5.00
       5.50        10.55        12.94         3.75         5.50
       6.00        15.27        11.67         4.00         6.00
       6.50        20.90        10.79         4.25         6.50
       7.00        27.14        10.15         4.50         7.00
       7.50        34.03         9.73         4.75         7.50
       8.00        41.10         9.40         5.00         8.00

%overflow   = percentage of buckets which have an overflow bucket
bytes/entry = overhead bytes used per key/elem pair
hitprobe    = # of entries to check when looking up a present key
missprobe   = # of entries to check when looking up an absent key

Keep in mind this data is for maximally loaded tables, i.e. just before the table grows.
Typical tables will be somewhat less loaded.
*/
// #define CO_MEM_DEBUG_ALLOCATIONS
// #define CO_MEM_DEBUG_PANIC_ON_FAIL
#include "coimpl.h"
#include "map.h"
#include "test.h"
#include "array.h"

#ifdef CO_WITH_LIBC
  #include <stdlib.h>
#endif

// Enable using keys and value types larger than maxKeySize and maxElemSize by
// storing them in separat memalloc-ed memory.
// THIS IS WIP -- does not currently free the memory.
//#define ENABLE_LARGE_ENTRIES

#define PTRSIZE sizeof(void*)

// Maximum number of key/elem pairs a bucket can hold.
#define bucketCntBits 3
#define bucketCnt     (1ul << bucketCntBits)

// Maximum average load of a bucket that triggers growth is 6.5.
// Represent as loadFactorNum/loadFactorDen, to allow integer math.
#define loadFactorNum 13
#define loadFactorDen 2

// Maximum key or elem size to keep inline (instead of memallocing per element.)
// Must fit in a u8.
#define maxKeySize  128
#define maxElemSize 128

// dataOffset should be the size of the bmap struct, but needs to be aligned correctly.
// For amd64p32 this means 64-bit alignment even though pointers are 32 bit.
#define dataOffset offsetof(struct{ bmap b; i64 v; },v)

// sentinel bucket ID for iterator checks
#define IT_NO_CHECK ((uintptr)1ul << ((8 * PTRSIZE) - 1))

typedef struct rtype     rtype;     // type descriptor
typedef struct bmap      bmap;      // bucket
typedef struct HMapExtra HMapExtra; // fields that are not present on all maps

// keyhasher is a function for hashing keys (ptr to key, seed) -> hash
typedef uintptr(*keyhasher)(const void* keyp, uintptr seed);

// equalfun is used by rtype
typedef bool(*equalfun)(const void*, const void*);


// Possible tophash values. We reserve a few possibilities for special marks.
// Each bucket (including its overflow buckets, if any) will have either all or none
// of its entries in the evacuated* states (except during the evacuate() function,
// which only happens during map writes and thus no one else can observe the map during
// that time).
enum tophash_flag {
  emptyRest,
    // this cell is empty, and there are no more non-empty cells at higher indexes
    // or overflows
  emptyOne,
    // this cell is empty
  evacuatedX,
    // key/elem is valid. Entry has been evacuated to first half of larger tbl
  evacuatedY,
    // same as above, but evacuated to second half of larger table
  evacuatedEmpty,
    // cell is empty, bucket is evacuated
  minTopHash,
    // minimum tophash for a normal filled cell
};

typedef u8 hflag;
// since HMap.flags is exposed in the api, but the enum isn't, save ourselves
// some future headache by asserting that the types match.
static_assert(__builtin_types_compatible_p(hflag, __typeof__(((HMap*)0)->flags)),
              "type of HMap.flags doesn't match hflag type");
enum hflag {
  H_iterator      = 1 << 0, // there may be an iterator using buckets
  H_oldIterator   = 1 << 1, // there may be an iterator using oldbuckets
  H_hashWriting   = 1 << 2, // a thread is writing to the map
  H_sameSizeGrow  = 1 << 3, // the current map growth is to a new map of the same size
  H_hmemManaged   = 1 << 4, // memory for HMap should be freed by mapfree
  H_deterministic = 1 << 5, // deterministic behavior (no attack mitigation)
} END_TYPED_ENUM(hflag)

typedef u8 tkind;
enum tkind {
  tkind_invalid,
  tkind_ptr,
  tkind_sint,
  tkind_uint,
  tkind_float,
  tkind_struct,
} END_TYPED_ENUM(tkind);

typedef u8 HMapTypeFlag;
enum HMapTypeFlag {
  maptypeReflexiveKey  = 1 << 1, // k==k for all keys
  maptypeNeedKeyUpdate = 1 << 2, // need to update key on an overwrite
#ifdef ENABLE_LARGE_ENTRIES
  maptypeIndirectKey   = 1 << 3, // store ptr to key instead of key itself
  maptypeIndirectElem  = 1 << 4, // store ptr to elem instead of elem itself
#endif
} END_TYPED_ENUM(HMapTypeFlag);

struct bmap {
  // tophash generally contains the top byte of the hash value
  // for each key in this bucket. If tophash[0] < minTopHash,
  // tophash[0] is a bucket evacuation state instead.
  u8 tophash[bucketCnt];
  // Followed by bucketCnt keys
  // and then bucketCnt elems.
  //   NOTE: packing all the keys together and then all the elems together makes the
  //   code a bit more complicated than alternating key/elem/key/elem/... but it allows
  //   us to eliminate padding which would be needed for, e.g., map[i64]i8.
  // Followed by an overflow pointer.
};

struct rtype { // aka _type
  usize    size;
  u8       align;
  tkind    kind;
  equalfun equal; // comparing objects of this type
};

// HMapExtra holds fields that are not present on all maps.
struct HMapExtra {
  // If both key and elem do not contain pointers and are inline, then we mark bucket
  // type as containing no pointers. This avoids scanning such maps.
  // However, bmap.overflow is a pointer. In order to keep overflow buckets
  // alive, we store pointers to all overflow buckets in HMap.extra.overflow and
  // HMap.extra.oldoverflow.
  // overflow and oldoverflow are only used if key and elem do not contain pointers.
  // overflow contains overflow buckets for HMap.buckets.
  // oldoverflow contains overflow buckets for HMap.oldbuckets.
  // The indirection allows to store a pointer to the slice in hiter.
  PtrArray overflow;    // *[]*bmap
  PtrArray oldoverflow; // *[]*bmap

  // nextOverflow holds a pointer to a free overflow bucket.
  bmap* nextOverflow;
  void* overflow_storage[];
};

struct HMapType {
  rtype     typ;
  rtype     key;
  rtype     elem;
  rtype     bucket; // internal type representing a hash bucket
  keyhasher hasher;

  u8  keysize;    // size of key slot
  u8  elemsize;   // size of elem slot

  HMapTypeFlag flags;
};

#ifdef ENABLE_LARGE_ENTRIES
  // store ptr to key instead of key itself
  inline static bool maptype_indirectkey(const HMapType* mt) {
    return mt->flags&maptypeIndirectKey;}
  // store ptr to elem instead of elem itself
  inline static bool maptype_indirectelem(const HMapType* mt) {
    return mt->flags&maptypeIndirectElem;}
#else
  inline static bool maptype_indirectkey(const HMapType* mt) { return false; }
  inline static bool maptype_indirectelem(const HMapType* mt) { return false; }
#endif // ENABLE_LARGE_ENTRIES

// true if k==k for all keys
static bool maptype_reflexivekey(const HMapType* mt) {
  return mt->flags&maptypeReflexiveKey;}
// true if we need to update key on an overwrite
static bool maptype_needkeyupdate(const HMapType* mt) {
  return mt->flags&maptypeNeedKeyUpdate;}


#ifdef CO_TESTING_ENABLED
  // rtype_isReflexive reports whether the == operation on the type is reflexive.
  // That is, x == x for all values x of type t.
  static bool rtype_isReflexive(const rtype* t) {
    switch ((enum tkind)t->kind) {
      case tkind_ptr:
      case tkind_sint:
      case tkind_uint:
        return true;
      case tkind_float:
        return false;
      case tkind_struct:
        // really, the answer is MAX([rtype_isReflexive(f) for f in t->fields])
        return false;
      case tkind_invalid:
        break;
    }
    panic("non-key type %p", t);
  }

  // rtype_needKeyUpdate reports whether map overwrites require the key to be copied
  static bool rtype_needKeyUpdate(const rtype* t) {
    switch ((enum tkind)t->kind) {
      case tkind_ptr:
      case tkind_sint:
      case tkind_uint:
        return false;
      case tkind_float:
        // Float keys can be updated from +0 to -0.
        // String keys can be updated to use a smaller backing store.
        // Interfaces might have floats of strings in them.
        return true;
      case tkind_struct:
        // really, the answer is MAX([rtype_needKeyUpdate(f) for f in t->fields])
        return true;
      case tkind_invalid:
        break;
    }
    panic("non-key type %p", t);
  }

  static keyhasher rtype_hasher(const rtype* t) {
    switch ((enum tkind)t->kind) {
      case tkind_sint:
      case tkind_uint:
        if (t->size == 4)
          return &hash_4;
        if (t->size == 8)
          return &hash_4;
        break;
      case tkind_ptr:
          return &hash_ptr;
      case tkind_float:
      case tkind_struct:
      case tkind_invalid:
        break;
    }
    panic("no hasher for rtype %d", t->kind);
  }
#endif // CO_TESTING_ENABLED

// isEmpty reports whether the given tophash array entry
// represents an empty bucket entry.
inline static bool isEmpty(u8 x) {
  return x <= emptyOne; }

// bucketShift returns 1<<b, optimized for code generation.
inline static usize bucketShift(u8 b) {
  // Masking the shift amount allows overflow checks to be elided.
  return (usize)1 << (b & (PTRSIZE*8 - 1));
}

// bucketMask returns 1<<b - 1
inline static usize bucketMask(u8 b) {
  return bucketShift(b) - 1; }

// tophash calculates the tophash value for hash.
inline static u8 tophash(uintptr hash) {
  u8 top = (u8)(hash >> (PTRSIZE*8 - 8));
  if (top < minTopHash)
    top += minTopHash;
  return top;
}

inline static bool is_evacuated(bmap* b) {
  u8 h = b->tophash[0];
  return h > emptyOne && h < minTopHash;
}


// is_over_loadFactor reports whether count items placed in 1<<B buckets is over
// loadFactor
inline static bool is_over_loadFactor(usize count, u8 B) {
  return count > bucketCnt && count > loadFactorNum * (bucketShift(B) / loadFactorDen);
}

// is_tooManyOverflowBuckets reports whether noverflow buckets is too many for a map
// with 1<<B buckets. Note that most of these overflow buckets must be in sparse use;
// if use was dense, then we'd have already triggered regular map growth.
inline static bool is_tooManyOverflowBuckets(u16 noverflow, u8 B) {
  // If the threshold is too low, we do extraneous work.
  // If the threshold is too high, maps that grow and shrink can hold on to lots of
  // unused memory. "too many" means (approximately) as many overflow buckets as regular
  // buckets. See incrnoverflow for more details.
  if (B > 15)
    B = 15;
  // The compiler doesn't see here that B < 16; mask B to generate shorter shift code.
  return noverflow >= (u16)1 << (B & 15);
}


inline static bmap* nullable bmap_overflow(bmap* b, const HMapType* t) {
  return *(bmap**)((uintptr)b + (uintptr)t->bucket.size - PTRSIZE);
}

inline static void bmap_setoverflow(bmap* b, const HMapType* t, bmap* ovf) {
  *(bmap**)((uintptr)b + (uintptr)t->bucket.size - PTRSIZE) = ovf;
}

// inline static void* bmap_keys(bmap* b) {
//   return (void*)b + dataOffset;
// }


// hmap_isgrowing reports whether h is growing.
// The growth may be to the same size or bigger.
inline static bool hmap_isgrowing(HMap* h) {
  return h->oldbuckets != NULL;
}

// hmap_sameSizeGrow reports whether the current growth is to a map of the same size
inline static bool hmap_sameSizeGrow(const HMap* h) {
  return (h->flags & H_sameSizeGrow) != 0;
}

// hmap_oldbucketcount calculates the number of buckets prior to the current map growth
// aka noldbuckets
static usize hmap_oldbucketcount(HMap* h) {
  u8 B = h->B;
  if (!hmap_sameSizeGrow(h))
    B--;
  return bucketShift(B);
}

// hmap_oldbucketmask provides a mask that can be applied to calculate n % noldbuckets()
inline static usize hmap_oldbucketmask(HMap* h) {
  return hmap_oldbucketcount(h) - 1;
}

// hmap_incrnoverflow increments h.noverflow.
// noverflow counts the number of overflow buckets.
// This is used to trigger same-size map growth.
// See also tooManyOverflowBuckets.
// To keep hmap small, noverflow is a uint16.
// When there are few buckets, noverflow is an exact count.
// When there are many buckets, noverflow is an approximate count.
static void hmap_incrnoverflow(HMap* h) {
  // We trigger same-size map growth if there are
  // as many overflow buckets as buckets.
  // We need to be able to count to 1<<h.B.
  if (h->B < 16) {
    h->noverflow++;
    return;
  }
  // Increment with probability 1/(1<<(h->B-15)).
  // When we reach 1<<15 - 1, we will have approximately
  // as many overflow buckets as buckets.
  u32 mask = ((u32)1 << (h->B - 15)) - 1; // [LOOK]
  // Example: if h->B == 18, then mask == 7,
  // and fastrand & 7 == 0 with probability 1/8.
  if ((fastrand() & mask) == 0 && (h->flags & H_deterministic) == 0)
    h->noverflow++;
}

static bool hmap_createoverflow(HMap* h, u32 lenhint, Mem mem) {
  if (!h->extra) {
    h->extra = memallocztv(mem, HMapExtra, overflow_storage, lenhint);
    if UNLIKELY( !h->extra )
      return false;
    if (lenhint > 0)
      PtrArrayInitStorage(&h->extra->overflow, h->extra->overflow_storage, lenhint);
  }
  return true;
}

static bmap* hmap_newoverflow(HMap* h, const HMapType* t, bmap* b, Mem mem) {
  bmap* ovf;
  if (h->extra != NULL && h->extra->nextOverflow != NULL) {
    // We have preallocated overflow buckets available.
    // See make_bucket_array for more details.
    ovf = h->extra->nextOverflow;
    if (bmap_overflow(ovf, t) == NULL) {
      // We're not at the end of the preallocated overflow buckets. Bump the pointer.
      h->extra->nextOverflow = (bmap*)((void*)ovf + t->bucket.size);
    } else {
      // This is the last preallocated overflow bucket.
      // Reset the overflow pointer on this bucket,
      // which was set to a non-nil sentinel value.
      bmap_setoverflow(ovf, t, NULL);
      h->extra->nextOverflow = NULL;
    }
  } else {
    if (!(ovf = memallocz(mem, t->bucket.size)))
      return NULL;
  }
  hmap_incrnoverflow(h);
  if UNLIKELY( !hmap_createoverflow(h, 1, mem) ) {
    if (h->extra != NULL && ovf != h->extra->nextOverflow)
      memfree(mem, ovf);
    return NULL;
  }
  PtrArrayPush(&h->extra->overflow, ovf, mem);
  bmap_setoverflow(b, t, ovf);
  return ovf;
}

// make_bucket_array initializes a backing array for map buckets.
// 1<<B is the minimum number of buckets to allocate.
// dirtyalloc should either be null or a bucket array previously allocated by
// make_bucket_array with the same t and B parameters.
// If dirtyalloc is null a new backing array will be alloced and otherwise dirtyalloc
// will be cleared and reused as backing array.
static bmap* nullable make_bucket_array(
  Mem mem, const HMapType* t, u8 B, void* dirtyalloc, bmap** nextOverflow)
{
  usize base = bucketShift(B);
  usize nbuckets = base;

  // For small B, overflow buckets are unlikely.
  if (B >= 4) {
    // Add on the estimated number of overflow buckets required to insert the median
    // number of elements used with this value of B.
    nbuckets += bucketShift(B - 4);
    usize sz = t->bucket.size * nbuckets;
    usize up = ALIGN2(sz, PTRSIZE);
    if (up != sz)
      nbuckets = up / t->bucket.size;
  }

  bmap* buckets;
  if (dirtyalloc == NULL) {
    buckets = memalloczv(mem, t->bucket.size, nbuckets);
    if UNLIKELY(buckets == NULL) {
      *nextOverflow = NULL;
      return NULL;
    }
    // dlog("memalloc bmap[%zu] [%p-%p) (%zu * %zu = %zu B)",
    //   nbuckets, buckets, buckets + t->bucket.size*nbuckets,
    //   nbuckets, t->bucket.size, t->bucket.size * nbuckets);
  } else {
    // dirtyalloc was previously generated by the above branch but may not be empty
    buckets = dirtyalloc;
    memset(buckets, 0, t->bucket.size * nbuckets);
  }

  if (base != nbuckets) {
    // We preallocated some overflow buckets.
    // To keep the overhead of tracking these overflow buckets to a minimum,
    // we use the convention that if a preallocated overflow bucket's overflow
    // pointer is nil, then there are more available by bumping the pointer.
    // We need a safe non-nil pointer for the last overflow bucket; just use buckets.
    *nextOverflow = (bmap*)((void*)buckets + base*t->bucket.size);
    bmap* last = (bmap*)((void*)buckets + (nbuckets - 1)*t->bucket.size);
    bmap_setoverflow(last, t, buckets);
  } else {
    *nextOverflow = NULL;
  }

  return buckets;
}


// is_bucket_evacuated aka bucketEvacuated
static bool is_bucket_evacuated(const HMapType* t, HMap* h, usize bucket) {
  bmap* b = (void*)h->oldbuckets + bucket*t->bucket.size;
  return is_evacuated(b);
}


static void advanceEvacuationMark(const HMapType* t, HMap* h, usize newbit, Mem mem) {
  h->nevacuate++;
  // Experiments suggest that 1024 is overkill by at least an order of magnitude.
  // Put it in there as a safeguard anyway, to ensure O(1) behavior.
  usize stop = h->nevacuate + 1024;
  if (stop > newbit)
    stop = newbit;
  while (h->nevacuate != stop && is_bucket_evacuated(t, h, h->nevacuate))
    h->nevacuate++;
  if (h->nevacuate != newbit)
    return;
  // newbit == # of oldbuckets
  // Growing is all done. Free old main bucket array.
  memfree(mem, h->oldbuckets);
  h->oldbuckets = NULL;
  // Can discard old overflow buckets as well.
  // If they are still referenced by an iterator,
  // then the iterator holds a pointers to the slice.
  if (h->extra != NULL)
    PtrArrayClear(&h->extra->oldoverflow);
  h->flags &= ~H_sameSizeGrow;
}


// evacDst is an evacuation destination.
typedef struct {
  bmap* b; // current destination bucket
  u32   i; // key/elem index into b
  void* k; // pointer to current key storage
  void* e; // pointer to current elem storage
} evacDst;


static bool evacuate(const HMapType* t, HMap* h, usize oldbucket, Mem mem) {
  assertnotnull(h->oldbuckets);
  bmap* b = (void*)h->oldbuckets + oldbucket*t->bucket.size;
  usize newbit = hmap_oldbucketcount(h);

  if (is_evacuated(b))
    goto end;

  // TODO: reuse overflow buckets instead of using new ones, if there
  // is no iterator using the old buckets.  (If !H_oldIterator.)

  // xy contains the x and y (low and high) evacuation destinations.
  evacDst xy[2] = {0};
  evacDst* x = &xy[0];
  x->b = (void*)h->buckets + oldbucket*t->bucket.size;
  x->k = (void*)x->b + dataOffset;
  x->e = x->k + bucketCnt*t->keysize;
  assertnotnull(x->b);

  if (!hmap_sameSizeGrow(h)) {
    // Only calculate y pointers if we're growing bigger.
    // Otherwise GC can see bad pointers.
    evacDst* y = &xy[1];
    y->b = (void*)h->buckets + (oldbucket + newbit)*t->bucket.size;
    y->k = (void*)y->b + dataOffset;
    y->e = y->k + bucketCnt*t->keysize;
  }

  for (; b != NULL; b = bmap_overflow(b, t)) {
    void* k = (void*)b + dataOffset;
    void* e = k + bucketCnt*t->keysize;

    for (
      usize i = 0;
      i < bucketCnt;
      ({
        i++;
        k += t->keysize;
        e += t->elemsize;
      }) )
    {
      u8 top = b->tophash[i];
      if (isEmpty(top)) {
        b->tophash[i] = evacuatedEmpty;
        continue;
      }
      assertf(top >= minTopHash, "bad map state");
      void* k2 = k;
      if (maptype_indirectkey(t))
        k2 = *(void**)k2;
      u8 useY = 0;
      if (!hmap_sameSizeGrow(h)) {
        // Compute hash to make our evacuation decision (whether we need
        // to send this key/elem to bucket x or bucket y).
        uintptr hash = t->hasher(k2, (uintptr)h->hash0);
        if ((h->flags & H_iterator) &&
            !maptype_reflexivekey(t) &&
            !t->key.equal(k2, k2))
        {
          // If key != key (NaNs), then the hash could be (and probably
          // will be) entirely different from the old hash. Moreover,
          // it isn't reproducible. Reproducibility is required in the
          // presence of iterators, as our evacuation decision must
          // match whatever decision the iterator made.
          // Fortunately, we have the freedom to send these keys either
          // way. Also, tophash is meaningless for these kinds of keys.
          // We let the low bit of tophash drive the evacuation decision.
          // We recompute a new random tophash for the next level so
          // these keys will get evenly distributed across all buckets
          // after multiple grows.
          useY = top & 1;
          top = tophash(hash);
        } else {
          if (hash & newbit)
            useY = 1;
        }
      }

      assert(evacuatedX+1 == evacuatedY && (evacuatedX^1) == evacuatedY);

      b->tophash[i] = evacuatedX + useY; // evacuatedX + 1 == evacuatedY
      evacDst* dst = &xy[useY];          // evacuation destination

      if (dst->i == bucketCnt) {
        dst->b = hmap_newoverflow(h, t, dst->b, mem);
        if UNLIKELY(dst->b == NULL)
          return false;
        dst->i = 0;
        dst->k = (void*)dst->b + dataOffset;
        dst->e = dst->k + bucketCnt*t->keysize;
      }

      // Note: mask dst->i as an optimization, to avoid a bounds check
      assertnotnull(dst->b)->tophash[dst->i & (bucketCnt-1)] = top;

      if (maptype_indirectkey(t)) {
        *(void**)dst->k = k2; // copy pointer
      } else {
        if (dst->k != k) // TODO: is this condition ever false?
          memmove(dst->k, k, t->key.size); // copy elem
      }
      if (maptype_indirectelem(t)) {
        *(void**)dst->e = *(void**)e;
      } else {
        if (dst->e != e) // TODO: is this condition ever false?
          memmove(dst->e, e, t->elem.size);
      }

      dst->i++;

      // These updates might push these pointers past the end of the
      // key or elem arrays.  That's ok, as we have the overflow pointer
      // at the end of the bucket to protect against pointing past the
      // end of the bucket.
      dst->k += t->keysize;
      dst->e += t->elemsize;
    } // end for(i)
  } // end for(b)

  // TODO: Revisit the following logic to better understand its effects.
  // Should we be freeing memory?
  // (Note that bucket.ptrdata is not used in this implementation b/c no GC.)
  //
  // // Unlink the overflow buckets & clear key/elem to help GC.
  // if ((h->flags & H_oldIterator) == 0 && t->bucket.ptrdata != 0) {
  //   void* b = h->oldbuckets + oldbucket*t->bucket.size;
  //   // Preserve b.tophash because the evacuation state is maintained there.
  //   void* ptr = b + dataOffset;
  //   usize n = t->bucket.size - dataOffset;
  //   memset(ptr, 0, n);
  // }

end:
  if (oldbucket == h->nevacuate)
    advanceEvacuationMark(t, h, newbit, mem);
  return true;
}


static void growWork(const HMapType* t, HMap* h, usize bucket, Mem mem) {
  assert(hmap_isgrowing(h));

  // make sure we evacuate the oldbucket corresponding
  // to the bucket we're about to use
  if UNLIKELY(!evacuate(t, h, bucket & hmap_oldbucketmask(h), mem))
    return;

  // evacuate one more oldbucket to make progress on growing
  if (hmap_isgrowing(h))
    evacuate(t, h, h->nevacuate, mem);
}


static bool hash_grow(const HMapType* t, HMap* h, Mem mem) {
  // If we've hit the load factor, get bigger.
  // Otherwise, there are too many overflow buckets,
  // so keep the same number of buckets and "grow" laterally.
  u8 bigger = 1;
  if (!is_over_loadFactor(h->count+1, h->B)) {
    bigger = 0;
    h->flags |= H_sameSizeGrow;
  }
  bmap* oldbuckets = h->buckets;
  bmap* nextOverflow = NULL;
  bmap* newbuckets = make_bucket_array(mem, t, h->B + bigger, NULL, &nextOverflow);
  if UNLIKELY(newbuckets == NULL)
    return false;

  hflag flags = h->flags & ~(H_iterator | H_oldIterator);
  if (h->flags & H_iterator)
    flags |= H_oldIterator;

  assertf(h->oldbuckets == NULL, "TODO free h->oldbuckets");

  // commit the grow
  h->B += bigger;
  h->flags = flags;
  h->oldbuckets = oldbuckets;
  h->buckets = newbuckets;
  h->nevacuate = 0;
  h->noverflow = 0;

  if (h->extra != NULL && h->extra->overflow.len != 0) {
    // Promote current overflow buckets to the old generation
    asserteq(h->extra->oldoverflow.len, 0);
    // swap PtrArrays:
    h->extra->oldoverflow = h->extra->overflow;
    memset(&h->extra->overflow, 0, sizeof(h->extra->overflow));
  }

  if (nextOverflow != NULL) {
    hmap_createoverflow(h, 0, mem);
    h->extra->nextOverflow = nextOverflow;
  }
  // the actual copying of the hash table data is done incrementally
  // by growWork() and evacuate().
  return true;
}


// Like map_access, but allocates a slot for the key if it is not present in the map.
// Returns pointer to value storage.
void* nullable map_assign(const HMapType* t, HMap* h, void* key, Mem mem) {
  assertnotnull(h);
  safecheckf((h->flags & H_hashWriting) == 0, "concurrent map writes");
  h->flags ^= H_hashWriting;

  uintptr hash = t->hasher(key, (uintptr)h->hash0);

  if (h->buckets == NULL) {
    void* buckets = memallocz(mem, t->bucket.size);
    if UNLIKELY(buckets == NULL)
      return NULL;
    h->buckets = buckets;
    // dlog("memalloc bmap[1] [%p-%p) (%zu B)",
    //   h->buckets, h->buckets + t->bucket.size, t->bucket.size);
  }

  usize bucket;

again:
  bucket = hash & bucketMask(h->B);
  if (hmap_isgrowing(h))
    growWork(t, h, bucket, mem);

  bmap* b = (void*)h->buckets + bucket*t->bucket.size;
  u8 top = tophash(hash);

  u8* inserti = NULL;
  void* insertk = NULL;
  void* elem = NULL;

  while (1) { // bucketloop
    for (usize i = 0; i < bucketCnt; i++) { // for1
      if (b->tophash[i] != top) {
        if (isEmpty(b->tophash[i]) && inserti == NULL) {
          inserti = &b->tophash[i];
          insertk = (void*)b + dataOffset + i*t->keysize;
          elem = (void*)b + dataOffset + bucketCnt*t->keysize + i*t->elemsize;
        }
        if (b->tophash[i] == emptyRest)
          goto end_bucketloop;
        continue; // for1
      }
      void* k = (void*)b + dataOffset + i*t->keysize;
      if (maptype_indirectkey(t))
        k = *(void**)k;
      if (!t->key.equal(key, k))
        continue; // for1
      // already have a mapping for key. Update it.
      if (maptype_needkeyupdate(t) && k != key)
        memmove(k, key, t->key.size);
      elem = (void*)b + dataOffset + bucketCnt*t->keysize + i*t->elemsize;
      goto done;
    }
    bmap* ovf = bmap_overflow(b, t);
    if (ovf == NULL)
      break;
    b = ovf;
  }
end_bucketloop:
  // Did not find mapping for key. Allocate new cell & add entry.

  // If we hit the max load factor or we have too many overflow buckets,
  // and we're not already in the middle of growing, start growing.
  if (!hmap_isgrowing(h) &&
      ( is_over_loadFactor(h->count + 1, h->B) ||
        is_tooManyOverflowBuckets(h->noverflow, h->B)) )
  {
    if UNLIKELY(!hash_grow(t, h, mem))
      return NULL;
    goto again; // Growing the table invalidates everything, so try again
  }

  if (!inserti) {
    // The current bucket and all the overflow buckets connected to it are full;
    // allocate a new one.
    bmap* newb = hmap_newoverflow(h, t, b, mem);
    if UNLIKELY(newb == NULL)
      return NULL;
    inserti = &newb->tophash[0];
    insertk = (void*)newb + dataOffset;
    elem = insertk + bucketCnt*t->keysize;
  }

  // store new key/elem at insert position
  if (maptype_indirectkey(t)) {
    dlog("TODO free kmem storage later");
    void* kmem = memallocz(mem, t->key.size);
    *(void**)insertk = kmem;
    insertk = kmem;
  }
  if (maptype_indirectelem(t)) {
    void* vmem = memallocz(mem, t->elem.size);
    dlog("TODO free vmem storage %p (%zu B) later", vmem, t->elem.size);
    *(void**)elem = vmem;
  }
  assert(insertk != key);
  memmove(insertk, key, t->key.size);
  *inserti = top;
  h->count++;

done:
  safecheckf(h->flags & H_hashWriting, "concurrent map writes");
  h->flags &= ~H_hashWriting;
  if (maptype_indirectelem(t))
    elem = *(void**)elem;
  return elem;
}


inline static void delete_cleanup(
  const HMapType* t, HMap* h, bmap* b, bmap* bOrig, usize i)
{
  if (i == bucketCnt-1) {
    bmap* ovf = bmap_overflow(b, t);
    if (ovf != NULL && ovf->tophash[0] != emptyRest)
      return;
  } else if (b->tophash[i+1] != emptyRest) {
    return;
  }
  for (;;) {
    b->tophash[i] = emptyRest;
    if (i == 0) {
      if (b == bOrig)
        break; // beginning of initial bucket, we're done.
      // Find previous bucket, continue at its last entry.
      bmap* c = b;
      for (b = bOrig; bmap_overflow(b, t) != c; )
        b = bmap_overflow(b, t);
      i = bucketCnt - 1;
    } else {
      i--;
    }
    if (b->tophash[i] != emptyOne)
      break;
  }
}


void* nullable map_delete(const HMapType* t, HMap* nullable h, void* key, Mem mem) {
  if (h == NULL || h->count == 0)
    return NULL;

  safecheckf((h->flags & H_hashWriting) == 0, "concurrent map writes");
  h->flags ^= H_hashWriting;

  uintptr hash = t->hasher(key, (uintptr)h->hash0);
  usize bucket = hash & bucketMask(h->B);

  if (hmap_isgrowing(h))
    growWork(t, h, bucket, mem);

  bmap* b = (void*)h->buckets + bucket*t->bucket.size;
  bmap* bOrig = b;
  u8 top = tophash(hash);
  void* founde = NULL;

  for (; b != NULL; b = bmap_overflow(b, t)) {
    for (usize i = 0; i < bucketCnt; i++) {
      if (b->tophash[i] != top) {
        if (b->tophash[i] == emptyRest)
          goto end_search;
        continue;
      }
      void* k = (void*)b + dataOffset + i*t->keysize;
      void* k2 = k;
      if (maptype_indirectkey(t))
        k2 = *(void**)k2;
      if (!t->key.equal(key, k))
        continue;

      // Only clear key if there are pointers in it.
      if (maptype_indirectkey(t))
        *(void**)k = NULL; // TODO: free key?

      founde = (void*)b + dataOffset + bucketCnt*t->keysize + i*t->elemsize;
      if (maptype_indirectelem(t)) {
        founde = *(void**)founde;
        // Note: Go clears the entry's data, likely to allow the old value to be GC'd.
        // Instead, in this C implementation, we rely on the caller to clean up the
        // removed value (zero it, free it, etc.)
        // if maptype_indirectelem(t):
        //   *(void**)founde = NULL;
        // else:
        //   memset(e, 0, t->elem.size);
      }
      b->tophash[i] = emptyOne; // tophash_flag

      // If the bucket now ends in a bunch of emptyOne states,
      // change those to emptyRest states.
      delete_cleanup(t, h, b, bOrig, i);

      h->count--;
      // Reset the hash seed to make it more difficult for attackers to
      // repeatedly trigger hash collisions. See Go issue 25237.
      if (h->count == 0 && (h->flags & H_deterministic) == 0)
        h->hash0 = fastrand();
      goto end_search;
    }
  }
end_search:

  safecheckf(h->flags & H_hashWriting, "concurrent map writes");
  h->flags &= ~H_hashWriting;
  return founde;
}


static void* nullable map_access1(const HMapType* t, const HMap* nullable h, void** kp) {
  if (h == NULL || h->count == 0)
    return NULL;

  safecheckf((h->flags & H_hashWriting) == 0, "concurrent map read & write");

  void* key = *kp;
  uintptr hash = t->hasher(key, (uintptr)h->hash0);
  usize m = bucketMask(h->B);
  bmap* b = ((void*)h->buckets + (hash & m)*t->bucket.size);
  bmap* c = h->oldbuckets;

  if (c != NULL) {
    if (!hmap_sameSizeGrow(h)) {
      // There used to be half as many buckets; mask down one more power of two.
      m >>= 1;
    }
    bmap* oldb = (bmap*)((void*)c + (hash & m)*t->bucket.size);
    if (!is_evacuated(oldb))
      b = oldb;
  }

  u8 top = tophash(hash);
  for (; b != NULL; b = bmap_overflow(b, t)) {
    for (usize i = 0; i < bucketCnt; i++) {
      // check if top of hash matches the bucket b
      if (b->tophash[i] != top) {
        if (b->tophash[i] == emptyRest)
          return NULL; // not found
        continue; // check next bucket
      }
      // hash is indeed in this bucket; load key
      void* k = (void*)b + dataOffset + i*t->keysize;
      if (maptype_indirectkey(t))
        k = *(void**)k;
      // check if key is truly equal, and if so we found a matching entry
      if (t->key.equal(key, k)) {
        void* e = (void*)b + dataOffset + bucketCnt*t->keysize + i*t->elemsize;
        if (maptype_indirectelem(t))
          e = *(void**)e;
        *kp = k;
        return e;
      }
    }
  }

  // not found
  return NULL;
}


void* nullable map_access(const HMapType* t, const HMap* nullable h, void* key) {
  return map_access1(t, h, &key);
}


void map_clear(const HMapType* t, HMap* nullable h, Mem mem) {
  if (h == NULL || h->count == 0)
    return;

  safecheckf((h->flags & H_hashWriting) == 0, "concurrent map writes");
  h->flags ^= H_hashWriting;

  h->flags &= ~H_sameSizeGrow;
  if (h->oldbuckets != NULL) {
    memfree(mem, h->oldbuckets);
    h->oldbuckets = NULL;
  }
  h->nevacuate = 0;
  h->noverflow = 0;
  h->count = 0;

  // Reset the hash seed to make it more difficult for attackers to
  // repeatedly trigger hash collisions. See Go issue 25237.
  if ((h->flags & H_deterministic) == 0)
    h->hash0 = fastrand();

  // Keep the HMapExtra allocation but clear any extra information
  if (h->extra != NULL) {
    PtrArrayClear(&h->extra->overflow);
    PtrArrayFree(&h->extra->oldoverflow, mem);
    h->extra->nextOverflow = NULL;
    // note: we don't free h->extra->nextOverflow; it is a pointer into h->buckets
  }

  // make_bucket_array clears the memory pointed to by h->buckets and recovers
  // any overflow buckets by generating them as if h->buckets was newly allocated.
  bmap* nextOverflow = NULL;
  make_bucket_array(mem, t, h->B, h->buckets, &nextOverflow);
  if (nextOverflow != NULL) {
    // If overflow buckets are created then h->extra
    // will have been allocated during initial bucket creation.
    h->extra->nextOverflow = nextOverflow;
  }

  safecheckf(h->flags & H_hashWriting, "concurrent map writes");
  h->flags &= ~H_hashWriting;
}


void map_free(const HMapType* t, HMap* h, Mem mem) {
  if (h->extra != NULL) {
    PtrArrayFree(&h->extra->overflow, mem);
    PtrArrayFree(&h->extra->oldoverflow, mem);
    // note: we don't free h->extra->nextOverflow; it is a pointer into h->buckets
    memfree(mem, h->extra);
  }
  if (h->oldbuckets != NULL)
    memfree(mem, h->oldbuckets);
  if (h->buckets != NULL)
    memfree(mem, h->buckets);
  if (h->flags & H_hmemManaged)
    memfree(mem, h);
}


// map_make implements map creation for make(map[k]v, hint).
// If the compiler has determined that the map or the first bucket
// can be created on the stack, *hp and/or bucket may be non-null.
// If h != NULL, the map can be created directly in h.
// If h->buckets != NULL, bucket pointed to can be used as the first bucket.
// Return NULL on memory allocation failure or overflow from too large hint.
HMap* nullable map_make1(
  const HMapType* t, HMap* nullable h, Mem mem, usize hint, u32 hash0, u8 flags)
{
  // check if hint is too large
  usize z;
  if (check_mul_overflow(hint, t->bucket.size, &z))
    return NULL;

  if (h == NULL) {
    h = memalloczt(mem, HMap);
    if UNLIKELY(h == NULL)
      return NULL;
    h->flags |= H_hmemManaged;
  }

  assertf((flags & H_deterministic) == flags, "unexpected initial flags");
  h->flags |= flags;
  h->hash0 = hash0;

  // Find the size parameter B which will hold the requested # of elements.
  // For hint < 0 is_over_loadFactor returns false since hint < bucketCnt.
  u8 B = 0;
  while (is_over_loadFactor(hint, B))
    B++;
  h->B = B;

  // if B == 0, the buckets field is allocated lazily later (in map_assign)
  if (B != 0) {
    // allocate initial hash table
    // If hint is large zeroing this memory could take a while.
    bmap* nextOverflow = NULL;
    if (h->buckets != NULL)
      memfree(mem, h->buckets);
    void* buckets = make_bucket_array(mem, t, B, NULL, &nextOverflow);
    if UNLIKELY(buckets == NULL)
      goto free_and_fail;
    h->buckets = buckets;
    if (nextOverflow != NULL) {
      if UNLIKELY(!hmap_createoverflow(h, 0, mem))
        goto free_and_fail;
      h->extra->nextOverflow = nextOverflow;
    }
  }

  return h;

free_and_fail:
  if (h->buckets != NULL)
    memfree(mem, h->buckets);
  if (h->flags & H_hmemManaged)
    memfree(mem, h);
  return NULL;
}


HMap* nullable map_make(const HMapType* t, HMap* nullable h, Mem mem, usize hint) {
  return map_make1(t, h, mem, hint, fastrand(), 0);
}

HMap* nullable map_make_deterministic(
  const HMapType* t, HMap* nullable h, Mem mem, usize hint, u32 hash_seed)
{
  return map_make1(t, h, mem, hint, hash_seed, H_deterministic);
}


// map_new_small implements map creation when hint is known to be at most bucketCnt
// at compile time and the map needs to be allocated on the heap.
HMap* nullable map_new_small(Mem mem) {
  HMap* h = memalloczt(mem, HMap);
  if UNLIKELY(h == NULL)
    return NULL;
  h->flags |= H_hmemManaged;
  h->hash0 = fastrand();
  return h;
}


bool map_set_deterministic(HMap* h, bool enabled) {
  bool prevstate = (h->flags & H_deterministic) != 0;
  SET_FLAG(h->flags, H_deterministic, enabled);
  return prevstate;
}


usize map_bucketsize(const HMapType* t, usize count, usize alloc_overhead) {
  u8 B = 0;
  while (is_over_loadFactor(count, B))
    B++;

  // see comments in make_bucket_array
  usize base = bucketShift(B);
  usize nbuckets = base;
  if (B >= 4) {
    nbuckets += bucketShift(B - 4);
    usize sz = t->bucket.size * nbuckets;
    usize up = ALIGN2(sz, PTRSIZE);
    if (up != sz)
      nbuckets = up / t->bucket.size;
  }

  // alloc_overhead + (bucketsize * nbuckets)
  usize nbyte;
  if (check_mul_overflow(t->bucket.size, nbuckets, &nbyte))
    return 0;
  if (check_add_overflow(nbyte, alloc_overhead, &nbyte))
    return 0;
  return nbyte;
}


void map_iter_init(HMapIter* it, const HMapType* t, HMap* nullable h) {
  #if DEBUG
  HMapIter zeroit = {0};
  assertf(memcmp(it,&zeroit,sizeof(zeroit)) == 0, "iterator not zeroed");
  #endif

  it->t = t;
  if (h == NULL || h->count == 0)
    return;
  it->h = h;
  it->B = h->B;
  it->buckets = h->buckets;

  // decide where to start
  usize r = (usize)fastrand();
  if (h->B > 31-bucketCntBits)
    r += (usize)fastrand() << 31;
  it->startbucket = r & bucketMask(h->B);
  it->offset = (u8)(r >> h->B & (bucketCnt - 1));

  // iterator state
  it->bucket = it->startbucket;

  // remember we have an iterator
  h->flags |= H_iterator | H_oldIterator;

  // advance to first entry
  map_iter_next(it);
}


void* nullable map_iter_next(HMapIter* it) {
  HMap* h = it->h;
  safecheckf((h->flags & H_hashWriting) == 0, "concurrent map iteration & write");

  const HMapType* t = it->t;
  usize bucket      = it->bucket; // current bucket index
  bmap* b           = it->bptr;   // current bucket data
  u8 i              = it->i;      // current index in b
  usize checkbucket = it->checkbucket;

next:
  if (b == NULL) {
    if (bucket == it->startbucket && it->wrapped) {
      // end of iteration
      it->key = NULL;
      it->val = NULL;
      return NULL;
    }
    if (hmap_isgrowing(h) && it->B == h->B) {
      // Iterator was started in the middle of a grow, and the grow isn't done yet.
      // If the bucket we're looking at hasn't been filled in yet (i.e. the old
      // bucket hasn't been evacuated) then we need to iterate through the old
      // bucket and only return the ones that will be migrated to this bucket.
      usize oldbucket = bucket & hmap_oldbucketmask(h);
      b = h->oldbuckets + oldbucket*t->bucket.size;
      if (!is_evacuated(b)) {
        checkbucket = bucket;
      } else {
        b = it->buckets + bucket*t->bucket.size;
        checkbucket = IT_NO_CHECK;
      }
    } else {
      b = it->buckets + bucket*t->bucket.size;
      checkbucket = IT_NO_CHECK;
    }
    bucket++;
    if (bucket == bucketShift(it->B)) {
      bucket = 0;
      it->wrapped = true;
    }
    i = 0;
  }

  for ( ; i < bucketCnt; i++) {
    usize offi = (i + it->offset) & (bucketCnt - 1);
    if (isEmpty(b->tophash[offi]) || b->tophash[offi] == evacuatedEmpty) {
      // TODO: emptyRest is hard to use here, as we start iterating
      // in the middle of a bucket. It's feasible, just tricky.
      continue;
    }
    void* k = (void*)b + dataOffset + offi*(uintptr)t->keysize;
    if (maptype_indirectkey(t))
      k = *(void**)k;
    void* v = (void*)b + dataOffset + bucketCnt*(uintptr)t->keysize + offi*t->elemsize;

    if (checkbucket != IT_NO_CHECK && !hmap_sameSizeGrow(h)) {
      // Special case: iterator was started during a grow to a larger size
      // and the grow is not done yet. We're working on a bucket whose
      // oldbucket has not been evacuated yet. Or at least, it wasn't
      // evacuated when we started the bucket. So we're iterating
      // through the oldbucket, skipping any keys that will go
      // to the other new bucket (each oldbucket expands to two
      // buckets during a grow).
      if (maptype_reflexivekey(t) || t->key.equal(k, k)) {
        // If the item in the oldbucket is not destined for
        // the current new bucket in the iteration, skip it.
        uintptr hash = t->hasher(k, (uintptr)h->hash0);
        if ((hash & bucketMask(it->B)) != checkbucket)
          continue;
      } else {
        // Hash isn't repeatable if k != k (NaNs).  We need a
        // repeatable and randomish choice of which direction
        // to send NaNs during evacuation. We'll use the low
        // bit of tophash to decide which way NaNs go.
        // NOTE: this case is why we need two evacuate tophash
        // values, evacuatedX and evacuatedY, that differ in
        // their low bit.
        if ((checkbucket >> (it->B - 1)) != ((uintptr)b->tophash[offi] & 1))
          continue;
      }
    }

    if ((b->tophash[offi] != evacuatedX && b->tophash[offi] != evacuatedY) ||
        !(maptype_reflexivekey(t) ||
        t->key.equal(k, k)) )
    {
      // This is the golden data, we can return it.
      // OR
      // key!=key, so the entry can't be deleted or updated, so we can just return it.
      // That's lucky for us because when key!=key we can't look it up successfully.
      it->key = k;
      if (maptype_indirectelem(t))
        v = *(void**)v;
      it->val = v;
    } else {
      // The hash table has grown since the iterator was started.
      // The golden data for this key is now somewhere else.
      // Check the current hash table for the data.
      // This code handles the case where the key
      // has been deleted, updated, or deleted and reinserted.
      // NOTE: we need to regrab the key as it has potentially been
      // updated to an equal() but not identical key (e.g. +0.0 vs -0.0).

      void* rk = NULL;
      void* rv = map_access1(t, h, &rk);
      if (rk == NULL)
        continue; // key has been deleted
      it->key = rk;
      it->val = rv;
    }

    it->bucket = bucket;
    it->bptr = b; // see Go issue 14921 (Go only writes here if they differ)
    it->i = i + 1;
    it->checkbucket = checkbucket;
    return it->key;
  } // for (i in bucketCnt)

  b = bmap_overflow(b, t);
  i = 0;
  goto next;
}


// ————————————————————————————————————————————————————————————————————————————————————
// map types

static bool i32_equal(const i32* a, const i32* b) { return *a == *b; }
static bool ptr_equal(const void** a, const void** b) { return *a == *b; }

// static const rtype kRType_i32 = {
//   .size = sizeof(i32),
//   .align = sizeof(i32),
//   .kind = tkind_sint,
//   .equal = (equalfun)&i32_equal,
// };

// MAPTYPE defines a HMapType at compile time
#define MAPTYPE(kt, kalign, keqf, vt, valign, veqf, hashf, mtflags) {        \
  .bucket = {                                                                \
    .align = PTRSIZE,                                                        \
    .kind  = tkind_struct,                                                   \
    .size  = MAPTYPE_SIZE(kt, vt),                                           \
  },                                                                         \
  .key = {                                                                   \
    .size = sizeof(kt),                                                      \
    .align = kalign,                                                         \
    .equal = (equalfun)(keqf),                                               \
  },                                                                         \
  .elem = {                                                                  \
    .size = sizeof(vt),                                                      \
    .align = valign,                                                         \
    .equal = (equalfun)(veqf),                                               \
  },                                                                         \
  .hasher = (keyhasher)(hashf),                                              \
  .keysize = sizeof(kt),                                                     \
  .elemsize = sizeof(vt),                                                    \
  .flags = mtflags,                                                          \
};                                                                           \
static_assert((MAPTYPE_SIZE(kt, vt) & alignof(kt) - 1) == 0, "not aligned"); \
static_assert((MAPTYPE_SIZE(kt, vt) & alignof(vt) - 1) == 0, "not aligned"); \
static_assert(sizeof(kt) <= maxKeySize, "must use make_ptr_type(ktyp)");     \
static_assert(sizeof(vt) <= maxKeySize, "must use make_ptr_type(vtyp)")

#define MAPTYPE_SIZE(kt, vt) (bucketCnt*(1 + sizeof(kt) + sizeof(vt)) + PTRSIZE)

const HMapType kMapType_i32_i32 =
  MAPTYPE(i32, sizeof(i32), &i32_equal,
          i32, sizeof(i32), &i32_equal,
          &hash_4, maptypeReflexiveKey);

const HMapType kMapType_ptr_ptr =
  MAPTYPE(void*, sizeof(void*), &ptr_equal,
          void*, sizeof(void*), &ptr_equal,
          &hash_ptr, maptypeReflexiveKey);


// ————————————————————————————————————————————————————————————————————————————————————
#ifdef CO_TESTING_ENABLED

static rtype make_ptr_type(const rtype* etyp) {
  return (rtype){ .size = PTRSIZE, .align = PTRSIZE, .kind = tkind_ptr };
}

static rtype make_bucket_type(const rtype* ktyp, const rtype* etyp) {
  rtype ktyp2, etyp2;
  if (ktyp->size > maxKeySize) {
    ktyp2 = make_ptr_type(ktyp);
    ktyp = &ktyp2;
  }
  if (etyp->size > maxElemSize) {
    etyp2 = make_ptr_type(etyp);
    etyp = &etyp2;
  }

  const usize overflowPad = 0;
  usize size = bucketCnt*(1 + ktyp->size + etyp->size) + overflowPad + PTRSIZE;
  assert( ! ( (size & ktyp->align-1) != 0 || (size & etyp->align-1) != 0) );

  return (rtype){
    .align = PTRSIZE,
    .size  = size,
    .kind  = tkind_struct,
  };
}

UNUSED
static HMapType mkmaptype(
  const rtype* ktyp, const rtype* vtyp, keyhasher nullable hasher)
{
  HMapType mt = {
    .bucket = make_bucket_type(ktyp, vtyp),
    .key = *ktyp,
    .elem = *vtyp,
    .hasher = hasher ? hasher : rtype_hasher(ktyp),
  };
  assertnotnull(ktyp->equal);

  #ifdef ENABLE_LARGE_ENTRIES
    if (ktyp->size > maxKeySize) {
      mt.keysize = PTRSIZE;
      mt.flags |= maptypeIndirectKey;
    } else {
      mt.keysize = (u8)ktyp->size;
    }
    if (vtyp->size > maxElemSize) {
      mt.elemsize = PTRSIZE;
      mt.flags |= maptypeIndirectElem;
    } else {
      mt.elemsize = (u8)vtyp->size;
    }
  #else
    assertf(ktyp->size <= maxKeySize, "requires ENABLE_LARGE_ENTRIES");
    assertf(vtyp->size <= maxElemSize, "requires ENABLE_LARGE_ENTRIES");
  #endif

  if (rtype_isReflexive(ktyp))
    mt.flags |= maptypeReflexiveKey;
  if (rtype_needKeyUpdate(ktyp))
    mt.flags |= maptypeNeedKeyUpdate;
  return mt;
}


DEF_TEST(map) {
  Mem mem = mem_libc_allocator();
  fastrand_seed(1234);

  // HMapType mt = mkmaptype(&kRType_i32, &kRType_i32, NULL);
  const HMapType* mt = &kMapType_i32_i32;

  #if 0
    // create a map on the heap with a hint
    HMap* h = map_make(mt, NULL, mem, 4);
    assertf(h != NULL, "map_make failed");
  #else
    // initialize a map stored on the stack (with implicit hint <= bucketCnt)
    HMap hstk = { .buckets = memallocz(mem, mt->bucket.size) };
    // HMap hstk = { 0 };
    HMap* h = map_init_small(&hstk);
    map_set_deterministic(h, true);
  #endif

  i32 insert_count = 200;
  asserteq(map_len(h), 0);

  // insert
  for (i32 key = 0; key < insert_count; key++) {
    i32* valp = map_assign(mt, h, &key, mem);
    assertf(valp != NULL, "out of memory");
    *valp = key;
  }

  // lookup
  for (i32 key = 0; key < insert_count; key++) {
    i32* valp = map_access(mt, h, &key);
    assertnotnull(valp);
    asserteq(key, *valp);
  }

  // replace
  for (i32 key = insert_count/2; key < insert_count; key++) {
    i32* valp = map_assign(mt, h, &key, mem);
    assertf(valp != NULL, "out of memory");
    // NOTE: it is the caller's responsibility to free an existing *valp
    // if it is a pointer to heap memory.
    *valp = key;
  }

  // delete
  for (i32 key = insert_count; key < insert_count; key++) {
    i32* valp = map_delete(mt, h, &key, mem);
    assertf(valp != NULL, "key %d not found/removed", key);
    // NOTE: it is the caller's responsibility to free an existing *valp
    // if it is a pointer to heap memory. In safety-oriented code the caller
    // might also want to memset(valp, 0, sizeof(*valp)).
  }

  asserteq(h->count, (usize)insert_count);
  map_clear(mt, h, mem);
  asserteq(h->count, 0);

  for (i32 key = 0; key < insert_count; key++)
    assertnull(map_access(mt, h, &key));

  map_free(mt, h, mem);

  // exit(0);
}


DEF_TEST(map_iter) {
  u8 membuf[2048];
  FixBufAllocator ma = {0};

  const HMapType* mt = &kMapType_i32_i32;
  u32 hash0 = 0xfeedface;
  i32 insert_count = 20;

  Mem mem = FixBufAllocatorInit(&ma, membuf, sizeof(membuf));
  HMap hstk = {0};
  HMap* h = map_make_deterministic(mt, &hstk, mem, insert_count, hash0);

  i64 expectsum = 0;
  for (i32 key = 0; key < insert_count; key++) {
    i32* valp = map_assign(mt, h, &key, mem);
    assertf(valp != NULL, "out of memory");
    *valp = key;
    expectsum += (i64)key;
  }

  HMapIter it = {0};
  map_iter_init(&it, mt, h);
  i64 actualsum = 0;
  while (it.key) {
    // dlog("%4d => %-4d (%p => %p)", *(i32*)it.key, *(i32*)it.val, it.key, it.val);
    actualsum += (i64)*(i32*)it.val;
    map_iter_next(&it);
  }

  assertf(expectsum == actualsum, "iterator should visit every entry exactly once");
}


DEF_TEST(map_allocfail) {
  const HMapType* mt = &kMapType_i32_i32;
  u8 membuf[sizeof(HMap) + MAPTYPE_SIZE(i32,i32) * 128] = {0};
  FixBufAllocator ma = {0};
  u32 hash0 = 0xfeedface;

  { // test alloc failure of HMap in map_make
    Mem mem = FixBufAllocatorInit(&ma, membuf, sizeof(HMap)-1);
    HMap* h = map_make_deterministic(mt, NULL, mem, 0, hash0);
    assertnull(h);
  }

  { // test alloc failure of initial buckets
    //
    // When the number of buckets brings B over 0, map_make calls make_bucket_array
    // to allocate both the first bucket and the Nth buckets, whereas when B==0 in
    // map_make, allocation of the first bucket happens lazily in map_assign.
    // Therefore, the expected allocation size in this test is 2x buckets, not 1.
    //
    // First, make sure our calculations are precise; the following should allocate
    // exactly memsize in total:
    usize memsize = sizeof(HMap) + MAPTYPE_SIZE(i32,i32)*2 + kFixBufAllocatorOverhead*2;
    Mem mem = FixBufAllocatorInit(&ma, membuf, memsize);
    assertnotnull(mem);
    assertnotnull(map_make_deterministic(mt, NULL, mem, bucketCnt + 1, hash0));
    assertf(ma.cap == ma.len, "memory exhausted");
    //
    // Second, we reduce the available memory by 1 byte -- this should fail
    memsize = sizeof(HMap) + MAPTYPE_SIZE(i32,i32)*2 + kFixBufAllocatorOverhead*2 - 1;
    mem = FixBufAllocatorInit(&ma, membuf, memsize);
    assertnotnull(mem);
    assertnull(map_make_deterministic(mt, NULL, mem, bucketCnt + 1, hash0));
  }

  { // test alloc failure during growth (lazy initial bucket)
    usize memsize = sizeof(HMap) + kFixBufAllocatorOverhead;
    Mem mem = FixBufAllocatorInit(&ma, membuf, memsize);
    assertnotnull(mem);
    HMap* h = assertnotnull(map_make_deterministic(mt, NULL, mem, 0, hash0));
    i32 key = 1;
    void* vp = map_assign(mt, h, &key, mem);
    assertnull(vp);
  }

  { // test alloc failure during growth (extra buckets)
    usize memsize = sizeof(HMap) + MAPTYPE_SIZE(i32,i32)*2 + kFixBufAllocatorOverhead*2;
    Mem mem = FixBufAllocatorInit(&ma, membuf, memsize);
    assertnotnull(mem);
    HMap* h = assertnotnull(map_make_deterministic(mt, NULL, mem, bucketCnt + 1, hash0));
    for (i32 key = 0; key < (i32)bucketCnt*2; key++) {
      if (is_over_loadFactor(h->count + 1, h->B)) {
        // hash_grow is called and it should fail to allocate memory
        assertnull(map_assign(mt, h, &key, mem));
        break;
      }
      assertnotnull(map_assign(mt, h, &key, mem));
    }
  }

  { // test alloc failure during evacuate.
    // This seed specifically triggers hmap_newoverflow in evacuate
    // and was found empirically.
    fastrand_seed(158);
    i32 insert_count = 14;
    // calculate exactly how much memory we need up until the point where
    // hmap_newoverflow is called by evacuate, so that we run out of memory
    // just at the right time.
    usize memsize =
      ALIGN2(sizeof(i32) + kFixBufAllocatorOverhead, kFixBufAllocatorAlign)
      * insert_count;
    Mem mem = FixBufAllocatorInit(&ma, membuf, memsize);
    HMap hstk = { .buckets = memallocz(mem, mt->bucket.size) };
    HMap* h = map_init_small(&hstk);
    for (i32 key = 0; key < insert_count; key++) {
      void* vp = map_assign(mt, h, &key, mem);
      if (key < insert_count-1) {
        assertnotnull(vp);
      } else {
        // hmap_newoverflow should have failed
        assertnull(vp);
      }
    }
    //
    // program to find seed for smallest number of keys to trigger hmap_newoverflow
    // during evacuation.
    // Add a global "static int g_hitcount1 = 0;"
    // and "g_hitcount1++;" in the evacuate function where it calls hmap_newoverflow.
    #if 0
      u32 maxseed = 100000;
      u32 reportseed = maxseed/100;
      u32 bestseed = 0;
      i32 minkey = I32_MAX-1;
      usize memsizes[2] = {0,0};
      fprintf(stderr, "");
      for (u32 seed = 0; seed < maxseed; seed++) {
        // progress report
        if (seed % reportseed == 0) {
          int t = (int)ceil((float)seed / maxseed * 100.0f);
          fprintf(stderr, "\r\e[0K" "finding best seed %.*s%.*s %u%%",
            t/2,      "||||||||||||||||||||||||||||||||||||||||||||||||||",
            50 - t/2, "..................................................", t);
        }
        fastrand_seed(seed);
        g_hitcount1 = 0; // reset
        Mem mem = FixBufAllocatorInit(&ma, membuf, sizeof(membuf));
        HMap hstk = { .buckets = memallocz(mem, mt->bucket.size) };
        HMap* h = map_init_small(&hstk);
        i32 insert_count = 106;
        for (i32 key = 0; key < insert_count; key++) {
          usize malen1 = ma.len;
          map_assign(mt, h, &key, mem);
          if (g_hitcount1 > 0) {
            if (key < minkey) {
              minkey = key;
              bestseed = seed;
              memsizes[0] = malen1;
              memsizes[1] = ma.len;
            }
            break;
          }
        }
      }
      fprintf(stderr, "\r\e[0K");
      log("best seed: %u (%d keys, ma.len %zu -> %zu)",
        bestseed, minkey+1, memsizes[0], memsizes[1]);
      // best seed: 158 (14 keys, ma.len 336 -> 800)
    #endif
  }

  // exit(0);
}


#ifdef ENABLE_LARGE_ENTRIES
  typedef struct { u8 v[maxElemSize + 1]; } test_bigval;
  static bool test_bigval_equal(const test_bigval* a, const test_bigval* b) {
    return memcmp(a->v, b->v, sizeof(a->v)) == 0;
  }
  // static usize test_bigval_hash(const test_bigval* v, usize seed) {
  //   return hash_i32((i32*)v->v, seed);
  // }

  DEF_TEST(map_bigval) {
    Mem mem = mem_libc_allocator();
    fastrand_seed(1234);

    const rtype ktyp = {
      .size = sizeof(i32),
      .align = sizeof(i32),
      .kind = tkind_sint,
      .equal = (equalfun)&i32_equal,
    };
    const rtype vtyp = {
      .size = sizeof(test_bigval),
      .align = alignof(test_bigval),
      .kind = tkind_struct,
      .equal = (equalfun)&test_bigval_equal,
    };
    const HMapType mt_ = mkmaptype(&ktyp, &vtyp, (keyhasher)&hash_i32);
    const HMapType* mt = &mt_;

    HMap hstk = { .buckets = memallocz(mem, mt->bucket.size) };
    HMap* h = map_init_small(&hstk);

    i32 insert_count = 3;

    // insert
    for (i32 key = 0; key < insert_count; key++) {
      test_bigval* valp = map_assign(mt, h, &key, mem);
      assertf(valp != NULL, "out of memory");
      memset(valp->v, ' '+key, sizeof(valp->v));
    }

    // lookup
    for (i32 key = 0; key < insert_count; key++) {
      test_bigval* valp = map_access(mt, h, &key);
      assertnotnull(valp);
      test_bigval cmp;
      memset(cmp.v, ' '+key, sizeof(cmp.v));
      assert(memcmp(valp->v, cmp.v, sizeof(cmp.v)) == 0);
    }

    // replace
    for (i32 key = insert_count/2; key < insert_count; key++) {
      test_bigval* valp = map_assign(mt, h, &key, mem);
      assertf(valp != NULL, "out of memory");
    }

    // delete
    for (i32 key = insert_count; key < insert_count; key++) {
      test_bigval* valp = map_delete(mt, h, &key, mem);
      assertf(valp != NULL, "key %d not found/removed", key);
    }
  }
#endif // ENABLE_LARGE_ENTRIES

#endif // CO_TESTING_ENABLED
