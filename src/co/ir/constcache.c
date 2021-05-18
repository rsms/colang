#include "../common.h"
#include "ir.h"

#define RBKEY      u64
#define RBKEY_NULL 0
#define RBVALUE    void*
#define RBUSERDATA Mem
#include "../util/rbtree.c.h"

// TODO: Consider a HAMT structure instead of a red-black tree as it would be more compact
// in memory and faster for 64-bit int keys that are sequential (essentially a sparse array.)

// ———————————————————————————————————————————————————————————————————————————————————————————————
// RB API

inline static RBNode* RBAllocNode(Mem mem) {
  return (RBNode*)memalloc(mem, sizeof(RBNode));
}

inline static void RBFreeNode(RBNode* node, Mem mem) {
  memfree(mem, node);
}

inline static void RBFreeValue(void* value, Mem mem) {
  // Called when a value is removed or replaced.
  memfree(mem, value);
}

inline static int RBCmp(RBKEY a, RBKEY b, Mem mem) {
  if (a < b) { return -1; }
  if (b < a) { return 1; }
  return 0;
}

// ———————————————————————————————————————————————————————————————————————————————————————————————
#ifdef _DOCUMENTATION_ONLY_
// RBHas performs a lookup of k. Returns true if found.
bool RBHas(const RBNode* n, RBKEY k, Mem mem);

// RBGet performs a lookup of k. Returns value or RBVALUE_NOT_FOUND.
RBVALUE RBGet(const RBNode* n, RBKEY k, Mem mem);

RBNode* RBGetNode(RBNode* node, RBKEY key, Mem mem);

// RBSet adds or replaces value for k. Returns new n.
RBNode* RBSet(RBNode* n, RBKEY k, RBVALUE v, Mem mem);

// RBSet adds value for k if it does not exist. Returns new n.
// "added" is set to true when a new value was added, false otherwise.
RBNode* RBAdd(RBNode* n, RBKEY k, RBVALUE v, bool* added, Mem mem);

// RBDelete removes k if found. Returns new n.
RBNode* RBDelete(RBNode* n, RBKEY k, Mem mem);

// RBClear removes all entries. n is invalid after this operation.
void RBClear(RBNode* n, Mem mem);

// Iteration. Return true from callback to keep going.
typedef bool(RBIterator)(const RBNode* n, Mem mem);
bool RBIter(const RBNode* n, RBIterator* f, Mem mem);

// RBCount returns the number of entries starting at n. O(n) time complexity.
size_t RBCount(const RBNode* n);

// RBRepr formats n as printable lisp text, useful for inspecting a tree.
// keystr should produce a string representation of a given key.
Str RBRepr(const RBNode* n, Str s, int depth, Str(keyfmt)(Str,RBKEY));
#endif
// ———————————————————————————————————————————————————————————————————————————————————————————————
//
// const cache is structured in two levels, like this:
//
// type -> RBNode { value -> IRValue }
//
/*

typedef struct IRConstCache {
  u32   bmap;       // maps TypeCode => branch array index
  void* branches[]; // dense branch array
} IRConstCache;
*/


// number of entries in c->entries
inline static u32 branchesLen(const IRConstCache* c) {
  return (u32)popcount(c->bmap);
}

// bitindex ... [TODO doc]
inline static u32 bitindex(u32 bmap, u32 bitpos) {
  return (u32)popcount(bmap & (bitpos - 1));
}


inline static IRConstCache* IRConstCacheAlloc(Mem mem, size_t entryCount) {
  return (IRConstCache*)memalloc(mem, sizeof(IRConstCache) + (entryCount * sizeof(void*)));
}

// static const char* fmtbin(u32 n) {
//   static char buf[33];
//   u32 i = 0;
//   while (n) {
//     buf[i++] = n & 1 ? '1' : '0';
//     n >>= 1;
//   }
//   buf[i] = '\0';
//   return buf;
// }


IRValue* nullable IRConstCacheGet(
  const IRConstCache* c,
  Mem                 mem,
  TypeCode            t,
  u64                 value,
  int*                out_addHint
) {
  if (c != NULL) {
    u32 bitpos = 1 << t;
    if ((c->bmap & bitpos) != 0) {
      u32 bi = bitindex(c->bmap, bitpos); // index in c->buckets
      auto branch = (RBNode*)c->branches[bi];
      assert(branch != NULL);
      *out_addHint = (int)(bi + 1);
      return (IRValue*)RBGet(branch, value, mem);
    }
  }
  *out_addHint = 0;
  return NULL;
}

IRConstCache* IRConstCacheAdd(
  IRConstCache* c,
  Mem           mem,
  TypeCode      t,
  u64           value,
  IRValue*      v,
  int           addHint
) {
  // Invariant: t>=0 and t<32
  // Note: TypeCode_NUM_END is static_assert to be <= 32

  // dlog("IRConstCacheAdd type=%c (%u) value=0x%lX", TypeCodeEncoding(t), t, value);

  const u32 bitpos = 1 << t;

  if (c == NULL) {
    // first type tree
    // dlog("case A -- initial branch");
    c = IRConstCacheAlloc(mem, 1);
    c->bmap = bitpos;
    c->branches[0] = RBSet(NULL, value, v, mem);
  } else {
    // if addHint is not NULL, it is the branch index+1 of the type branch
    if (addHint > 0) {
      u32 bi = (u32)(addHint - 1);
      auto branch = (RBNode*)c->branches[bi];
      c->branches[bi] = RBSet(branch, value, v, mem);
      return c;
    }
    u32 bi = bitindex(c->bmap, bitpos); // index in c->buckets
    if ((c->bmap & bitpos) == 0) {
      // dlog("case B -- new branch");
      // no type tree -- copy c into a +1 sized memory slot
      auto nbranches = branchesLen(c);
      auto c2 = IRConstCacheAlloc(mem, nbranches + 1);
      c2->bmap = c->bmap | bitpos;
      auto dst = &c2->branches[0];
      auto src = &c->branches[0];
      // copy entries up until bi
      memcpy(dst, src, bi * sizeof(void*));
      // add bi
      dst[bi] = RBSet(NULL, value, v, mem);
      // copy entries after bi
      memcpy(dst + (bi + 1), src + bi, (nbranches - bi) * sizeof(void*));
      // Note: Mem is forward only so no free(c) here
      c = c2;
    } else {
      // dlog("case C -- existing branch");
      auto branch = (RBNode*)c->branches[bi];
      c->branches[bi] = RBSet(branch, value, v, mem);
    }
  }

  return c;
}


// ——————————————————————————————————————————————————————————————————————————————————————————————

R_TEST(constcache) {
  // printf("--------------------------------------------------\n");
  auto mem = MemArenaAlloc();

  IRConstCache* c = NULL;
  u64 testValueGen = 1; // IRValue pointer simulator (generator)

  // c is null; get => null
  int addHint = 0;
  auto v1 = IRConstCacheGet(c, mem, TypeCode_int8, 1, &addHint);
  assert(v1 == NULL);

  auto expect1 = testValueGen++;
  auto expect2 = testValueGen++;
  auto expect3 = testValueGen++;

  // add values. This data causes all cases of the IRConstCacheAdd function to be used.
  // 1. initial branch creation, when c is null
  c = IRConstCacheAdd(c, mem, TypeCode_int8,  1, (IRValue*)expect1, 0);
  // 2. new branch on existing c
  c = IRConstCacheAdd(c, mem, TypeCode_int16, 1, (IRValue*)expect2, 0);
  // 3. new value on existing branch
  c = IRConstCacheAdd(c, mem, TypeCode_int16, 2, (IRValue*)expect3, 0);

  // verify that Get returns the expected values
  v1 = IRConstCacheGet(c, mem, TypeCode_int8, 1, &addHint);
  assert((u64)v1 == expect1);
  auto v2 = IRConstCacheGet(c, mem, TypeCode_int16, 1, &addHint);
  assert((u64)v2 == expect2);
  auto v3 = IRConstCacheGet(c, mem, TypeCode_int16, 2, &addHint);
  assert((u64)v3 == expect3);

  // test the addHint, which is an RBNode of the type branch when it exists.
  addHint = 0;
  auto expect4 = testValueGen++;
  auto v4 = IRConstCacheGet(c, mem, TypeCode_int16, 3, &addHint);
  assert(v4 == NULL);
  assert(addHint != 0); // since TypeCode_int16 branch should exist
  c = IRConstCacheAdd(c, mem, TypeCode_int16, 3, (IRValue*)expect4, addHint);
  v4 = IRConstCacheGet(c, mem, TypeCode_int16, 3, &addHint);
  assert((u64)v4 == expect4);


  MemArenaFree(mem);
  // printf("--------------------------------------------------\n");
}
