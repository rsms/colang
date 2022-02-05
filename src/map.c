#include "coimpl.h"
#include "mem.h"
#include "test.h"
#include "hash.h"
#include "array.h"

#ifdef CO_WITH_LIBC
  #include <stdlib.h>
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define XXH_INLINE_ALL
#include "xxhash.h"
#pragma GCC diagnostic pop

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
typedef uintptr(*keyhasher)(const void*, uintptr);

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
// Each bucket (including its overflow buckets, if any) will have either all or none of its
// entries in the evacuated* states (except during the evacuate() method, which only happens
// during map writes and thus no one else can observe the map during that time).
enum tophash_flag {
  emptyRest,
    // this cell is empty, and there are no more non-empty cells at higher indexes
    // or overflows
  emptyOne,       // this cell is empty
  evacuatedX,     // key/elem is valid. Entry has been evacuated to first half of larger tbl
  evacuatedY,     // same as above, but evacuated to second half of larger table
  evacuatedEmpty, // cell is empty, bucket is evacuated
  minTopHash,     // minimum tophash for a normal filled cell
};

typedef u8 hflag;
enum hflag {
  iterator     = 1 << 0, // there may be an iterator using buckets
  oldIterator  = 1 << 1, // there may be an iterator using oldbuckets
  hashWriting  = 1 << 2, // a goroutine is writing to the map
  sameSizeGrow = 1 << 3, // the current map growth is to a new map of the same size
  memManaged   = 1 << 4, // memory for hmap should be freed by mapfree
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
  u16   noverflow; // approximate number of overflow buckets; see incrnoverflow for details
  u32   hash0;     // hash seed

  bmap*          buckets; // array of 2^B Buckets. may be nil if count==0.
  bmap* nullable oldbuckets;
    // previous bucket array of half the size, non-nil only when growing
  uintptr        nevacuate;
    // progress counter for evacuation (buckets less than this have been evacuated)

  mapextra* nullable extra; // optional fields
};

struct rtype { // aka _type
  usize   size;
  // uintptr ptrdata; // size of memory prefix holding all pointers (GC)
  // u32     hash;
  tflag   tflag;
  u8      align;
  //u8      fieldAlign;
  tkind   kind;
  // function for comparing objects of this type
  bool(*equal)(const void*, const void*);
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
  u16 bucketsize; // size of bucket (TODO: get rid of this; use bucket.size instead)

  maptypeflag flags;
};

// store ptr to key instead of key itself
static bool maptype_indirectkey(const maptype* mt) {return mt->flags&maptypeIndirectKey;}
// store ptr to elem instead of elem itself
static bool maptype_indirectelem(const maptype* mt) {return mt->flags&maptypeIndirectElem;}
// true if k==k for all keys
static bool maptype_reflexivekey(const maptype* mt) {return mt->flags&maptypeReflexiveKey;}
// true if we need to update key on an overwrite
static bool maptype_needkeyupdate(const maptype* mt) {return mt->flags&maptypeNeedKeyUpdate;}


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
      return false;
    case tkind_invalid:
      break;
  }
  panic("non-key type %p", t);
}

static uintptr i32_hasher(const void* keyp, uintptr seed) {
  return (uintptr)XXH32(keyp, sizeof(i32), seed);
}

// rtype_isReflexive reports whether the == operation on the type is reflexive.
// That is, x == x for all values x of type t.
static keyhasher rtype_hasher(const rtype* t) {
  switch ((enum tkind)t->kind) {
    case tkind_sint:
    case tkind_uint:
      if (t->size == sizeof(i32))
        return i32_hasher;
      break;
    case tkind_ptr:
    case tkind_float:
    case tkind_struct:
    case tkind_invalid:
      break;
  }
  panic("no hasher for rtype %d", t->kind);
}


// isEmpty reports whether the given tophash array entry represents an empty bucket entry.
static bool isEmpty(u8 x) {
  return x <= emptyOne; }

// bucketShift returns 1<<b, optimized for code generation.
static usize bucketShift(u8 b) {
  // Masking the shift amount allows overflow checks to be elided.
  return (usize)1 << (b & (PTRSIZE*8 - 1));
}

// bucketMask returns 1<<b - 1
static usize bucketMask(u8 b) {
  return bucketShift(b) - 1; }

// tophash calculates the tophash value for hash.
static u8 tophash(uintptr hash) {
  u8 top = (u8)(hash >> (PTRSIZE*8 - 8));
  if (top < minTopHash)
    top += minTopHash;
  return top;
}

bool evacuated(bmap* b) {
  u8 h = b->tophash[0];
  return h > emptyOne && h < minTopHash;
}


// is_over_loadFactor reports whether count items placed in 1<<B buckets is over loadFactor
// aka overLoadFactor
static bool is_over_loadFactor(usize count, u8 B) {
  return count > bucketCnt && count > loadFactorNum * (bucketShift(B) / loadFactorDen);
}

// is_tooManyOverflowBuckets reports whether noverflow buckets is too many for a map with
// 1<<B buckets. Note that most of these overflow buckets must be in sparse use;
// if use was dense, then we'd have already triggered regular map growth.
static bool is_tooManyOverflowBuckets(u16 noverflow, u8 B) {
  // If the threshold is too low, we do extraneous work.
  // If the threshold is too high, maps that grow and shrink can hold on to lots of
  // unused memory. "too many" means (approximately) as many overflow buckets as regular
  // buckets. See incrnoverflow for more details.
  if (B > 15)
    B = 15;
  // The compiler doesn't see here that B < 16; mask B to generate shorter shift code.
  return noverflow >= (u16)1 << (B & 15);
}


static bmap* bmap_overflow(bmap* b, maptype* t) {
  return *(bmap**)((uintptr)b + (uintptr)t->bucketsize - PTRSIZE);
}

static void bmap_setoverflow(bmap* b, maptype* t, bmap* ovf) {
  *(bmap**)((uintptr)b + (uintptr)t->bucketsize - PTRSIZE) = ovf;
}

void* bmap_keys(bmap* b) {
  return (void*)b + dataOffset;
}


// hmap_isgrowing reports whether h is growing. The growth may be to the same size or bigger.
bool hmap_isgrowing(hmap* h) {
  return h->oldbuckets != NULL;
}

// hmap_sameSizeGrow reports whether the current growth is to a map of the same size
static bool hmap_sameSizeGrow(hmap* h) {
  return (h->flags & sameSizeGrow) != 0;
}

// hmap_oldbucketcount calculates the number of buckets prior to the current map growth
static usize hmap_oldbucketcount(hmap* h) {
  u8 B = h->B;
  if (!hmap_sameSizeGrow(h))
    B--;
  return bucketShift(B);
}

// hmap_oldbucketmask provides a mask that can be applied to calculate n % noldbuckets()
static usize hmap_oldbucketmask(hmap* h) {
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

static bool hmap_createoverflow(Mem mem, hmap* h, u32 lenhint) {
  if (h->extra)
    return true;
  h->extra = memallocztv(mem, mapextra, overflow_storage, lenhint);
  if (UNLIKELY( !h->extra ))
    return false;
  PtrArrayInitStorage(&h->extra->overflow, h->extra->overflow_storage, lenhint);
  return true;
}

static bmap* hmap_newoverflow(Mem mem, hmap* h, maptype* t, bmap* b) {
  bmap* ovf;
  if (h->extra != NULL && h->extra->nextOverflow != NULL) {
    // We have preallocated overflow buckets available.
    // See make_bucket_array for more details.
    ovf = h->extra->nextOverflow;
    if (bmap_overflow(ovf, t) == NULL) {
      // We're not at the end of the preallocated overflow buckets. Bump the pointer.
      h->extra->nextOverflow = (bmap*)((void*)ovf + t->bucketsize);
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
  if (UNLIKELY( !hmap_createoverflow(mem, h, 1) )) {
    if (ovf != h->extra->nextOverflow)
      memfree(mem, ovf);
    return NULL;
  }
  PtrArrayPush(&h->extra->overflow, ovf, mem);
  bmap_setoverflow(b, t, ovf);
  return ovf;
}


static void growWork(maptype* t, hmap* h, usize bucket) {
  dlog("TODO");
  // // make sure we evacuate the oldbucket corresponding
  // // to the bucket we're about to use
  // evacuate(t, h, bucket & hmap_oldbucketmask(h));

  // // evacuate one more oldbucket to make progress on growing
  // if (hmap_isgrowing(h))
  //   evacuate(t, h, h.nevacuate);
}

static void typed_memmove(const rtype* typ, void* dst, void* src) {
  if (dst != src)
    memmove(dst, src, typ->size);
}

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

// make_bucket_array initializes a backing array for map buckets.
// 1<<B is the minimum number of buckets to allocate.
// dirtyalloc should either be nil or a bucket array previously
// allocated by make_bucket_array with the same t and B parameters.
// If dirtyalloc is nil a new backing array will be alloced and
// otherwise dirtyalloc will be cleared and reused as backing array.
bmap* nullable make_bucket_array(
  Mem mem, maptype* t, u8 B, void* dirtyalloc, bmap** nextOverflow)
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
  dlog("memalloc bmap[%zu] [%p-%p) (%zu * %zu = %zu B)",
    nbuckets, buckets, buckets + t->bucket.size*nbuckets,
    nbuckets, t->bucket.size, t->bucket.size * nbuckets);
  assertf(dirtyalloc==NULL, "TODO has dirtyalloc");
    // if dirtyalloc != NULL:
    //   dirtyalloc was previously generated by the above newarray(t.bucket, int(nbuckets))
    //   but may not be empty.

  if (base != nbuckets) {
    // We preallocated some overflow buckets.
    // To keep the overhead of tracking these overflow buckets to a minimum,
    // we use the convention that if a preallocated overflow bucket's overflow
    // pointer is nil, then there are more available by bumping the pointer.
    // We need a safe non-nil pointer for the last overflow bucket; just use buckets.
    *nextOverflow = (bmap*)((void*)buckets + base*t->bucketsize);
    bmap* last = (bmap*)((void*)buckets + (nbuckets - 1)*t->bucketsize);
    bmap_setoverflow(last, t, buckets);
  } else {
    *nextOverflow = NULL;
  }

  return buckets;
}


void hash_grow(Mem mem, maptype* t, hmap* h) {
  panic("TODO");
}


// Like mapaccess, but allocates a slot for the key if it is not present in the map.
// Returns pointer to value storage.
void* nullable mapassign(Mem mem, maptype* t, hmap* h, void* key) {
  if (h->flags & hashWriting)
    panic("concurrent map writes");

  uintptr hash = t->hasher(key, (uintptr)h->hash0);
  dlog("hash(key) => %lx", hash);

  h->flags ^= hashWriting;

  if (h->buckets == NULL) {
    if (!(h->buckets = memallocz(mem, t->bucket.size)))
      return NULL;
    dlog("memalloc bmap[1] [%p-%p) (%zu B)",
      h->buckets, h->buckets + t->bucket.size, t->bucket.size);
  }

  usize bucket;

again:
  bucket = hash & bucketMask(h->B);
  if (hmap_isgrowing(h))
    growWork(t, h, bucket);

  bmap* b = (bmap*)((void*)h->buckets + bucket*t->bucketsize);
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
      if (!t->key.equal(key, k)) {
        continue; // for1
      }
      // already have a mapping for key. Update it.
      dlog("update mapping (TODO free/deref existing value via callback)");
      if (maptype_needkeyupdate(t))
        typed_memmove(&t->key, k, key);
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
    hash_grow(mem, t, h);
    goto again; // Growing the table invalidates everything, so try again
  }

  if (!inserti) {
    // The current bucket and all the overflow buckets connected to it are full;
    // allocate a new one.
    bmap* newb = hmap_newoverflow(mem, h, t, b);
    inserti = &newb->tophash[0];
    insertk = (void*)newb + dataOffset;
    elem = insertk + bucketCnt*t->keysize;
  }

  // store new key/elem at insert position
  if (maptype_indirectkey(t)) {
    void* kmem = memallocz(mem, t->key.size);
    *(void**)insertk = kmem;
    insertk = kmem;
  }
  if (maptype_indirectelem(t)) {
    void* vmem = memallocz(mem, t->elem.size);
    *(void**)elem = vmem;
  }
  typed_memmove(&t->key, insertk, key);
  *inserti = top;
  h->count++;

done:
  dlog("done");
  if ((h->flags & hashWriting) == 0)
    panic("concurrent map writes");
  h->flags &= ~hashWriting;
  if (maptype_indirectelem(t))
    elem = *(void**)elem;
  return elem;
}



static void* nullable mapaccess(maptype* t, hmap* nullable h, void* key) {
  if (h == NULL || h->count == 0)
    return NULL;
  if (h->flags & hashWriting)
    panic("concurrent map read and map write");

  uintptr hash = t->hasher(key, (uintptr)h->hash0);
  usize m = bucketMask(h->B);
  bmap* b = (bmap*)((void*)h->buckets + (hash & m)*t->bucketsize);
  bmap* c = h->oldbuckets;
  if (c != NULL) {
    if (!hmap_sameSizeGrow(h)) {
      // There used to be half as many buckets; mask down one more power of two.
      m >>= 1;
    }
    bmap* oldb = (bmap*)((void*)c + (hash & m)*t->bucketsize);
    if (!evacuated(oldb))
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



// makemap implements map creation for make(map[k]v, hint).
// If the compiler has determined that the map or the first bucket
// can be created on the stack, *hp and/or bucket may be non-null.
// If h != NULL, the map can be created directly in h.
// If h->buckets != NULL, bucket pointed to can be used as the first bucket.
// Upon successful return, the resulting map can be found at h.
static hmap* makemap(hmap* nullable h, Mem mem, maptype* t, usize hint) {
  // check if hint is too large
  usize z;
  if (check_mul_overflow(hint, t->bucket.size, &z))
    return NULL;

  if (h == NULL) {
    if ((h = memalloczt(mem, hmap)) == NULL)
      return NULL;
    dlog("memalloc hmap [%p-%p) (%zu B)", h, h+sizeof(*h), sizeof(*h));
    h->flags |= memManaged;
  }

  h->hash0 = fastrand(); // seed

  // Find the size parameter B which will hold the requested # of elements.
  // For hint < 0 is_over_loadFactor returns false since hint < bucketCnt.
  u8 B = 0;
  while (is_over_loadFactor(hint, B))
    B++;
  h->B = B;

  // if B == 0, the buckets field is allocated lazily later (in mapassign)
  if (B != 0) {
    // allocate initial hash table
    // If hint is large zeroing this memory could take a while.
    bmap* nextOverflow = NULL;
    if (!(h->buckets = make_bucket_array(mem, t, B, NULL, &nextOverflow))) {
      if (h->flags & memManaged)
        memfree(mem, h);
      return NULL;
    }
    assertf(nextOverflow != NULL, "TODO h->extra.nextOverflow = nextOverflow");
    // if (nextOverflow) {
    //   h->extra = new(mapextra);
    //   h->extra.nextOverflow = nextOverflow;
    // }
  }

  return h;
}

static void mapfree(Mem mem, hmap* h, maptype* t) {
  if (h->flags & memManaged)
    memfree(mem, h);
}


// ————————————————————————————————————————————————————————————————————————————————————


static maptype mkmaptype(const rtype* ktyp, const rtype* vtyp, keyhasher nullable hasher) {
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
  mt.bucketsize = (u16)mt.bucket.size;
  if (rtype_isReflexive(ktyp))
    mt.flags |= maptypeReflexiveKey;
  if (rtype_needKeyUpdate(ktyp))
    mt.flags |= maptypeNeedKeyUpdate;
  return mt;
}


static bool i32_equal(const void* p1, const void* p2) {
  return *((i32*)p1) == *((i32*)p2);
}

static const rtype kRType_i32 = {
  .size = sizeof(i32),
  .align = sizeof(i32),
  .kind = tkind_sint,
  .equal = &i32_equal,
};

static maptype gMaptype_i32_i32 = {0};

typedef struct MapI32 MapI32; // i32 => i32
struct MapI32 {
  Mem      mem;
  maptype* t;
  hmap     h;
};

static MapI32* MapI32Make(MapI32* nullable m, Mem mem, usize hint) {
  if (gMaptype_i32_i32.hasher == NULL)
    gMaptype_i32_i32 = mkmaptype(&kRType_i32, &kRType_i32, NULL);
  if (m == NULL) {
    if ((m = memalloczt(mem, MapI32)) == NULL)
      return NULL;
    m->h.flags |= memManaged;
  }
  m->mem = mem;
  m->t = &gMaptype_i32_i32;
  hmap* hp = makemap(&m->h, mem, m->t, hint);
  if (!hp) {
    if (m->h.flags & memManaged)
      memfree(m->mem, &m->h);
    return NULL;
  }
  asserteq(hp, &m->h);
  return m;
}

static void MapI32Free(MapI32* m) {
  if (m->h.flags & memManaged) {
    m->h.flags &= ~memManaged; // clear so that mapfree doesn't do the same
    mapfree(m->mem, &m->h, m->t);
    memfree(m->mem, m);
  }
}

static bool MapI32Set(MapI32* m, i32 key, i32 val) {
  i32* valp = (i32*)mapassign(m->mem, m->t, &m->h, &key);
  if (valp == NULL)
    return false;
  *valp = val;
  return true;
}

static i32* MapI32Lookup(MapI32* m, i32 key) {
  return mapaccess(m->t, &m->h, &key);
}

static i32 MapI32Get(MapI32* m, i32 key, i32 defaultval) {
  i32* valp = MapI32Lookup(m, key);
  if (!valp)
    return defaultval;
  return *valp;
}


DEF_TEST(map) {
  dlog("bucketCnt  %zu", bucketCnt);
  dlog("dataOffset %zu", dataOffset);
  Mem mem = mem_libc_allocator();
  fastrand_seed(1234);

  MapI32* m = MapI32Make(NULL, mem, 0);
  assertnotnull(m);
  dlog("MapI32Make(0) => %p", m);
  assertf(MapI32Set(m, 123, 456), "out of memory");
  asserteq(MapI32Get(m, 123, -1), 456);

  maptype mt = mkmaptype(&kRType_i32, &kRType_i32, NULL);

  hmap* h = makemap(NULL, mem, &mt, 0);
  assertf(h != NULL, "makemap failed");
  dlog("makemap({i32,i32},0) => %p", h);

  i32 key = 1;
  i32* valp = (i32*)mapassign(mem, &mt, h, &key);
  assertf(valp != NULL, "out of memory");
  *valp = 345;
  dlog("mapassign(%d) => %p", key, valp);

  valp = mapaccess(&mt, h, &key);
  dlog("mapaccess(%d) => found=%d %d", key, !!valp, (!valp ? 0 : *valp));
  assertnotnull(valp);
  asserteq(345, *valp);

  mapfree(mem, h, &mt);

  // exit(0);
}


