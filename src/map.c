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

#include "coimpl.h"
#include "map.h"
#include "test.h"
#include "hash.h"
#include "array.h"

#ifdef CO_WITH_LIBC
  #include <stdlib.h>
#endif

#define PTRSIZE sizeof(void*)

// Maximum number of key/elem pairs a bucket can hold.
#define bucketCntBits 3
#define bucketCnt     (1ul << bucketCntBits)

// Maximum average load of a bucket that triggers growth is 6.5.
// Represent as loadFactorNum/loadFactorDen, to allow integer math.
#define loadFactorNum 13
#define loadFactorDen 2

// Maximum key or elem size to keep inline (instead of mallocing per element).
// Must fit in a u8.
#define maxKeySize  128
#define maxElemSize 128

// dataOffset should be the size of the bmap struct, but needs to be aligned correctly.
// For amd64p32 this means 64-bit alignment even though pointers are 32 bit.
#define dataOffset offsetof(struct{ bmap b; i64 v; },v)

// sentinel bucket ID for iterator checks
#define noCheck (1ul << (8 * PTRSIZE) - 1)

typedef struct bmap     bmap;     // bucket
typedef struct hmap     hmap;     // header
typedef struct mapextra mapextra; // fields that are not present on all maps
typedef struct maptype  maptype;  // describes the type stored in a map
typedef struct rtype    rtype;

// keyhasher is a function for hashing keys (ptr to key, seed) -> hash
typedef uintptr(*keyhasher)(const void* keyp, uintptr seed);

// freefun is a function for freeing removed keys and entries.
// pv is an array of count pointers to free.
typedef void(*freefun)(Mem, void** pv, usize count);

// equalfun is used by rtype
typedef bool(*equalfun)(const void*, const void*);

// tflag is used by an rtype to signal what extra type information is
// available in the memory directly following the rtype value.
typedef u8 tflag;
enum tflag {
  // tflagUncommon means that there is a pointer, *uncommonType,
  // just beyond the outer type structure.
  //
  // For example, if t.Kind() == Struct and t.tflag&tflagUncommon != 0,
  // then t has uncommonType data and it can be accessed as:
  //
  //  type tUncommon struct {
  //    structType
  //    u uncommonType
  //  }
  //  u := &(*tUncommon)(unsafe.Pointer(t)).u
  tflagUncommon = 1 << 0,

  // tflagExtraStar means the name in the str field has an
  // extraneous '*' prefix. This is because for most types T in
  // a program, the type *T also exists and reusing the str data
  // saves binary size.
  tflagExtraStar = 1 << 1,

  // tflagNamed means the type has a name.
  tflagNamed = 1 << 2,

  // tflagRegularMemory means that equal and hash functions can treat
  // this type as a single region of t.size bytes.
  tflagRegularMemory = 1 << 3,
} END_TYPED_ENUM(tflag)


// Possible tophash values. We reserve a few possibilities for special marks.
// Each bucket (including its overflow buckets, if any) will have either all or none
// of its entries in the evacuated* states (except during the evacuate() method,
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
enum hflag {
  H_iterator     = 1 << 0, // there may be an iterator using buckets
  H_oldIterator  = 1 << 1, // there may be an iterator using oldbuckets
  H_hashWriting  = 1 << 2, // a goroutine is writing to the map
  H_sameSizeGrow = 1 << 3, // the current map growth is to a new map of the same size
  H_hmemManaged  = 1 << 4, // memory for hmap should be freed by mapfree
} END_TYPED_ENUM(hflag);

typedef u8 tkind;
enum tkind {
  tkind_invalid,
  tkind_ptr,
  tkind_sint,
  tkind_uint,
  tkind_float,
  tkind_struct,
} END_TYPED_ENUM(tkind);

typedef u8 maptypeflag;
enum maptypeflag {
  maptypeIndirectKey   = 1 << 0, // store ptr to key instead of key itself
  maptypeIndirectElem  = 1 << 1, // store ptr to elem instead of elem itself
  maptypeReflexiveKey  = 1 << 2, // k==k for all keys
  maptypeNeedKeyUpdate = 1 << 3, // need to update key on an overwrite
} END_TYPED_ENUM(maptypeflag);

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

struct hmap {
  isize count; // # live cells == size of map.  Must be first (used by len() builtin)
  hflag flags;
  u8    B;         // log_2 of # of buckets (can hold up to loadFactor * 2^B items)
  u16   noverflow; // approximate number of overflow buckets; see incrnoverflow
  u32   hash0;     // hash seed
  bmap* buckets;   // array of 2^B Buckets. may be nil if count==0.
  bmap* nullable oldbuckets;
    // previous bucket array of half the size, non-nil only when growing
  uintptr nevacuate;
    // progress counter for evacuation (buckets less than this have been evacuated)
  mapextra* nullable extra; // optional fields
  Mem mem; // memory allocator
};

struct rtype { // aka _type
  usize    size;
  tflag    tflag;
  u8       align;
  tkind    kind;
  equalfun equal; // comparing objects of this type
};

// mapextra holds fields that are not present on all maps.
struct mapextra {
  // If both key and elem do not contain pointers and are inline, then we mark bucket
  // type as containing no pointers. This avoids scanning such maps.
  // However, bmap.overflow is a pointer. In order to keep overflow buckets
  // alive, we store pointers to all overflow buckets in hmap.extra.overflow and
  // hmap.extra.oldoverflow.
  // overflow and oldoverflow are only used if key and elem do not contain pointers.
  // overflow contains overflow buckets for hmap.buckets.
  // oldoverflow contains overflow buckets for hmap.oldbuckets.
  // The indirection allows to store a pointer to the slice in hiter.
  PtrArray overflow;    // *[]*bmap
  PtrArray oldoverflow; // *[]*bmap

  // nextOverflow holds a pointer to a free overflow bucket.
  bmap* nextOverflow;
  void* overflow_storage[];
};

struct maptype {
  rtype     typ;
  rtype     key;
  rtype     elem;
  rtype     bucket; // internal type representing a hash bucket
  keyhasher hasher;

  u8  keysize;    // size of key slot
  u8  elemsize;   // size of elem slot

  maptypeflag flags;
};

// store ptr to key instead of key itself
static bool maptype_indirectkey(const maptype* mt) {
  return mt->flags&maptypeIndirectKey;}
// store ptr to elem instead of elem itself
static bool maptype_indirectelem(const maptype* mt) {
  return mt->flags&maptypeIndirectElem;}
// true if k==k for all keys
static bool maptype_reflexivekey(const maptype* mt) {
  return mt->flags&maptypeReflexiveKey;}
// true if we need to update key on an overwrite
static bool maptype_needkeyupdate(const maptype* mt) {
  return mt->flags&maptypeNeedKeyUpdate;}


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
      if (t->size == sizeof(i32))
        return (keyhasher)hash_i32; // from hash.h
      break;
    case tkind_ptr:
    case tkind_float:
    case tkind_struct:
    case tkind_invalid:
      break;
  }
  panic("no hasher for rtype %d", t->kind);
}

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


inline static bmap* nullable bmap_overflow(bmap* b, const maptype* t) {
  return *(bmap**)((uintptr)b + (uintptr)t->bucket.size - PTRSIZE);
}

inline static void bmap_setoverflow(bmap* b, const maptype* t, bmap* ovf) {
  *(bmap**)((uintptr)b + (uintptr)t->bucket.size - PTRSIZE) = ovf;
}

// inline static void* bmap_keys(bmap* b) {
//   return (void*)b + dataOffset;
// }


// hmap_isgrowing reports whether h is growing.
// The growth may be to the same size or bigger.
inline static bool hmap_isgrowing(hmap* h) {
  return h->oldbuckets != NULL;
}

// hmap_sameSizeGrow reports whether the current growth is to a map of the same size
inline static bool hmap_sameSizeGrow(hmap* h) {
  return (h->flags & H_sameSizeGrow) != 0;
}

// hmap_oldbucketcount calculates the number of buckets prior to the current map growth
// aka noldbuckets
static usize hmap_oldbucketcount(hmap* h) {
  u8 B = h->B;
  if (!hmap_sameSizeGrow(h))
    B--;
  return bucketShift(B);
}

// hmap_oldbucketmask provides a mask that can be applied to calculate n % noldbuckets()
inline static usize hmap_oldbucketmask(hmap* h) {
  return hmap_oldbucketcount(h) - 1;
}

// hmap_incrnoverflow increments h.noverflow.
// noverflow counts the number of overflow buckets.
// This is used to trigger same-size map growth.
// See also tooManyOverflowBuckets.
// To keep hmap small, noverflow is a uint16.
// When there are few buckets, noverflow is an exact count.
// When there are many buckets, noverflow is an approximate count.
static void hmap_incrnoverflow(hmap* h) {
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
  if ((fastrand() & mask) == 0)
    h->noverflow++;
}

static bool hmap_createoverflow(hmap* h, u32 lenhint) {
  if (!h->extra) {
    h->extra = memallocztv(h->mem, mapextra, overflow_storage, lenhint);
    if (UNLIKELY( !h->extra ))
      return false;
    if (lenhint > 0)
      PtrArrayInitStorage(&h->extra->overflow, h->extra->overflow_storage, lenhint);
  }
  return true;
}

static bmap* hmap_newoverflow(hmap* h, const maptype* t, bmap* b) {
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
    if (!(ovf = memallocz(h->mem, t->bucket.size)))
      return NULL;
  }
  hmap_incrnoverflow(h);
  if (UNLIKELY( !hmap_createoverflow(h, 1) )) {
    if (ovf != h->extra->nextOverflow)
      memfree(h->mem, ovf);
    return NULL;
  }
  PtrArrayPush(&h->extra->overflow, ovf, h->mem);
  bmap_setoverflow(b, t, ovf);
  return ovf;
}

// make_bucket_array initializes a backing array for map buckets.
// 1<<B is the minimum number of buckets to allocate.
// dirtyalloc should either be nil or a bucket array previously
// allocated by make_bucket_array with the same t and B parameters.
// If dirtyalloc is nil a new backing array will be alloced and
// otherwise dirtyalloc will be cleared and reused as backing array.
static bmap* nullable make_bucket_array(
  Mem mem, const maptype* t, u8 B, void* dirtyalloc, bmap** nextOverflow)
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

  bmap* buckets = memalloczv(mem, t->bucket.size, nbuckets);
  if (UNLIKELY( !buckets )) {
    *nextOverflow = NULL;
    return NULL;
  }
  // dlog("memalloc bmap[%zu] [%p-%p) (%zu * %zu = %zu B)",
  //   nbuckets, buckets, buckets + t->bucket.size*nbuckets,
  //   nbuckets, t->bucket.size, t->bucket.size * nbuckets);
  assertf(dirtyalloc == NULL, "TODO has dirtyalloc");
    // if dirtyalloc != NULL:
    //   dirtyalloc was previously generated by the above
    //   newarray(t.bucket, int(nbuckets)) but may not be empty.

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
static bool is_bucket_evacuated(const maptype* t, hmap* h, usize bucket) {
  bmap* b = (void*)h->oldbuckets + bucket*t->bucket.size;
  return is_evacuated(b);
}


static void advanceEvacuationMark(const maptype* t, hmap* h, usize newbit) {
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
  memfree(h->mem, h->oldbuckets);
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


static void evacuate(const maptype* t, hmap* h, usize oldbucket) {
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
        dst->b = hmap_newoverflow(h, t, dst->b);
        dst->i = 0;
        dst->k = (void*)dst->b + dataOffset;
        dst->e = dst->k + bucketCnt*t->keysize;
      }

      // Note: mask dst->i as an optimization, to avoid a bounds check
      dst->b->tophash[dst->i & (bucketCnt-1)] = top;

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
    advanceEvacuationMark(t, h, newbit);
}


static void growWork(const maptype* t, hmap* h, usize bucket) {
  // make sure we evacuate the oldbucket corresponding
  // to the bucket we're about to use
  evacuate(t, h, bucket & hmap_oldbucketmask(h));

  // evacuate one more oldbucket to make progress on growing
  if (hmap_isgrowing(h))
    evacuate(t, h, h->nevacuate);
}


static void hash_grow(const maptype* t, hmap* h) {
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
  bmap* newbuckets = make_bucket_array(h->mem, t, h->B + bigger, NULL, &nextOverflow);

  hflag flags = h->flags & ~(H_iterator | H_oldIterator);
  if (h->flags & H_iterator)
    flags |= H_oldIterator;

  if (h->oldbuckets != NULL)
    panic("TODO free h->oldbuckets");

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
    hmap_createoverflow(h, 0);
    h->extra->nextOverflow = nextOverflow;
  }
  // the actual copying of the hash table data is done incrementally
  // by growWork() and evacuate().
}


// Like map_access, but allocates a slot for the key if it is not present in the map.
// Returns pointer to value storage.
void* nullable map_assign(const maptype* t, hmap* h, void* key) {
  assertnotnull(h);
  assertf((h->flags & H_hashWriting) == 0, "concurrent map writes");
  h->flags ^= H_hashWriting;

  uintptr hash = t->hasher(key, (uintptr)h->hash0);

  if (h->buckets == NULL) {
    if (!(h->buckets = memallocz(h->mem, t->bucket.size)))
      return NULL;
    // dlog("memalloc bmap[1] [%p-%p) (%zu B)",
    //   h->buckets, h->buckets + t->bucket.size, t->bucket.size);
  }

  usize bucket;

again:
  bucket = hash & bucketMask(h->B);
  if (hmap_isgrowing(h))
    growWork(t, h, bucket);

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
    hash_grow(t, h);
    goto again; // Growing the table invalidates everything, so try again
  }

  if (!inserti) {
    // The current bucket and all the overflow buckets connected to it are full;
    // allocate a new one.
    bmap* newb = hmap_newoverflow(h, t, b);
    inserti = &newb->tophash[0];
    insertk = (void*)newb + dataOffset;
    elem = insertk + bucketCnt*t->keysize;
  }

  // store new key/elem at insert position
  if (maptype_indirectkey(t)) {
    dlog("TODO free kmem storage later");
    void* kmem = memallocz(h->mem, t->key.size);
    *(void**)insertk = kmem;
    insertk = kmem;
  }
  if (maptype_indirectelem(t)) {
    dlog("TODO free vmem storage later");
    void* vmem = memallocz(h->mem, t->elem.size);
    *(void**)elem = vmem;
  }
  assert(insertk != key);
  memmove(insertk, key, t->key.size);
  *inserti = top;
  h->count++;

done:
  assertf(h->flags & H_hashWriting, "concurrent map writes");
  h->flags &= ~H_hashWriting;
  if (maptype_indirectelem(t))
    elem = *(void**)elem;
  return elem;
}


inline static void delete_cleanup(
  const maptype* t, hmap* h, bmap* b, bmap* bOrig, usize i)
{
  if (i == bucketCnt-1) {
    bmap* ovf = bmap_overflow(b, t);
    if (ovf != NULL && ovf->tophash[0] != emptyRest)
      return;
  }
  if (b->tophash[i+1] != emptyRest)
    return;
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


void* nullable map_delete(const maptype* t, hmap* h, void* key) {
  if (h->count == 0)
    return NULL;

  assertf((h->flags & H_hashWriting) == 0, "concurrent map writes");
  h->flags ^= H_hashWriting;

  uintptr hash = t->hasher(key, (uintptr)h->hash0);
  usize bucket = hash & bucketMask(h->B);

  if (hmap_isgrowing(h))
    growWork(t, h, bucket);

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
      if (h->count == 0)
        h->hash0 = fastrand();
      goto end_search;
    }
  }
end_search:

  assertf(h->flags & H_hashWriting, "concurrent map writes");
  h->flags &= ~H_hashWriting;
  return founde;
}



void* nullable map_access(const maptype* t, hmap* nullable h, void* key) {
  if (h == NULL || h->count == 0)
    return NULL;

  assertf((h->flags & H_hashWriting) == 0, "concurrent map read and map write");

  uintptr hash = t->hasher(key, (uintptr)h->hash0);
  usize m = bucketMask(h->B);
  bmap* b = (bmap*)((void*)h->buckets + (hash & m)*t->bucket.size);
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
      if (b->tophash[i] != top) {
        if (b->tophash[i] == emptyRest)
          return NULL;
        continue;
      }
      void* k = (void*)b + dataOffset + i*t->keysize;
      if (maptype_indirectkey(t))
        k = *(void**)k;

      if (t->key.equal(key, k)) {
        void* e = (void*)b + dataOffset + bucketCnt*t->keysize + i*t->elemsize;
        if (maptype_indirectelem(t))
          e = *(void**)e;
        return e;
      }
    }
  }
  return NULL;
}


// map_init_small initializes a caller-managed map when hint is known to be
// at most bucketCnt at compile time.
hmap* map_init_small(hmap* h, Mem mem) {
  assert(h->mem == NULL || h->mem == mem);
  h->mem = mem;
  h->hash0 = fastrand();
  return h;
}


// map_new_small implements map creation when hint is known to be at most bucketCnt
// at compile time and the map needs to be allocated on the heap.
hmap* nullable map_new_small(Mem mem) {
  hmap* h = memalloczt(mem, hmap);
  if (UNLIKELY( h == NULL ))
    return NULL;
  h->flags |= H_hmemManaged;
  return map_init_small(h, mem);
}


// map_make implements map creation for make(map[k]v, hint).
// If the compiler has determined that the map or the first bucket
// can be created on the stack, *hp and/or bucket may be non-null.
// If h != NULL, the map can be created directly in h.
// If h->buckets != NULL, bucket pointed to can be used as the first bucket.
// Return NULL on memory allocation failure or overflow from too large hint.
hmap* nullable map_make(const maptype* t, hmap* nullable h, Mem mem, usize hint) {
  // check if hint is too large
  usize z;
  if (check_mul_overflow(hint, t->bucket.size, &z))
    return NULL;

  if (h == NULL) {
    if ((h = memalloczt(mem, hmap)) == NULL)
      return NULL;
    h->flags |= H_hmemManaged;
  }

  assert(h->mem == NULL || h->mem == mem);
  h->mem = mem;
  h->hash0 = fastrand(); // seed

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
    h->buckets = make_bucket_array(mem, t, B, NULL, &nextOverflow);
    if (UNLIKELY( h->buckets == NULL ))
      goto free_and_fail;
    if (nextOverflow != NULL) {
      if (UNLIKELY( !hmap_createoverflow(h, 0) ))
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


void map_free(const maptype* t, hmap* h) {
  Mem mem = h->mem;
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


// ————————————————————————————————————————————————————————————————————————————————————

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
static maptype mkmaptype(
  const rtype* ktyp, const rtype* vtyp, keyhasher nullable hasher)
{
  maptype mt = {
    .bucket = make_bucket_type(ktyp, vtyp),
    .key = *ktyp,
    .elem = *vtyp,
    .hasher = hasher ? hasher : rtype_hasher(ktyp),
  };
  assertnotnull(ktyp->equal);
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
  if (rtype_isReflexive(ktyp))
    mt.flags |= maptypeReflexiveKey;
  if (rtype_needKeyUpdate(ktyp))
    mt.flags |= maptypeNeedKeyUpdate;
  return mt;
}

// ————————————————————————————————————————————————————————————————————————————————————


// static void noop_free(Mem mem, void** pv, usize count) { dlog("free %p", pv[0]); }
static bool i32_equal(const i32* a, const i32* b) { return *a == *b; }
// static bool f64_equal(const f64* a, const f64* b) { return *a == *b; }

// static const rtype kRType_i32 = {
//   .size = sizeof(i32),
//   .align = sizeof(i32),
//   .kind = tkind_sint,
//   .equal = (equalfun)&i32_equal,
// };

// MAPTYPE defines a maptype at compile time
#define MAPTYPE(kt, kalign, keqf, vt, valign, veqf, hashf, mtflags) {                \
  .bucket = {                                                                        \
    .align = PTRSIZE,                                                                \
    .kind  = tkind_struct,                                                           \
    .size  = MAPTYPE_SIZE(kt, vt),                                                   \
  },                                                                                 \
  .key = {                                                                           \
    .size = sizeof(kt),                                                              \
    .align = kalign,                                                                 \
    .equal = (equalfun)(keqf),                                                       \
  },                                                                                 \
  .elem = {                                                                          \
    .size = sizeof(vt),                                                              \
    .align = valign,                                                                 \
    .equal = (equalfun)(veqf),                                                       \
  },                                                                                 \
  .hasher = (keyhasher)(hashf),                                                      \
  .keysize = sizeof(kt),                                                             \
  .elemsize = sizeof(vt),                                                            \
  .flags = mtflags,                                                                  \
};                                                                                   \
static_assert((MAPTYPE_SIZE(kt, vt) & alignof(kt) - 1) == 0, "not aligned");         \
static_assert((MAPTYPE_SIZE(kt, vt) & alignof(vt) - 1) == 0, "not aligned");         \
static_assert(sizeof(kt) <= maxKeySize, "must use make_ptr_type(ktyp) to calc size");\
static_assert(sizeof(vt) <= maxKeySize, "must use make_ptr_type(vtyp) to calc size")
#define MAPTYPE_SIZE(kt, vt) \
  (bucketCnt*(1 + sizeof(kt) + sizeof(vt)) + PTRSIZE)


const maptype kMapType_i32_i32 =
  MAPTYPE(i32, sizeof(i32), &i32_equal,
          i32, sizeof(i32), &i32_equal,
          &hash_i32, maptypeReflexiveKey);

// const maptype kMapType_f64_i32 =
//   MAPTYPE(f64, sizeof(f64), &f64_equal,
//           i32, sizeof(i32), &i32_equal,
//           &hash_i32/*<-FIXME*/, maptypeNeedKeyUpdate);


DEF_TEST(map) {
  Mem mem = mem_libc_allocator();
  fastrand_seed(1234);

  // maptype mt = mkmaptype(&kRType_i32, &kRType_i32, NULL);
  const maptype* mt = &kMapType_i32_i32;

  #if 0
    // create a map on the heap with a hint
    hmap* h = map_make(mt, NULL, mem, 4);
    assertf(h != NULL, "map_make failed");
  #else
    // initialize a map stored on the stack (with implicit hint <= bucketCnt)
    hmap hstk = { .buckets = memallocz(mem, mt->bucket.size) };
    // hmap hstk = { 0 };
    hmap* h = map_init_small(&hstk, mem);
  #endif

  dlog("map(i32,i32,0) => %p", h);
  i32 insert_count = 20;

  // insert
  for (i32 key = 0; key < insert_count; key++) {
    i32* valp = map_assign(mt, h, &key);
    // dlog("map_assign(%d) => %p", key, valp);
    assertf(valp != NULL, "out of memory");
    *valp = key;
  }

  // lookup
  for (i32 key = 0; key < insert_count; key++) {
    i32* valp = map_access(mt, h, &key);
    // dlog("map_access(%d) => found=%d %d", key, !!valp, (!valp ? 0 : *valp));
    assertnotnull(valp);
    asserteq(key, *valp);
  }

  // replace
  for (i32 key = insert_count/2; key < insert_count; key++) {
    i32* valp = map_assign(mt, h, &key);
    assertf(valp != NULL, "out of memory");
    // NOTE: it is the caller's responsibility to free an existing *valp
    // if it is a pointer to heap memory.
    *valp = key;
  }

  // delete
  for (i32 key = insert_count; key < insert_count; key++) {
    i32* valp = map_delete(mt, h, &key);
    assertf(valp != NULL, "key %d not found/removed", key);
    // NOTE: it is the caller's responsibility to free an existing *valp
    // if it is a pointer to heap memory. In safety-oriented code the caller
    // might also want to memset(valp, 0, sizeof(*valp)).
  }

  map_free(mt, h);

  // exit(0);
}


