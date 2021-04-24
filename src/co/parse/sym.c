//
// sym is a string type that is interned and can be efficiently compared
// for equality by pointer value. It's used for identifiers.
//
#include <rbase/rbase.h>
#include "sym.h"
#include "../util/array.h"

#define XXH_INLINE_ALL
#include <xxhash/xxhash.h>

// red-black tree implementation used for interning
#define RBKEY      Sym
#define RBUSERDATA Mem
#include "../util/rbtree.c.h"

static_assert(sizeof(SymRBNode) == sizeof(RBNode), "");

inline static RBNode* RBAllocNode(Mem mem) {
  return (RBNode*)memalloct(mem, RBNode);
}

// syms are never removed from the interning tree
inline static void RBFreeNode(RBNode* node, Mem mem) {
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


void sympool_init(SymPool* p, SymPool* base, Mem mem, SymRBNode* root) {
  p->root = root;
  p->base = base;
  p->mem = mem;
  rwmtx_init(&p->mu, mtx_plain);
}

void sympool_dispose(SymPool* p) {
  if (p->root)
    RBClear((RBNode*)p->root, p->mem);
  rwmtx_destroy(&p->mu);
}


// Caller must hold read lock on rp->mu
inline static Sym symlookup(const RBNode* node, const char* data, size_t len, u32 hash) {
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


Sym symaddh(SymPool* p, const char* data, size_t len, u32 hash) {
  assert(len <= 0xFFFFFFFF);

  // allocate a new Sym
  auto hp = (SymHeader*)memalloc_raw(p->mem, sizeof(SymHeader) + (size_t)len + 1);
  hp->hash = hash;
  hp->sh.len = SYM_MAKELEN(len, /*flags*/ 0);
  hp->sh.cap = len;
  auto sp = &hp->sh.p[0];
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
  p->root = (SymRBNode*)RBInsert((RBNode*)p->root, s, &added, p->mem);
  if (!added)
    s2 = symlookup((RBNode*)p->root, data, len, hash);
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


Sym symgeth(SymPool* p, const char* data, size_t len, u32 hash) {
  SymPool* rp = p;
  while (rp) {
    rwmtx_rlock(&rp->mu);
    auto s = symlookup((RBNode*)rp->root, data, len, hash);
    rwmtx_runlock(&rp->mu);
    if (s)
      return s;
    // look in base pool
    rp = rp->base;
  }
  // not found; add
  return symaddh(p, data, len, hash);
}


Sym symget(SymPool* p, const char* data, size_t len) {
  XXH32_hash_t h = XXH32((const char*)data, len, sym_xxhash32_seed);
  return symgeth(p, data, len, h);
}

Sym symadd(SymPool* p, const char* data, size_t len) {
  XXH32_hash_t h = XXH32((const char*)data, len, sym_xxhash32_seed);
  return symaddh(p, data, len, h);
}

static bool sym_rb_iter1(const RBNode* n, void* userdata) {
  auto sp = (Str*)userdata;
  *sp = str_append(*sp, n->key);
  *sp = str_appendcstr(*sp, ", ");
  return true; // keep going
}

Str sympool_repr_unsorted(const SymPool* p, Str s) {
  u32 len1 = str_len(s);
  RBIter((const RBNode*)p->root, sym_rb_iter1, &s);
  if (str_len(s) != len1)
    str_setlen(s, str_len(s) - 2); // undo last ", "
  return s;
}

static bool sym_rb_iter(const RBNode* n, void* userdata) {
  auto a = (Array*)userdata;
  ArrayPush(a, (void*)n->key, NULL);
  return true; // keep going
}

static int str_sortf(ConstStr a, ConstStr b, void* userdata) {
  return a == b ? 0 : strcmp(a, b);
}

Str sympool_repr(const SymPool* p, Str s) {
  Array a;
  void* astorage[64];
  ArrayInitWithStorage(&a, astorage, countof(astorage));
  RBIter((const RBNode*)p->root, sym_rb_iter, &a);
  ArraySort(&a, (ArraySortFun)str_sortf, NULL);
  bool first = true;
  s = str_appendc(s, '{');
  ArrayForEach(&a, Sym, sym) {
    if (first) {
      first = false;
      s = str_appendc(s, '"');
    } else {
      s = str_appendcstr(s, ", \"");
    }
    s = str_appendrepr(s, sym, symlen(sym));
    // s = str_appendn(s, sym, symlen(sym));
    s = str_appendc(s, '"');
  }
  s = str_appendc(s, '}');
  ArrayFree(&a, NULL);
  return s;
}


// ----------------------------------------------------------------------------
// unit tests

R_UNIT_TEST(sym, {
  SymPool syms;
  sympool_init(&syms, NULL, NULL, NULL);

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
})

// inline static Str rbkeyfmt(Str s, RBKEY k) {
//   return str_appendfmt(s, "Sym(\"%s\" %x)", k, symhash(k));
// }

R_UNIT_TEST(sym_base_pool, {;
  SymPool syms1;
  SymPool syms2;
  SymPool syms3;
  sympool_init(&syms1, NULL, NULL, NULL);
  sympool_init(&syms2, &syms1, NULL, NULL);
  sympool_init(&syms3, &syms2, NULL, NULL);

  auto A1 = symaddh(&syms1, "A", 1, 1);
  symaddh(&syms1, "B", 1, 2);
  symaddh(&syms1, "C", 1, 3);

  auto B2 = symaddh(&syms2, "B", 1, 2);
  symaddh(&syms2, "C", 1, 3);

  auto C3 = symaddh(&syms3, "C", 1, 3);

  // auto s = str_new(0);
  // s = RBRepr((const RBNode*)syms1.root, s, 0, rbkeyfmt);
  // dlog("syms1: %s", s);
  // s = RBRepr((const RBNode*)syms2.root, str_setlen(s, 0), 0, rbkeyfmt);
  // dlog("syms2: %s", s);
  // s = RBRepr((const RBNode*)syms3.root, str_setlen(s, 0), 0, rbkeyfmt);
  // dlog("syms3: %s", s);
  // str_free(s);

  asserteq(C3, symgeth(&syms3, "C", 1, 3)); // found in syms3
  asserteq(B2, symgeth(&syms3, "B", 1, 2)); // not found in syms3, but found in syms2
  asserteq(A1, symgeth(&syms3, "A", 1, 1)); // not found in syms3 or syms2, but found in syms1

  sympool_dispose(&syms1);
  sympool_dispose(&syms2);
  sympool_dispose(&syms3);
})
