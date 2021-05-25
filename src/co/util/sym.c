//
// sym is a string type that is interned and can be efficiently compared
// for equality by pointer value. It's used for identifiers.
//
#include "../common.h"
#include "sym.h"
#include "array.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define XXH_INLINE_ALL
#include <xxhash/xxhash.h>
#pragma GCC diagnostic pop

// red-black tree implementation used for interning
#define RBKEY      Sym
#define RBUSERDATA Mem
#define RBNODETYPE SymRBNode
#include "rbtree.c.h"

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
  rwmtx_init(&p->mu, mtx_plain);
}

void sympool_dispose(SymPool* p) {
  if (p->root)
    RBClear(p->root, p->mem);
  rwmtx_destroy(&p->mu);
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
  const SymRBNode* node, const char* data, size_t len, u32 hash)
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


static Sym symaddh(SymPool* p, const char* data, size_t len, u32 hash) {
  assert(len <= 0xFFFFFFFF);

  // allocate a new Sym
  auto hp = (SymHeader*)memalloc(p->mem, sizeof(SymHeader) + (size_t)len + 1);
  hp->hash = hash;
  hp->len = SYM_MAKELEN(len, /*flags*/ 0);
  auto sp = &hp->p[0];
  memcpy(sp, data, len);
  sp[len] = 0;
  auto s = (Sym)sp;

  // It's possible that an equivalent symbol is already in the tree.
  // Either the caller made the wrong assumption thinking the symbol did not exist,
  // or another thread raced us and managed to get a write lock just before we did
  // and added the same symbol. Either way, we store the success result of RBInsert
  // at `added` which we later check.
  bool added;
  Sym s2 = NULL;

  // attempt to insert into tree
  rwmtx_lock(&p->mu);
  p->root = RBInsert(p->root, s, &added, p->mem);
  if (!added)
    s2 = symlookup(p->root, data, len, hash);
  rwmtx_unlock(&p->mu);

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


Sym nullable symfind(SymPool* p, const char* data, size_t len) {
  u32 hash = HASH_SYM_DATA(data, len);
  const SymPool* rp = p;
  while (rp) {
    rwmtx_rlock((rwmtx_t*)&rp->mu);
    auto s = symlookup(rp->root, data, len, hash);
    rwmtx_runlock((rwmtx_t*)&rp->mu);
    if (s)
      return s;
    // look in base pool
    rp = rp->base;
  }
  return NULL;
}


Sym symget(SymPool* p, const char* data, size_t len) {
  // symget is a hot path with the majority of calls ending up with a successful lookup
  u32 hash = HASH_SYM_DATA(data, len);
  const SymPool* rp = p;
  while (rp) {
    rwmtx_rlock((rwmtx_t*)&rp->mu);
    auto s = symlookup(rp->root, data, len, hash);
    rwmtx_runlock((rwmtx_t*)&rp->mu);
    if (s)
      return s;
    // look in base pool
    rp = rp->base;
  }
  // not found; add
  return symaddh(p, data, len, hash);
}

Sym symadd(SymPool* p, const char* data, size_t len) {
  u32 h = HASH_SYM_DATA(data, len);
  return symaddh(p, data, len, h);
}

static bool sym_rb_iter1(const SymRBNode* n, void* userdata) {
  auto sp = (Str*)userdata;
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
  auto rctx = (ReprCtx*)userdata;
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


// ----------------------------------------------------------------------------
// unit tests

R_TEST(sym) {
  auto mem = MemLinearAlloc();
  SymPool syms;
  sympool_init(&syms, NULL, mem, NULL);

  asserteq(SYM_MAKELEN(5, 0), 5);

  // make sure sym interning works as expected
  const char* a = "break";
  char* b = memstrdup(syms.mem, "break");
  assertop(a, != ,b);

  auto sym_a = symgetcstr(&syms, a);
  asserteq(symlen(sym_a), strlen(a));

  auto sym_b = symgetcstr(&syms, b);
  asserteq(symlen(sym_b), strlen(b));

  asserteq(sym_a, sym_b);
  memfree(syms.mem, b);

  // symadd
  auto s1 = symaddcstr(&syms, "sea");
  auto s2 = symaddcstr(&syms, "sea");
  asserteq(s1, s2);

  // repr
  auto s = sympool_repr(&syms, str_new(0));
  int cmp = strcmp(s, "{\"break\", \"sea\"}");
  if (cmp != 0)
    errlog("sympool_repr => %s", s);
  asserteq(cmp, 0);
  str_free(s);

  sympool_dispose(&syms);
  MemLinearFree(mem);
}


R_TEST(symflags) {
  auto mem = MemLinearAlloc();
  SymPool syms;
  sympool_init(&syms, NULL, mem, NULL);
  auto s = symgetcstr(&syms, "hello");
  u32 msglen = strlen("hello");
  for (u32 i = 0; i <= SYM_FLAGS_MAX; i++) {
    sym_dangerously_set_flags(s, i);
    asserteq(symflags(s), i);    // we should be able to read the flag value
    asserteq(symlen(s), msglen); // len should still be accurate
  }
  sympool_dispose(&syms);
  MemLinearFree(mem);
}


R_TEST(sym_hash) {
  const char* buffer = "hello";
  size_t size = strlen(buffer);

  // oneshot
  XXH32_hash_t hash1 = XXH32(buffer, size, sym_xxhash32_seed);
  // dlog("hash1 oneshot: %x", hash1);

  { // state approach piece by piece
    XXH32_state_t* hstate = XXH32_createState();
    XXH32_reset(hstate, sym_xxhash32_seed);
    size_t len1 = size / 2;
    size_t len2 = size - len1;
    XXH32_update(hstate, buffer, len1);
    XXH32_update(hstate, &buffer[len1], len2);
    XXH32_hash_t hash2 = XXH32_digest(hstate);
    // dlog("hash32 state:  %x", hash2);
    asserteq(hash2, hash1);
    XXH32_freeState(hstate);
  }
}

__attribute__((used))
inline static Str rbkeyfmt(Str s, RBKEY k) {
  return str_appendfmt(s, "Sym(\"%s\" %x)", k, symhash(k));
}

R_TEST(sympool) {
  auto mem = MemLinearAlloc();
  SymPool syms1;
  SymPool syms2;
  SymPool syms3;
  sympool_init(&syms1, NULL, mem, NULL);
  sympool_init(&syms2, &syms1, mem, NULL);
  sympool_init(&syms3, &syms2, mem, NULL);

  auto A1 = symadd(&syms1, "A", 1);
  symadd(&syms1, "B", 1);
  symadd(&syms1, "C", 1);

  auto B2 = symadd(&syms2, "B", 1);
  symadd(&syms2, "C", 1);

  auto C3 = symadd(&syms3, "C", 1);

  // auto s = str_new(0);
  // s = RBRepr(syms1.root, s, 0, rbkeyfmt);
  // dlog("syms1: %s", s);
  // s = RBRepr(syms2.root, str_setlen(s, 0), 0, rbkeyfmt);
  // dlog("syms2: %s", s);
  // s = RBRepr(syms3.root, str_setlen(s, 0), 0, rbkeyfmt);
  // dlog("syms3: %s", s);
  // str_free(s);

  asserteq(C3, symget(&syms3, "C", 1)); // found in syms3
  asserteq(B2, symget(&syms3, "B", 1)); // not found in syms3, but found in syms2
  asserteq(A1, symget(&syms3, "A", 1)); // not found in syms3 or syms2, but found in syms1

  sympool_dispose(&syms1);
  sympool_dispose(&syms2);
  sympool_dispose(&syms3);
  MemLinearFree(mem);
}
