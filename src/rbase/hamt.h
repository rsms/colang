#pragma once
//
// This implements an immutable persistent Hash Array Mapped Trie (HAMT) data structure.
// It's inspired by Clojure's data structures and is efficient in both time and space.
//
ASSUME_NONNULL_BEGIN

// HAMT_BRANCHES defines the width of the trie. Must be one of 8, 16, 32, 64
#ifndef HAMT_BRANCHES
  #define HAMT_BRANCHES 32
#endif
static_assert(HAMT_BRANCHES==8||HAMT_BRANCHES==16||HAMT_BRANCHES==32||HAMT_BRANCHES==64,"");

// Hamt constants
//   HAMT_BITS     5  = 6 | 5 | 4 | 3
//   HAMT_BRANCHES 32 = 1 << HAMT_BITS    ; 2^6=64, 2^5=32, 2^4=16, 2^3=8
//   HAMT_MASK     31 = HAMT_BRANCHES - 1 ; 63, 31 (0x1f), 15, 7, 3, 1
//   HAMT_MAXDEPTH 7  = ((1 << HAMT_BITS) / HAMT_BITS) + 1
// HamtUInt is the integer type used for node keys and bitmaps in trie nodes
#if HAMT_BRANCHES   == 8
  #define HAMT_BITS     3
  #define HAMT_MASK     7
  #define HAMT_MAXDEPTH 3
  typedef u8 HamtUInt;
#elif HAMT_BRANCHES == 16
  #define HAMT_BITS     4
  #define HAMT_MASK     15
  #define HAMT_MAXDEPTH 5
  typedef u16 HamtUInt;
#elif HAMT_BRANCHES == 32
  #define HAMT_BITS     5
  #define HAMT_MASK     31
  #define HAMT_MAXDEPTH 7
  typedef u32 HamtUInt;
#else
  #define HAMT_BITS     6
  #define HAMT_MASK     63
  #define HAMT_MAXDEPTH 11
  typedef u64 HamtUInt;
#endif

// memory allocator
#ifndef HAMT_MEMALLOC
  #define HAMT_MEMALLOC(nbyte) memalloc_raw(NULL, (nbyte))
  #define HAMT_MEMFREE(ptr)    memfree(NULL, (ptr));
#endif

// forward declaration
typedef struct HamtCtx HamtCtx;
typedef struct HamtNode HamtNode;

// Hamt is an immutable persistent collection
typedef struct Hamt {
  HamtNode* root;
  HamtCtx*  ctx;
} Hamt;

// HamtNode is either a HAMT, value or a collision (set of values with same key/hash)
typedef struct HamtNode {
  atomic_u32 refs;      // reference count
  u32        tag;       // bit 0-2 = type, bit 3-31 = len
  HamtUInt   bmap;      // used for key when type==TValue
  HamtNode*  entries[]; // THamt:[THamt|TCollision|TValue], TCollision:[TValue], TValue:void*
} HamtNode;

// HamtCtx provides callbacks for the type of entries stored in a Hamt.
//
// To store arbitrary information alongside a Hamt, make your own struct e.g.
//   struct MyCtx { HamtCtx head; int mydata; };
//   hamt_new((HamtCtx*)&myctx);
// You can now access mydata in your callback functions by casting the ctx argument, e.g.
//   void myentfree(HamtCtx* ctx, void* entry) {
//     struct MyCtx* my = (struct MyCtx*)ctx;
//     ...
//   }
typedef struct HamtCtx {
  // entkey should return an entry's unique key. For example a hash or index.
  // The returned value is used to place and order the entry in the HAMT. Ideally all
  // possible distinct entries have unique keys. In some situations this might be impossible,
  // for instance with arbitrary string keys. In the case of distinct entries with identical
  // keys, collision sets are used and the enteq function is used to tell them apart.
  // The result of calling this function is cached for entries in a Hamt; this function
  // is thus only called once per lookup, insertion or removal operation.
  // This callback is required and must not be NULL.
  HamtUInt(*entkey)(HamtCtx* ctx, const void* entry);

  // enteq should return true if a and b are considered equivalent.
  // This function is only called for entries with identical key() values.
  // This callback is required and must not be NULL.
  bool(*enteq)(HamtCtx* ctx, const void* entry1, const void* entry2);

  // entfree is called when an entry is no longer referenced by a Hamt.
  // This callback is required and must not be NULL.
  void(*entfree)(HamtCtx* ctx, void* entry);

  // entrepr is called by hamt_repr and is expected to append a human-readable
  // representation of an entry to s.
  // This callback is optional and may be NULL.
  Str(*entrepr)(HamtCtx* ctx, Str s, const void* entry);
} HamtCtx;


// hamt_new creates an empty Hamt with a +1 reference count.
// Hamt handles are reference counted and need to be maintained with calls to
// hamt_retain and hamt_release as logical references are introduced and dropped.
// Thus, there's no hamt_free function; instead, call hamt_release when you are done.
Hamt hamt_new(HamtCtx* ctx);

// hamt_retain increments the reference count of a Hamt handle.
// Returns h as a convenience.
static Hamt hamt_retain(Hamt h);

// hamt_release decrements the reference count of a Hamt handle.
static void hamt_release(Hamt h);

// hamt_empty returns true when there are no entries in h
static bool hamt_empty(Hamt h);

// hamt_count returns the total number of entries stored in h.
//
// This HAMT implementation does not maintain a counter of total number of entries stored
// but instead traverses the internal trie to tally the total count. This is usually very
// fast as each branch knows the number of entries it holds. If you require reading count at
// a high frequency and low latency, you could keep track of the count yourself when calling
// modifying functions.
size_t hamt_count(Hamt h);


// hamt_with returns a version of h with entry added. Steals a ref to entry.
// *didadd is set to true if h grew by adding entry, false if an equivalent entry was replaced.
// Returns a new Hamt with a +1 reference count.
Hamt hamt_with(Hamt h, void* entry, bool* didadd);

// hamt_set is a convenience form of hamt_with that does the following:
// 1. h2 = hamt_with(*h, entry)
// 2. hamt_release(*h)
// 3. *h = h2
// Returns true if h grew by adding entry, false if an equivalent entry was replaced.
// Steals a ref to entry.
bool hamt_set(Hamt* h, void* entry);


// hamt_without returns a version of h without an entry matching *refentry.
// *removed is set to true if a matching entry is found and excluded, otherwise false.
static Hamt hamt_without(Hamt h, const void* refentry, bool* removed);

// hamt_withoutk is a variant of hamt_without where the lookup key is provided separately
// instead of being looked up via entkey. Note that refentry is still tested via enteq.
Hamt hamt_withoutk(Hamt h, const void* refentry, HamtUInt key, bool* removed);

// hamt_del is a convenience function with behavior similar to hamt_without but instead
// of returning a new version of h, it replaces h. It really just saves you from calling
// hamt_release after calling hamt_without.
// Returns true if an entry was removed.
static bool hamt_del(Hamt* h, const void* refentry);

// hamt_delk is a variant of hamt_del where the lookup key is provided separately
// instead of being looked up via entkey. Note that refentry is still tested via enteq.
bool hamt_delk(Hamt* h, const void* refentry, HamtUInt key);


// hamt_get performs a lookup of a entry equivalent to reference entry (value of *entry).
// If an entry is found, its value is stored to *entry and true is returned.
// The input value of *entry is used as the "lookup" value; it is queried for its key
// with entkey and may be compared via enteq with entries that has identical keys.
static bool hamt_get(Hamt h, const void** entry);

// hamt_getk is a variant of hamt_get where the lookup key is provided separately
// instead of being looked up via entkey. Note that the reference entry (input value of entry)
// is still tested via enteq.
bool hamt_getk(Hamt h, const void** entry, HamtUInt key);

// hamt_getp is a convenience variant of hamt_get which returns NULL on failure.
// Only suitable when the entry type is guaranteed never to be zero.
static const void* nullable hamt_getp(Hamt h, const void* refentry);


// HamtIter is an iterator's state
typedef struct HamtIter {
  void* n;                     // current node
  u32   i;                     // current n->entries[i]
  u32   nstacklen;             // number of entries in nstack
  u32   istacklen;             // number of entries in istack
  void* nstack[HAMT_MAXDEPTH]; // stack of nodes
  u32   istack[HAMT_MAXDEPTH]; // stack of indices
} HamtIter;

// hamt_iter_init initializes an iterator to the beginning of h
void hamt_iter_init(Hamt h, HamtIter* it);

// hamt_iter_next advances the iterator to the next entry.
// Returns true when an entry was stored to *entry.
// Returns false when there are no more entries. entry is left unchanged in this case.
bool hamt_iter_next(HamtIter* it, const void** entry);


// map_repr appends a human-readable string representation of h to s.
// If h.ctx->entrepr is non-NULL, it will be used to format entries in the output.
// If pretty is true, a multi-line tree structure is produced. Otherwise, a compact
// S-expression output is produced.
Str hamt_repr(Hamt h, Str s, bool pretty);


// -----------------------------------------------------------------------------------------------
// implementation

void _hamt_free(Hamt);

inline static Hamt hamt_retain(Hamt h) {
  AtomicAdd(&h.root->refs, 1);
  return h;
}

inline static void hamt_release(Hamt h) {
  if (AtomicSub((atomic_u32*)h.root, 1) == 1)
    _hamt_free(h);
}

inline static bool hamt_empty(Hamt h) {
  return h.root->tag == 0;
}

inline static const void* hamt_getp(Hamt h, const void* entry) {
  if (!hamt_get(h, &entry))
    return NULL;
  return entry;
}

inline static bool hamt_get(Hamt h, const void** entry) {
  return hamt_getk(h, entry, h.ctx->entkey(h.ctx, *entry));
}

inline static bool hamt_del(Hamt* h, const void* refentry) {
  return hamt_delk(h, refentry, h->ctx->entkey(h->ctx, refentry));
}

inline static Hamt hamt_without(Hamt h, const void* refentry, bool* removed) {
  return hamt_withoutk(h, refentry, h.ctx->entkey(h.ctx, refentry), removed);
}

ASSUME_NONNULL_END
