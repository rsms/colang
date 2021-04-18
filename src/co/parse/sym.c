//
// sym is a string type that is interned and can be efficiently compared
// for equality by pointer value. It's used for identifiers.
//
#include <rbase/rbase.h>
#include "sym.h"

// red-black tree implementation used for interning
#define RBKEY      Sym
#define RBUSERDATA Mem
#include "../util/rbtree.c.h"

inline static RBNode* RBAllocNode(Mem mem) {
  return (RBNode*)memalloct(mem, RBNode);
}

// syms are never removed from the interning tree
inline static void RBFreeNode(RBNode* node, Mem mem) {
  assert(!"sym deallocated");
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


static RBNode* symRoot = NULL; // FIXME symdefs override


inline static Sym symnew(u32 hash, const u8* ptr, u32 len) {
  auto hp = (SymHeader*)memalloc_raw(NULL, sizeof(SymHeader) + (size_t)len + 1);
  hp->hash = hash;
  hp->sh.len = len;
  hp->sh.cap = len;
  auto s = &hp->sh.p[0];
  memcpy(s, ptr, len);
  s[len] = 0;
  return (Sym)s;
}


inline static Sym symfind(const RBNode* node, u32 hash, const u8* aptr, size_t alen) {
  while (node) {
    // IMPORTANT: The comparison here MUST match the comparison used for other
    // operations on the tree. I.e. it must match RBCmp.
    // int cmp = RBCmp(key, (RBKEY)node->key);
    Sym b = (RBKEY)node->key;
    if (hash < symhash(b)) {
      node = node->left;
    } else if (hash > symhash(b)) {
      node = node->right;
    } else {
      int cmp = (int)alen - (int)symlen(b);
      if (cmp == 0) {
        cmp = memcmp(aptr, b, alen);
        if (cmp == 0)
          return b;
      }
      node = cmp < 0 ? node->left : node->right;
    }
  }
  return NULL;
}


Sym symget(/*SymPool* p,*/ const u8* data, size_t len, u32 hash) {
  assert(len <= 0xFFFFFFFF);
  // RBNode* root = (RBNode*)p->p;
  RBNode* root = symRoot;
  auto s = symfind(root, hash, data, len);
  if (s == NULL) {
    // intern miss
    s = symnew(hash, data, len);
    // p->p = RBInsert(root, s, NULL);
    symRoot = RBInsert(root, s, NULL);
  }
  return s;
}


Sym symgeth(/*SymPool* p,*/ const u8* data, size_t len) {
  return symget(/*p,*/ data, len, hash_fnv1a32((const u8*)data, len));
}
