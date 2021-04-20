#include "rbase.h"
#include "hamt.h"
#include "pool.h"

//#define BRANCHES 32
//#define BITS     5  /* ((u32)7 - ((u32)64 / BRANCHES)) */
//#define MASK     (BRANCHES-1)
//#define MAXLEVEL 7

#ifndef HAMT_MEM
  #define HAMT_MEM NULL /* use global memory */
#endif

#define BITS     5              /* 6, 5, 4, 3, 2, 1 */
#define BRANCHES (1 << BITS)    /* 2^6=64, 2^5=32, 2^4=16, 2^3=8, 2^2=4, 2^1=2 */
#define MASK     (BRANCHES - 1) /* 63, 31 (0x1f), 15, 7, 3, 1 */
#define MAXLEVEL 7


// NodeType is used in Node.tag to communicate a node's type
typedef enum {
  TValue     = 0, // tag bits 00
  THamt      = 1, // tag bits 01
  TCollision = 2, // tag bits 10
} NodeType;

// Node is either a HAMT, value or a collision (set of values with same key/hash)
typedef struct Node Node;
typedef struct Node {
  atomic_u32 refs;      // reference count -- MUST BE FIRST FIELD! --
  u32        tag;       // bit 0-2 = type, bit 3-31 = len
  u32        bmap;      // used for key when type==TValue
  Node*      entries[]; // THamt:[THamt|TCollision|TValue], TCollision:[TValue], TValue:void*
} Node;


#define TAG_TYPE_NBITS 2 // bits reserved in tag for type (2 = 0-3: 00, 01, 10, 11)
#define TAG_LEN_MASK   (UINT32_MAX << TAG_TYPE_NBITS) // 0b11111111111111111111111111111100
#define TAG_TYPE_MASK  (TAG_LEN_MASK ^ UINT32_MAX)    // 0b00000000000000000000000000000011
#define NODE_LEN_MAX    (UINT32_MAX >> TAG_TYPE_NBITS) // 0b00111111111111111111111111111111
// Using last 30 bits of tag as len, we can store up to 1 073 741 823 values
// in one collision node. That's more than enough.

#define NODE_TYPE(n)          ((NodeType)( (n)->tag & TAG_TYPE_MASK ))
#define NODE_SET_TYPE(n, typ) ({ (n)->tag = ((n)->tag & ~TAG_TYPE_MASK) | (typ); })

#define NODE_LEN(n)           ( (n)->tag >> TAG_TYPE_NBITS )
#define NODE_SET_LEN(n, len)  \
  ({ (n)->tag = ((len) << TAG_TYPE_NBITS) | ((n)->tag & TAG_TYPE_MASK); })

// NODE_KEY retrieves a TValue's key/hash
#define NODE_KEY(n)          ((n)->bmap)
#define NODE_SET_KEY(n, key) ((n)->bmap = (key))

#define popcount(x) _Generic((x), \
  int:                 __builtin_popcount, \
  unsigned int:        __builtin_popcount, \
  long:                __builtin_popcountl, \
  unsigned long:       __builtin_popcountl, \
  long long:           __builtin_popcountll, \
  unsigned long long:  __builtin_popcountll \
)(x)

// _nodepool is a set of "free lists" for each Node length class.
static Pool _nodepool[BRANCHES+1];

static Node _empty_hamt = {
  .refs = 1,
  .bmap = 0,
  .tag = THamt, // len = 0
};

// static inline u32 hamt_mask(u32 hash, u32 shift) { return ((hash >> shift) & MASK); }
// static inline u32 bitpos(u32 hash, u32 shift) { return (u32)1 << hamt_mask(hash, shift); }
static inline u32 bitindex(u32 bitmap, u32 bit) {
  return popcount(bitmap & (bit - 1));
}

static Node* node_alloc(u32 len, NodeType typ) {
  // assertions about limits of len
  #ifndef NDEBUG
  switch (typ) {
    case THamt:      assert(len <= BRANCHES); break;
    case TCollision: assert(len <= NODE_LEN_MAX); break;
    case TValue:     break;
    default:         assert(!"invalid type"); break;
  }
  #endif

  if (len <= BRANCHES) {
    // try to take from freelist
    PoolEntry* freen = PoolTake(&_nodepool[len]);
    if (freen != NULL) {
      // PoolEntry offset at &Node.entries[0]
      auto n = (Node*)(((char*)freen) - offsetof(Node,entries));
      NODE_SET_TYPE(n, typ);
      assert(n->refs == 1);
      return n;
    }
  }
  auto n = (Node*)memalloc_raw(HAMT_MEM, sizeof(Node) + (sizeof(Node*) * len));
  n->refs = 1;
  n->tag = (len << TAG_TYPE_NBITS) | typ;
  return n;
}

// steals reference of value
static Node* value_alloc(u32 key, void* value) {
  auto n = node_alloc(0, TValue);
  NODE_SET_KEY(n, key);
  n->entries[0] = value;
  return n;
}

static void node_release(HamtCtx* ctx, Node* n);

// node_free_noentries assumes that n->entries are released or invalid
static void node_free_noentries(HamtCtx* ctx, Node* n) {
  u32 len = NODE_LEN(n);
  if (len > BRANCHES) {
    memfree(HAMT_MEM, n);
  } else {
    // put into free list pool
    n->refs = 1;
    PoolAdd(&_nodepool[len], (PoolEntry*)&n->entries[0]);
  }
}

static void node_free(HamtCtx* ctx, Node* n) {
  //dlog("free node %p (type %u)", n, NODE_TYPE(n));
  u32 len = NODE_LEN(n);
  if (NODE_TYPE(n) == TValue) {
    assert(len == 0);
    ctx->entfree(ctx, n->entries[0]);
  } else { // THamt, TCollision
    for (u32 i = 0; i < len; i++)
      node_release(ctx, n->entries[i]);
  }
  node_free_noentries(ctx, n);
}

inline static Node* node_retain(Node* n) {
  AtomicAdd(&n->refs, 1);
  return n;
}

inline static void node_release(HamtCtx* ctx, Node* n) {
  if (AtomicSub(&n->refs, 1) == 1)
    node_free(ctx, n);
}

void _hamt_free(Hamt h) {
  // called by hamt_release when a Hamt handle's refcount drops to 0
  node_free(h.ctx, (Node*)h.p);
}


// // value_eq returns true if v1 and v2 have identical keys and equivalent values as
// // determined by TestValueEqual. In case the values are NOT equal, key1_out is set to
// // the computed key of v1 and false is returned.
// static bool value_eq(HamtCtx* ctx, Node* v1, Node* v2) {
//   return (
//     NODE_KEY(v1) == NODE_KEY(v2) &&
//     ctx->enteq(ctx, (const void*)v1->entries[0], (const void*)v2->entries[0])
//   );
// }

// collision_with returns a new collection that is a copy of c1 with v2 added
static Node* collision_with(HamtCtx* ctx, Node* c1, Node* v2) {
  assert(NODE_TYPE(c1) == TCollision);
  assert(NODE_TYPE(v2) == TValue);

  // There are two likely scenarios here:
  //   1. v2 is unique in this collision set and is to be added
  //   2. v2 is equivalent to an existing node in the set and should take its place
  // Since nodes are allocated with their length known up front we have two options:
  //   A) Two loops where we first check for equivalent entries, computing the new length,
  //      and then a second loop where we copy entries.
  //   B) Two loops where we make an assumption about either case 1 or 2 and fall back
  //      to a second loop if we guessed wrong.
  // Approach B is what we are taking here, assuming that entries are more often added to
  // collisions than they are replacing equivalent values.

  const void* nextentry = v2->entries[0];
  u32 len = NODE_LEN(c1);
  auto c2 = node_alloc(len + 1, TCollision);
  u32 i = 0;
  for (; i < len; i++) {
    if (ctx->enteq(ctx, c1->entries[i]->entries[0], nextentry))
      goto replace;
    c2->entries[i] = node_retain(c1->entries[i]);
  }
  c2->entries[i] = v2;
  return c2;

 replace:
  // we guessed wrong; replace a node. i is the index of the node to replace.
  // release entries added so far
  for (u32 j = 0; j < i; j++)
    node_release(ctx, c2->entries[j]);
  // free the node of incorrect length
  node_free_noentries(ctx, c2);
  // allocate a new node of correct length
  c2 = node_alloc(len, TCollision);
  for (u32 j = 0; j < len; j++) {
    if (j == i) {
      c2->entries[j] = v2;
      if (ctx->entreplace)
        ctx->entreplace(ctx, c1->entries[j]->entries[0], v2->entries[0]);
    } else {
      c2->entries[j] = node_retain(c1->entries[j]);
    }
  }
  return c2;
}

static Node* make_collision(Node* v1, Node* v2) {
  auto c = node_alloc(2, TCollision);
  c->entries[0] = v1;
  c->entries[1] = v2;
  return c;
}

// hamt_clone makes a copy of a Hamt
// bi: skip copying this entry (leave m2->entries[bi] uninitialized).
//     set to BRANCHES+1 to copy all entries.
//
// hamt_clone(m1, BRANCHES+1, 0)   identical clone
// hamt_clone(m1, 2, 0)            clone with entries[2] uninitialized
// hamt_clone(m1, 2, 1)            clone without entries[2]
//
// Returns a new Hamt with a +1 reference count.
static Node* hamt_clone(Node* m1, u32 bi, u32 dropcount) {
  u32 len1 = NODE_LEN(m1);
  u32 len2 = len1 - dropcount;
  Node* m2 = node_alloc(len2, THamt);
  m2->bmap = m1->bmap;

  // copy [0..bi] or [0..len2]
  u32 i1 = 0;
  for (; i1 < MIN(len2, bi); i1++) {
    //dlog("clone (A) m2->entries[%u] <= m1->entries[%u]", i1, i1);
    m2->entries[i1] = node_retain(m1->entries[i1]);
  }

  // either the above loop made a full identical copy of entries or there are entries remaining
  assert(bi > BRANCHES || i1 < NODE_LEN(m1));

  // copy [bi + dropcount..end]
  i1 += dropcount;
  for (u32 i2 = bi; i2 < len2; ) {
    //dlog("clone (B) m2->entries[%u] <= m1->entries[%u]", i2, i1);
    m2->entries[i2++] = node_retain(m1->entries[i1++]);
  }

  return m2;
}


// #define HAMT_COLLISION_IMPL2


// make_branch creates a HAMT at level with two entries v1 and v2, or a collision.
// steals refs to both v1 and v2
static Node* make_branch(u32 shift, u32 key1, Node* v1, Node* v2) {
  // Compute the "path component" for v1.key and v2.key for level.
  // shift is the new level for the branch which is being created.
  u32 index1 = (key1 >> shift) & MASK;
  u32 index2 = (NODE_KEY(v2) >> shift) & MASK;

  // loop that creates new branches while key prefixes are shared.
  //
  // head and tail of a chain when there is subindex conflict,
  // representing intermediate branches.
  Node* mHead = NULL;
  Node* mTail = NULL;
  while (index1 == index2) {
    // the current path component is equivalent; either the key is larger than the max depth
    // of the hamt implementation (collision) or we need to create an intermediate branch.

    if (shift >= BRANCHES) {
      // create collision node
      //
      // TODO: is there a more efficient way to build collision nodes that doesn't require
      // exhausting branch levels?
      // We currently rely on collisions only being at the edges in hamt_insert where if we
      // encounter a collision we simply add to it. If we were to change this code to say look
      // at (key1 == NODE_KEY(v2)) instead of (shift >= BRANCHES), then we'd need to
      // find a way to move a collision out.
      //
      auto c = make_collision(v1, v2);
      if (mHead == NULL)
        return c;
      // We have an existing head we build in the loop above.
      // Add c to its tail and return the head.
      mTail->entries[0] = c;
      return mHead;
    }

    // create an intermediate branc.
    auto m = node_alloc(1, THamt);
    m->bmap = 1u << index1;
    // append to tail of branch list
    if (mTail) {
      // add to list
      mTail->entries[0] = m;
    } else {
      // begin list
      mHead = m;
    }
    mTail = m;

    shift += BITS;
    index1 = (key1 >> shift) & MASK;
    index2 = (NODE_KEY(v2) >> shift) & MASK;
  }

  // create map with v1,v2
  auto m = node_alloc(2, THamt);
  m->bmap = (1u << index1) | (1u << index2);
  if (index1 < index2) {
    m->entries[0] = v1;
    m->entries[1] = v2;
  } else {
    m->entries[0] = v2;
    m->entries[1] = v1;
  }

  if (mHead == NULL)
    return m;

  // We have an existing head we build in the loop above.
  // Add m to its tail and return the head.
  mTail->entries[0] = m;
  return mHead;
}

// steals ref to v2
static Node* hamt_insert(HamtCtx* ctx, Node* m, u32 shift, Node* v2) {
  assert(NODE_TYPE(m) == THamt);
  u32 bitpos = 1u << ((NODE_KEY(v2) >> shift) & MASK);  // key bit position
  u32 bi     = bitindex(m->bmap, bitpos);  // bucket index

  // dlog("hamt_insert level=%u, v2.key=%u, bitindex(bitpos 0x%x) => %u",
  //   shift / BITS, NODE_KEY(v2), bitpos, bi);

  if ((m->bmap & bitpos) == 0) {
    // empty; index bit not set in bmap. Set the bit and append value to entries list.
    // copy entries in m2 with +1 space for slot at bi

    // TODO: use hamt_clone() here instead

    Node* m2 = node_alloc(NODE_LEN(m) + 1, THamt);
    m2->bmap = m->bmap | bitpos;
    // rsms: would memcpy + node_retain without stores be faster here?
    // copy up to bi:
    for (u32 i = 0; i < bi; i++) {
      //dlog("cpy m->entries[%u] => m2->entries[%u] (%p)", i, i, m->entries[i]);
      m2->entries[i] = node_retain(m->entries[i]);
    }
    m2->entries[bi] = v2;
    // copy after bi:
    for (u32 i = bi+1, j = bi; j < NODE_LEN(m); i++, j++) {
      //dlog("cpy m->entries[%u] => m2->entries[%u] (%p)", j, i, m->entries[j]);
      m2->entries[i] = node_retain(m->entries[j]);
    }
    return m2;
  }

  // An entry or branch occupies the slot; replace m2->entries[bi]
  // Note: Consider converting this to use iteration instead of recursion on hamt_insert

  // TODO: use hamt_clone instead of the inline code
  // Node* m2 = hamt_clone(m, bi, 1);
  // ...
  // Note: Need to remove node_release(ctx, v1) further down
  Node* m2 = node_alloc(NODE_LEN(m), THamt);
  m2->bmap = m->bmap;
  for (u32 i = 0; i < NODE_LEN(m); i++)
    m2->entries[i] = node_retain(m->entries[i]);

  Node* v1 = m->entries[bi]; // current entry
  Node* newobj; // to be assigned as m2->entries[bi]

  switch (NODE_TYPE(v1)) {

  case THamt:
    // follow branch
    newobj = hamt_insert(ctx, v1, shift + BITS, v2);
    break;

  case TCollision: {
    // existing collision (invariant: last branch; shift >= (BRANCHES - shift))
    Node* c1 = v1;
    #ifdef HAMT_COLLISION_IMPL2
      u32 key1 = NODE_KEY(c1->entries[0]);
      if (key1 == NODE_KEY(v2)) {
        newobj = collision_with(ctx, c1, v2);
      } else {
        newobj = make_branch(shift + BITS, key1, node_retain(c1), v2);
      }
    #else
      newobj = collision_with(ctx, c1, v2);
    #endif
    break;
  }

  case TValue: {
    // A value already exists at this path
    if (NODE_KEY(v1) == NODE_KEY(v2)) {
      if (ctx->enteq(ctx, (const void*)v1->entries[0], (const void*)v2->entries[0])) {
        // replace current value with v2 since they are equivalent
        newobj = v2;
        if (ctx->entreplace)
          ctx->entreplace(ctx, v1, v2);
      } else {
        #ifdef HAMT_COLLISION_IMPL2
          dlog("——————————— COLLISION BRANCH");
          newobj = make_collision(node_retain(v1), v2); // retain v2 to balance out release later
        #else
          newobj = make_branch(shift + BITS, NODE_KEY(v1), node_retain(v1), v2);
        #endif
      }
    } else {
      // branch
      newobj = make_branch(shift + BITS, NODE_KEY(v1), node_retain(v1), v2);
    }
    break;
  }

  } // switch

  // release the replaced object at m2->entries[bi]
  assert(v1 != newobj);
  node_release(ctx, v1);
  m2->entries[bi] = newobj;

  return m2;
}


static Node* hamt_remove(
  HamtCtx*     ctx,
  Node*        m1,
  u32          key,
  const void*  refentry,
  u32          shift,
  bool*        collision)
{
  assert(NODE_TYPE(m1) == THamt);
  u32 bitpos = 1u << ((key >> shift) & MASK); // key bit position
  u32 bi     = bitindex(m1->bmap, bitpos);    // bucket index

  dlog("hamt_remove level=%u, v2.key=%u, bitindex(bitpos 0x%x) => %u",
    shift / BITS, key, bitpos, bi);

  if ((m1->bmap & bitpos) != 0) {
    Node* n = m1->entries[bi];
    switch (NODE_TYPE(n)) {

    case THamt: {
      dlog(" THamt");
      // enter branch, calling remove() recursively, then either collapse the path into just
      // a value in case remove() returned a HAMT with a single Value, or just copy m with
      // the map returned from remove() at bi.
      //
      // Note: consider making this iterative; non-recursive.
      Node* m3 = hamt_remove(ctx, n, key, refentry, shift + BITS, collision);
      if (m3 != n) {
        Node* m2 = hamt_clone(m1, bi, 0);
        // maybe collapse path
        if (NODE_LEN(m3) == 1 && !*collision && NODE_TYPE(m3->entries[0]) != THamt) {
          dlog("THamt: collapse");
          m2->entries[bi] = node_retain(m3->entries[0]);
          node_release(ctx, m3);
        } else {
          m2->entries[bi] = m3;
        }
        return m2;
      }
      break;
    }

    case TCollision: {
      panic("TODO TCollision");
      break;
    }

    case TValue: {
      dlog(" TValue");
      if (key == NODE_KEY(n) && ctx->enteq(ctx, (const void*)n->entries[0], refentry)) {
        // this value matches; remove it
        u32 z = NODE_LEN(m1);
        if (z == 1) { // last value of this hamt
          m1 = node_retain(&_empty_hamt);
        } else {
          // make a copy of m1 without entries[bi]
          Node* m2 = hamt_clone(m1, bi, /* dropcount */1);
          m2->bmap = m1->bmap & ~bitpos;
          assert(NODE_LEN(m2) == NODE_LEN(m1) - 1);
          return m2;
        }
      }
      break;
    }
    } // switch
  }

  return node_retain(m1);
}


static const Node* nullable hamt_lookup(HamtCtx* ctx, Node* m, u32 key, const void* refentry) {
  assert(NODE_TYPE(m) == THamt);
  u32 shift = 0;
  while (1) {
    // Check if index bit is set in bitmap
    u32 bitpos = 1u << ((key >> shift) & MASK);
    if ((m->bmap & bitpos) == 0)
      return NULL;

    // Compare to value at m.entries[bi]
    // where bi is the bucket index by mapping index bit -> bucket index.
    Node* n = m->entries[bitindex(m->bmap, bitpos)];
    switch (NODE_TYPE(n)) {
      case THamt: {
        dlog("%p THamt", n);
        m = n;
        break;
      }
      case TCollision: {
        dlog("%p TCollision", n);
        if (key == NODE_KEY(n->entries[0])) {
          // note: with HAMT_COLLISION_IMPL2 it may happen that we encounter a collision node
          // on our way to a non-existing entry. For example, if there's a collision node at
          // index path 1/2/3 and we are looking for an entry with key 1/2/3/1 where that index
          // path does not exist, we will end our search at the collision node.
          // Therefore the above key check is needed to avoid calling enteq for all entries of
          // a collision node that will never be true.
          for (u32 i = 0; i < NODE_LEN(n); i++) {
            Node* v = n->entries[i];
            if (ctx->enteq(ctx, v->entries[0], refentry))
              return v;
          }
        }
        return NULL;
      }
      case TValue: {
        dlog("%p TValue", n);
        if (key == NODE_KEY(n) && ctx->enteq(ctx, n->entries[0], refentry))
          return n;
        return NULL;
      }
    }
    shift += BITS;
  }
  UNREACHABLE;
}

typedef Str(*EntReprFun)(Str s, const void* entry);

static Str entrepr_default(Str s, const void* entry) {
  return str_appendfmt(s, "%p", entry);
}

static Str _hamt_repr(Str s, Node* h, EntReprFun entrepr, int level) {
  s = str_appendfmt(s, "hamt (%p level %u)", h, level);
  for (u32 i = 0; i < NODE_LEN(h); i++) {
    s = str_appendfmt(s, "\n%-*s#%d => ", level*2, "", i);
    Node* n = h->entries[i];
    // dlog("%-*sent %u => %p", level*4, "", i, n);
    switch (NODE_TYPE(n)) {
      case THamt:
        s = _hamt_repr(s, n, entrepr, level + 1);
        break;
      case TCollision: {
        s = str_appendcstr(s, "collision");
        for (u32 i = 0; i < NODE_LEN(n); i++) {
          s = str_appendfmt(s, "\n%-*s  - Value ", level*2, "");
          s = entrepr(s, n->entries[i]->entries[0]);
        }
        break;
      }
      case TValue:
        s = entrepr(s, n->entries[0]);
        break;
    } // switch
  }
  return s;
}

// map_repr returns a human-readable, printable string representation
Str hamt_repr(Hamt h, Str s) {
  EntReprFun entrepr = h.ctx->entrepr != NULL ? h.ctx->entrepr : entrepr_default;
  Node* root = (Node*)h.p;
  assert(NODE_TYPE(root) == THamt);
  return _hamt_repr(s, root, entrepr, 1);
}

Hamt hamt_new(HamtCtx* ctx) {
  assert(ctx->entkey != NULL);
  assert(ctx->enteq != NULL);
  assert(ctx->entfree != NULL);
  return (Hamt){ node_retain(&_empty_hamt), ctx };
}

// steals ref to entry
Hamt hamt_with(Hamt h, void* entry) {
  // TODO: consider mutating h when h.refs==1
  auto v = value_alloc(h.ctx->entkey(h.ctx, entry), entry);
  auto m1 = (Node*)h.p;
  return (Hamt){ hamt_insert(h.ctx, m1, 0, v), h.ctx };
}

// steals ref to entry, mutates h
void hamt_set(Hamt* h, void* entry) {
  // TODO: consider mutating h when h.refs==1
  auto ctx = h->ctx;
  auto v = value_alloc(ctx->entkey(ctx, entry), entry);
  auto m1 = (Node*)h->p;
  auto m2 = hamt_insert(ctx, m1, 0, v);
  h->p = m2;
  node_release(ctx, m1);
}

// returns borrwed ref to entry, or NULL if not found
bool hamt_getk(Hamt h, const void** entry, u32 key) {
  const Node* v = hamt_lookup(h.ctx, (Node*)h.p, key, *entry);
  if (v == NULL)
    return false;
  *entry = v->entries[0];
  return true;
}

// borrows ref to entry
Hamt hamt_without(Hamt h, const void* entry, bool* removed) {
  // TODO: consider mutating h when h.refs==1
  bool collision = false; // temporary state
  auto ctx = h.ctx;
  auto m1 = (Node*)h.p;
  auto m2 = hamt_remove(h.ctx, m1, ctx->entkey(ctx, entry), entry, 0, &collision);
  *removed = m2 != m1;
  return (Hamt){ m2, h.ctx };
}

bool hamt_del(Hamt* h, const void* entry) {
  // TODO: consider mutating h when h.refs==1
  bool collision = false; // temporary state
  auto ctx = h->ctx;
  auto m1 = (Node*)h->p;
  auto m2 = hamt_remove(ctx, m1, ctx->entkey(ctx, entry), entry, 0, &collision);
  if (m1 == m2)
    return false;
  node_release(ctx, (Node*)h->p);
  h->p = m2;
  return true;
}


// ------------------------------------------------------------------------------------------------
#if R_UNIT_TEST_ENABLED

// TestValue is a user-defined type
typedef struct TestValue {
  atomic_u32  refs;
  u32         key;
  const char* str;
} TestValue;

// static void _TestValueFree(TestValue* v) {
//   dlog("_TestValueFree %u \"%s\"", v->key, v->str);
//   memset(v, 0, sizeof(TestValue));
//   memfree(NULL, v);
// }

// static void TestValueIncRef(TestValue* v) {
//   AtomicAdd(&v->refs, 1);
// }

// static void TestValueDecRef(TestValue* v) {
//   if (AtomicSub(&v->refs, 1) == 1)
//     _TestValueFree(v);
// }

static u32 TestValueKey(HamtCtx* ctx, const void* v) {
  return ((const TestValue*)v)->key;
  //return hash_fnv1a32((const u8*)v, strlen(v));
}

static bool TestValueEqual(HamtCtx* ctx, const void* a, const void* b) {
  dlog("TestValueEqual %p == %p", a, b);
  return strcmp(((const TestValue*)a)->str, ((const TestValue*)b)->str) == 0;
}

static void TestValueFree(HamtCtx* ctx, void* vp) {
  TestValue* v = (TestValue*)vp;
  dlog("_TestValueFree %u \"%s\"", v->key, v->str);
  memset(v, 0, sizeof(TestValue));
  memfree(NULL, v);
}

static void TestValueOnReplace(HamtCtx* ctx, void* preventry, void* nextentry) {
  dlog("TestValueOnReplace prev %p, next %p", preventry, nextentry);
}

static Str TestValueRepr(Str s, const void* vp) {
  auto v = (const TestValue*)vp;
  return str_appendfmt(s, "TestValue(0x%X \"%s\")", v->key, v->str);
}

// MakeTestValue takes a string of slash-separated integers and builds a Value
// where each integer maps to one level of branching in CHAMP.
//
// For instance, the key "1/2/3/4" produces the key:
//   0b00100_00011_00010_00001
//         4     3     2     1
//
static TestValue* MakeTestValue(const char* str) {
  u32 key = 0;
  u32 shift = 0;
  StrSlice slice = {};
  while (str_splitcstr(&slice, '/', str)) {
    u32 index;
    if (!parseu32(slice.p, slice.len, 10, &index)) {
      dlog("warning: ignoring non-numeric part: \"%.*s\"", (int)slice.len, slice.p);
      continue;
    }
    key |= index << shift;
    shift += BITS;
  }
  auto v = memalloct(NULL, TestValue);
  v->refs = 1;
  v->key = key;
  v->str = str;
  return v;
}

static void HamtTest() {

  dlog("TAG_TYPE_MASK: %x", TAG_TYPE_MASK);
  dlog("TAG_LEN_MASK:  %x", TAG_LEN_MASK);
  dlog("NODE_LEN_MAX:   %x", NODE_LEN_MAX);
  Str tmpstr = str_new(128);

  HamtCtx ctx = {
    // required
    .entkey  = TestValueKey,
    .enteq   = TestValueEqual,
    .entfree = TestValueFree,
    // optional
    .entreplace = TestValueOnReplace,
    .entrepr = TestValueRepr,
  };

  #define REPR(h) (tmpstr = hamt_repr((h), str_setlen(tmpstr, 0)))

  { // test: basics
    Hamt h = hamt_new(&ctx);
    assert(h.p != NULL);
    auto v = MakeTestValue("1");
    v->str = "hello";
    h = hamt_with(h, v);
    dlog("%s", REPR(h));

    auto v2 = (TestValue*)hamt_getp(h, v);
    assert(v2 != NULL);
    assert(v2 == v);
    assert(strcmp(v->str, "hello") == 0);
    hamt_release(h);
  }

  { // test: building trees
    Hamt h = hamt_new(&ctx);

    // key
    fprintf(stderr, "\n");
    auto v = MakeTestValue("1/2/3/4"); // 00100_00011_00010_00001 (LE)
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // cause a branch to be forked
    fprintf(stderr, "\n");
    v = MakeTestValue("1/2/1"); // 00001_00010_00001 (LE)
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // cause a collision; converts a value into a collision branch
    v = MakeTestValue("1/2/1");
    v->str = "1/2/1 (B)";
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // create a new branch (forks existing branch)
    v = MakeTestValue("1/3/1");
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // replace equivalent value in hamt node
    v = MakeTestValue("1/3/1");
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // cause another collision; adds to existing collision set
    v = MakeTestValue("1/2/1");
    v->str = "1/2/1 (C)";
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // // replace equivalent value in collision node
    // v = MakeTestValue("1/2/1");
    // v->str = "1/2/1 (C)";
    // hamt_set(&h, v);
    // dlog("%s", REPR(h));

    // move a collision out to a deeper branch
    v = MakeTestValue("1/2/1/1");
    hamt_set(&h, v);
    dlog("%s", REPR(h));

    // retrieve value in collision node
    v = MakeTestValue("1/2/1");
    auto v2 = (TestValue*)hamt_getp(h, v);
    assert(v2 != NULL);
    assert(v2->key == v->key);
    assert(strcmp(v->str, v2->str) == 0);
    TestValueFree(h.ctx, v);

    // remove non-collision value (first add a few)
    hamt_set(&h, MakeTestValue("1/3/2"));
    hamt_set(&h, MakeTestValue("1/3/3"));
    dlog("%s", REPR(h));
    v = MakeTestValue("1/3/2");
    bool ok = hamt_del(&h, v);
    TestValueFree(h.ctx, v);
    dlog("%s", REPR(h));
    assert(ok);

    // remove remaining values on the same branch
    v = MakeTestValue("1/3/1");
    ok = hamt_del(&h, v);
    TestValueFree(h.ctx, v);
    assert(ok);
    v = MakeTestValue("1/3/3");
    ok = hamt_del(&h, v);
    TestValueFree(h.ctx, v);
    dlog("%s", REPR(h));
    assert(ok);

    fprintf(stderr, "\n");
    hamt_release(h);
  }

  fprintf(stderr, "\n");
}

R_UNIT_TEST(Hamt, { HamtTest(); })

// // fmt_key does the inverse of make_key. Returns shared memory!
// static const char* fmt_key(u32 key) {
//   tmpstr_acq();
//   u32 shift = 0;
//   while (1) {
//     u32 part = (key >> shift) & MASK;
//     tmpstr = str_appendfmt(tmpstr, "%u", part);
//     shift += BITS;
//     if (shift > BRANCHES)
//       break;
//     tmpstr = str_appendc(tmpstr, '/');
//   }
//   return tmpstr;
// }

#endif /* R_UNIT_TEST_ENABLED */
