// Sym -- immutable interned strings
#pragma once
#include "mem.h"
#include "map.h"
ASSUME_NONNULL_BEGIN

// Sym is a string type that is interned and can be efficiently compared
// for equality by pointer value. It's used for identifiers.
// Sym is immutable with an embedded precomputed hash, interned in a SymPool.
// Sym is a valid null-terminated C-string.
// Sym can be compared for equality simply by comparing pointer address.
// Sym functions are tuned toward lookup rather than insertion or deletion.
typedef const char* Sym;

// SYM_FLAGS_MAX defines the largest possible flags value
#define SYM_FLAGS_MAX 31

// SYM_LEN_MAX defines the largest possible length of a symbol
#define SYM_LEN_MAX 0x7ffffff /* 0-134217727 (27-bit integer) */

// SymRBNode is a red-black tree node
typedef struct SymRBNode SymRBNode;
typedef struct SymRBNode {
  Sym                 key;
  bool                isred;
  SymRBNode* nullable left;
  SymRBNode* nullable right;
} SymRBNode;

// SymPool holds a set of syms unique to the pool
typedef struct SymPool SymPool;
typedef struct SymPool {
  SymRBNode* nullable     root;
  const SymPool* nullable base;
  Mem                     mem;
  //rwmtx_t                 mu; // TODO MT
} SymPool;

// sympool_init initialized a SymPool
// base is an optional "parent" or "outer" read-only symbol pool to use for secondary lookups
//   when a symbol is not found in the pool.
// mem is the memory to use for SymRBNodes.
// root may be a preallocated red-black tree. Be mindful of interactions with sympool_dispose.
void sympool_init(SymPool*, const SymPool* nullable base, Mem, SymRBNode* nullable root);

// sympool_dispose frees up memory used by p (but does not free p itself)
// When a SymPool has been disposed, all symbols in it becomes invalid.
void sympool_dispose(SymPool* p);

// // sympool_repr appends a printable list representation of the symbols in p to s
// Str sympool_repr(const SymPool* p, Str s);

// symget "interns" a Sym in p.
// All symget functions are thread safe.
Sym symget(SymPool* p, const char* data, u32 len);

// symgetcstr is a convenience around symget for C-strings (calls strlen for you.)
static Sym symgetcstr(SymPool* p, const char* cstr);

// symfind looks up a symbol but does not add it if missing
Sym nullable symfind(SymPool* p, const char* data, u32 len);

// symadd adds a symbol to p unless it already exists in p in which case the existing
// symbol is returned.
//
// A difference between symget vs symadd is what happens when a base pool is used:
// In the case of symget the entire base pool chain is traversed looking for the symbol
// and only if that fails is a new symbol added to p.
// However with symadd p's base is not searched and a new symbol is added to p regardless
// if it exists in base pools. Additionally, the implementation of this function assumes
// that the common case is that there's no symbol for data.
Sym symadd(SymPool*, const char* data, u32 len);

// symaddcstr is a convenience around symadd for C-strings (calls strlen for you.)
static Sym symaddcstr(SymPool*, const char* cstr);

// symcmp compares two Sym's string values, like memcmp.
// Note: to check equality of syms, simply compare their addresses (e.g. a==b)
static int symcmp(Sym a, Sym b);

// symhash returns the symbol's precomputed hash
static u32 symhash(Sym);

// symlen returns a symbols precomputed string length
static u32 symlen(Sym);

// symflags returns a symbols flags.
// (currently only used for built-in keywords defined in universe)
static u8 symflags(Sym);


// symmap -- hash map that maps Sym => void*

#define kSymMapType (&kMapType_ptr_ptr)

inline static HMap* nullable symmap_make(HMap* nullable h, Mem mem, usize hint) {
  return map_make(kSymMapType, h, mem, hint);
}
inline static void** nullable symmap_assign(HMap* h, Sym key, Mem mem) {
  return map_assign(kSymMapType, h, &key, mem);
}
inline static void** nullable symmap_access(const HMap* nullable h, Sym key) {
  return map_access(kSymMapType, h, &key);
}
inline static void symmap_free(HMap* h, Mem mem) {
  map_free(kSymMapType, h, mem);
}


// ---- Sym inline implementation ----

typedef struct __attribute__((__packed__)) SymHeader {
  u32  hash;
  u32  len; // _SYM_FLAG_BITS bits flags, rest is number of bytes at p
  char p[];
} SymHeader;

#define _SYM_HEADER(s) ((const SymHeader*)((s) - (sizeof(SymHeader))))

// these work for little endian only. Sym implementation relies on LE to be able to simply
// increment the length value.
#if defined(__ARMEB__) || defined(__ppc__) || defined(__powerpc__)
#error "big-endian arch not supported"
#endif
#define _SYM_FLAG_BITS 5
// _sym_flag_mask  0b11110000...0000
// _sym_len_mask   0b00001111...1111
static const u32 _sym_flag_mask = U32_MAX ^ (U32_MAX >> _SYM_FLAG_BITS);
static const u32 _sym_len_mask  = U32_MAX ^ _sym_flag_mask;

inline static Sym symgetcstr(SymPool* p, const char* cstr) {
  return symget(p, cstr, strlen(cstr));
}

inline static Sym symaddcstr(SymPool* p, const char* cstr) {
  return symadd(p, cstr, strlen(cstr));
}

inline static int symcmp(Sym a, Sym b) { return a == b ? 0 : strcmp(a, b); }
inline static u32 symhash(Sym s) { return _SYM_HEADER(s)->hash; }
inline static u32 symlen(Sym s) { return _SYM_HEADER(s)->len & _sym_len_mask; }

inline static u8 symflags(Sym s) {
  return (_SYM_HEADER(s)->len & _sym_flag_mask) >> (32 - _SYM_FLAG_BITS);
}

// SYM_MAKELEN(u32 len, u8 flags) is a helper macro for making the "length" portion of
// the SymHeader, useful when creating Syms at compile time.
#define SYM_MAKELEN(len, flags) \
  ( ( ((u32)(flags) << (32 - _SYM_FLAG_BITS)) & _sym_flag_mask ) | ((len) & _sym_len_mask) )

// sym_dangerously_set_flags mutates a Sym by setting its flags.
// Use with caution as Syms are assumed to be constant and immutable.
inline static void sym_dangerously_set_flags(Sym s, u8 flags) {
  assert(flags <= SYM_FLAGS_MAX);
  SymHeader* h = (SymHeader*)_SYM_HEADER(s);
  h->len = SYM_MAKELEN(h->len, flags);
}

// sym_dangerously_set_len mutates a Sym by setting its length.
// Use with caution as Syms are assumed to be constant and immutable.
inline static void sym_dangerously_set_len(Sym s, u32 len) {
  assert(len <= symlen(s)); // can only shrink
  SymHeader* h = (SymHeader*)_SYM_HEADER(s);
  h->len = (h->len & _sym_flag_mask) | len;
  h->p[h->len] = 0;
}


// ======================================================================================
// SymMap -- hash map that maps Sym => pointer (old)
#if 0

ASSUME_NONNULL_END
#define HASHMAP_NAME  SymMap
#define HASHMAP_KEY   Sym
#define HASHMAP_VALUE void*
#include "hashmap.h"
#undef HASHMAP_NAME
#undef HASHMAP_KEY
#undef HASHMAP_VALUE
ASSUME_NONNULL_BEGIN

// SymMapInit initializes a map structure with user-provided initial bucket storage.
// initbucketsc*sizeof(SymMapBucket) bytes must be available at initbucketsv and
// initbucketsv must immediately follow in memory, i.e. at m+sizeof(SymMap).
//
// Example:
//   struct foo {
//     SymMap       m;
//     SymMapBucket mbv[8];
//   };
//   stuct foo f = {0};
//   SymMapInit(&f.m, f.mbv, countof(f.mbv), mem);
//
void SymMapInit(SymMap*, void* initbucketsv, u32 initbucketsc, Mem);

// Creates and initializes a new SymMap in mem.
SymMap* nullable SymMapNew(Mem mem, u32 initbuckets);

// SymMapFree frees a SymMap created from SymMapNew
void SymMapFree(SymMap* m);

// SymMapDispose frees SymMap memory (does not free m.)
void SymMapDispose(SymMap* m);

// SymMapLen returns the number of entries currently in the map
static u32 SymMapLen(const SymMap*);

// SymMapGet searches for key. Returns value, or NULL if not found.
void* nullable SymMapGet(const SymMap*, Sym key);

// SymMapSet inserts key=value into m.
// On return, sets *valuep_inout to a replaced value or NULL if no existing value was found.
// Returns an error if memory allocation failed during growth of the hash table.
error SymMapSet(SymMap*, Sym key, void** valuep_inout);

// SymMapDel removes value for key. Returns the removed value or NULL if not found.
void* nullable SymMapDel(SymMap*, Sym key);

// SymMapClear removes all entries. In contrast to SymMapFree, map remains valid.
void SymMapClear(SymMap*);

// Iterator function type. Set stop=true to stop iteration.
typedef void(*SymMapIterator)(Sym key, void* value, bool* stop, void* nullable userdata);

// SymMapIter iterates over entries of the map.
void SymMapIter(const SymMap*, SymMapIterator, void* nullable userdata);

#endif

ASSUME_NONNULL_END
