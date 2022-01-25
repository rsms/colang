//
// sym is a string type that is interned and can be efficiently compared
// for equality by pointer value. It's used for identifiers.
//
#include "coimpl.h"

// xxhash used for symbol hashing
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define XXH_INLINE_ALL
#include "xxhash.h"
#pragma GCC diagnostic pop

// red-black tree implementation used for SymPool interning
#define RBKEY      Sym
#define RBUSERDATA Mem
#define RBNODETYPE SymRBNode
#include "rbtree.h"

// SymMap implementation
#define HASHMAP_IMPLEMENTATION
#define HASHMAP_NAME     SymMap
#define HASHMAP_KEY      Sym
#define HASHMAP_KEY_HASH symhash
#define HASHMAP_VALUE    void*
#include "hashmap.h"
#undef HASHMAP_NAME
#undef HASHMAP_KEY
#undef HASHMAP_KEY_HASH
#undef HASHMAP_VALUE
#undef HASHMAP_IMPLEMENTATION


// sym_xxhash32_seed is the xxHash seed used for hashing sym data
static const u32 sym_xxhash32_seed = 578;

#define HASH_SYM_DATA(data, len) XXH32((const void*)(data), (len), sym_xxhash32_seed)

inline static SymRBNode* RBAllocNode(Mem mem) {
  return memalloct(mem, SymRBNode);
}

// syms are never removed from the interning tree
inline static void RBFreeNode(SymRBNode* node, Mem mem) {
  memfree(mem, (void*)_SYM_HEADER(node->key));
  memfree(mem, node);
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
  const SymRBNode* node, const char* data, u32 len, u32 hash)
{
  while (node) {
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
  SymHeader* hp = (SymHeader*)memalloc(p->mem, sizeof(SymHeader) + (usize)len + 1);
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
    memfree(p->mem, hp);
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
