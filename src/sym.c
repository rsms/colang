// Sym -- immutable interned strings
// SPDX-License-Identifier: Apache-2.0
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define SYM_IMPLEMENTATION
#endif
#include "mem.c"
#include "map.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

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

// SymPool holds a set of syms unique to the pool
typedef struct SymPool SymPool;

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


// symmap -- maps Sym => void* (veneer on pmap)

typedef HMap SymMap;

inline static SymMap* nullable symmap_init(SymMap* h, Mem mem, usize hint) {
  return (SymMap*)pmap_init(h, mem, hint, MAPLF_2);
}
inline static void** nullable symmap_assign(SymMap* h, Sym key) {
  return (void**)pmap_assign(h, key);
}
inline static void** nullable symmap_find(const SymMap* h, Sym key) {
  return (void**)pmap_find(h, key);
}
inline static void symmap_free(SymMap* h) {
  hmap_dispose(h);
}

//———————————————————————————————————————————————————————————————————————————————————————
// internal

typedef struct SymRBNode {
  Sym                 key;
  bool                isred;
  SymRBNode* nullable left;
  SymRBNode* nullable right;
} SymRBNode;

typedef struct SymPool {
  SymRBNode* nullable     root;
  const SymPool* nullable base;
  Mem                     mem;
  //rwmtx_t                 mu; // TODO
} SymPool;

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

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef SYM_IMPLEMENTATION
#include "hash.c"

// red-black tree implementation used for SymPool interning
#define RBKEY      Sym
#define RBUSERDATA Mem
#define RBNODETYPE SymRBNode
#include "rbtree.h"

// Sym hashing
//   SYM_HASH_SEED is the xxHash seed used for hashing sym data
//   u32 HASH_SYM_DATA(const void* p, u32 len) computes a hash of a symbol's name
// If you change these, you have to re-run the universe generator.
#define SYM_HASH_SEED 578
#define HASH_SYM_DATA(data, len)  ((u32)hash_mem(data, (usize)len, SYM_HASH_SEED))


inline static SymRBNode* RBAllocNode(Mem mem) {
  return mem_alloct(mem, SymRBNode);
}

// syms are never removed from the interning tree,
// but other sympool instances might, like those used for testing.
inline static void RBFreeNode(SymRBNode* node, Mem mem) {
  SymHeader* hp = (SymHeader*)_SYM_HEADER(node->key);
  mem_free(mem, hp, sizeof(SymHeader) + (usize)hp->len + 1);
  mem_free(mem, node, sizeof(SymRBNode));
}

static int RBCmp(Sym a, Sym b, Mem mem) {
  if (symhash(a) < symhash(b))
    return -1;
  if (symhash(a) > symhash(b))
    return 1;
  int cmp = (int)symlen(a) - (int)symlen(b);
  if (cmp == 0) {
    // hash is identical and length is identical; compare bytes
    cmp = memcmp(a, b, symlen(a));
  }
  return cmp;
}


void sympool_init(SymPool* p, const SymPool* base, Mem mem, SymRBNode* root) {
  p->root = root;
  p->base = base;
  p->mem = mem;
  //rwmtx_init(&p->mu, mtx_plain);
}

void sympool_dispose(SymPool* p) {
  if (p->root)
    RBClear(p->root, p->mem);
  //rwmtx_destroy(&p->mu);
}


// static bool debug_0x11_iter(const SymRBNode* n, void* userdata) {
//   dlog("  node key %p (%s)", n->key, (uintptr_t)n->key > 0xff ? n->key : "");
//   return true; // keep going
// }

// static void debug_0x11_dump(const SymPool* p) {
//   dlog("sympool:");
//   if (p->root) {
//     RBIter(p->root, debug_0x11_iter, NULL);
//   } else {
//     dlog("null root");
//   }
// }


// Caller must hold read lock on rp->mu
inline static Sym nullable symlookup(
  const SymRBNode* nullable node, const char* data, u32 len, u32 hash)
{
  while (node != NULL) {
    // IMPORTANT: The comparison here MUST match the comparison used for other
    // operations on the tree. I.e. it must match RBCmp.
    Sym b = (RBKEY)node->key;
    if (hash < symhash(b)) {
      node = node->left;
    } else if (hash > symhash(b)) {
      node = node->right;
    } else {
      int cmp = (int)len - (int)symlen(b);
      if (cmp == 0) {
        cmp = memcmp(data, b, len);
        if (cmp == 0)
          return b;
      }
      node = cmp < 0 ? node->left : node->right;
    }
  }
  return NULL;
}


static Sym symaddh(SymPool* p, const char* data, u32 len, u32 hash) {
  assert(len <= 0xFFFFFFFF);

  // allocate a new Sym
  SymHeader* hp = (SymHeader*)mem_alloc(p->mem, sizeof(SymHeader) + (usize)len + 1);
  hp->hash = hash;
  hp->len = SYM_MAKELEN(len, /*flags*/ 0);
  char* sp = &hp->p[0];
  memcpy(sp, data, len);
  sp[len] = 0;
  Sym s = (Sym)sp;

  // It's possible that an equivalent symbol is already in the tree.
  // Either the caller made the wrong assumption thinking the symbol did not exist,
  // or another thread raced us and managed to get a write lock just before we did
  // and added the same symbol. Either way, we store the success result of RBInsert
  // at `added` which we later check.
  bool added;
  Sym s2 = NULL;

  // attempt to insert into tree
  //rwmtx_lock(&p->mu);
  p->root = RBInsert(p->root, s, &added, p->mem);
  if (!added)
    s2 = symlookup(p->root, data, len, hash);
  //rwmtx_unlock(&p->mu);

  if (!added) {
    // Another thread managed to insert the same symbol before we did.
    // Free the symbol we allocated
    mem_free(p->mem, hp, sizeof(SymHeader) + (usize)hp->len + 1);
    // return the equivalent symbol
    assert(s2 != NULL);
    s = s2;
  }

  return s;
}


Sym nullable symfind(SymPool* p, const char* data, u32 len) {
  u32 hash = HASH_SYM_DATA(data, len);
  const SymPool* rp = p;
  while (rp) {
    //rwmtx_rlock((rwmtx_t*)&rp->mu);
    Sym s = symlookup(rp->root, data, len, hash);
    //rwmtx_runlock((rwmtx_t*)&rp->mu);
    if (s)
      return s;
    // look in base pool
    rp = rp->base;
  }
  return NULL;
}


Sym symget(SymPool* p, const char* data, u32 len) {
  // symget is a hot path with the majority of calls ending up with a successful lookup
  u32 hash = HASH_SYM_DATA(data, len);
  const SymPool* rp = p;
  while (rp) {
    //rwmtx_rlock((rwmtx_t*)&rp->mu);
    Sym s = symlookup(rp->root, data, len, hash);
    //rwmtx_runlock((rwmtx_t*)&rp->mu);
    if (s)
      return s;
    // look in base pool
    rp = rp->base;
  }
  // not found; add
  return symaddh(p, data, len, hash);
}

Sym symadd(SymPool* p, const char* data, u32 len) {
  u32 h = HASH_SYM_DATA(data, len);
  return symaddh(p, data, len, h);
}


// sympool_repr
#if 0
static bool sym_rb_iter1(const SymRBNode* n, void* userdata) {
  Str* sp = (Str*)userdata;
  *sp = str_append(*sp, n->key, symlen(n->key));
  *sp = str_appendcstr(*sp, ", ");
  return true; // keep going
}

Str sympool_repr_unsorted(const SymPool* p, Str s) {
  u32 len1 = str_len(s);
  RBIter(p->root, sym_rb_iter1, &s);
  if (str_len(s) != len1)
    str_setlen(s, str_len(s) - 2); // undo last ", "
  return s;
}

typedef struct ReprCtx {
  Mem   mem;
  Array a;
  void* astorage[64];
} ReprCtx;

static bool sym_rb_iter(const SymRBNode* n, void* userdata) {
  ReprCtx* rctx = (ReprCtx*)userdata;
  ArrayPush(&rctx->a, (void*)n->key, rctx->mem);
  return true; // keep going
}

static int str_sortf(ConstStr a, ConstStr b, void* userdata) {
  return a == b ? 0 : strcmp(a, b);
}

Str sympool_repr(const SymPool* p, Str s) {
  ReprCtx rctx;
  rctx.mem = p->mem;
  ArrayInitWithStorage(&rctx.a, rctx.astorage, countof(rctx.astorage));
  RBIter(p->root, sym_rb_iter, &rctx);
  ArraySort(&rctx.a, (ArraySortFun)str_sortf, NULL);
  bool first = true;
  s = str_appendc(s, '{');
  for (u32 i = 0; i < rctx.a.len; i++) {
    Sym sym = rctx.a.v[i];
    if (first) {
      first = false;
      s = str_appendc(s, '"');
    } else {
      s = str_appendcstr(s, ", \"");
    }
    s = str_appendrepr(s, sym, symlen(sym));
    // s = str_append(s, sym, symlen(sym));
    s = str_appendc(s, '"');
  }
  if (p->base) {
    s = str_appendcstr(s, ", [base]: ");
    s = sympool_repr(p->base, s);
  }
  s = str_appendc(s, '}');
  ArrayFree(&rctx.a, rctx.mem);
  return s;
}
#endif

#endif // SYM_IMPLEMENTATION
