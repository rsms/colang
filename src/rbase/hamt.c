#include "rbase.h"
#include "hamt.h"
#include "pool.h"

// NodeType is used in Node.tag to communicate a node's type
typedef enum {
  THamt      = 0, // tag bits 00
  TValue     = 1, // tag bits 01
  TCollision = 2, // tag bits 10
} NodeType;

// Node is either a HAMT, value or a collision (set of values with same key/hash)
// typedef struct HamtNode Node;
// typedef struct Node {
//   atomic_u32 refs;      // reference count -- MUST BE FIRST FIELD! --
//   u32        tag;       // bit 0-2 = type, bit 3-31 = len
//   HamtUInt   bmap;      // used for key when type==TValue
//   Node*      entries[]; // THamt:[THamt|TCollision|TValue], TCollision:[TValue], TValue:void*
// } Node;


#define TAG_TYPE_NBITS 2 // bits reserved in tag for type (2 = 0-3: 00, 01, 10, 11)
#define TAG_LEN_MASK   (UINT32_MAX << TAG_TYPE_NBITS) // 0b11111111111111111111111111111100
#define TAG_TYPE_MASK  (TAG_LEN_MASK ^ UINT32_MAX)    // 0b00000000000000000000000000000011
#define NODE_LEN_MAX   (UINT32_MAX >> TAG_TYPE_NBITS) // 0b00111111111111111111111111111111
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

// _nodepool is a set of "free lists" for each Node length class.
static Pool _nodepool[HAMT_BRANCHES + 1];

static HamtNode _empty_hamt = {
  .refs = 1,
  .bmap = 0,
  .tag = THamt, // len = 0
};

// static inline u32 hamt_mask(u32 hash, u32 shift) { return ((hash >> shift) & HAMT_MASK); }
// static inline u32 bitpos(u32 hash, u32 shift) { return (u32)1 << hamt_mask(hash, shift); }
static inline u32 bitindex(HamtUInt bitmap, u32 bit) {
  return popcount(bitmap & (bit - 1));
}

static HamtNode* node_alloc(u32 len, NodeType typ) {
  // assertions about limits of len
  #ifndef NDEBUG
  switch (typ) {
    case THamt:      assert(len <= HAMT_BRANCHES); break;
    case TCollision: assert(len <= NODE_LEN_MAX); break;
    case TValue:     break;
    default:         assert(!"invalid type"); break;
  }
  #endif

  if (len <= HAMT_BRANCHES) {
    // try to take from freelist
    PoolEntry* freen = PoolTake(&_nodepool[len]);
    if (freen != NULL) {
      // PoolEntry offset at &HamtNode.entries[0]
      auto n = (HamtNode*)(((char*)freen) - offsetof(HamtNode,entries));
      NODE_SET_TYPE(n, typ);
      assert(n->refs == 1);
      return n;
    }
  }
  auto n = (HamtNode*)HAMT_MEMALLOC(sizeof(HamtNode) + (sizeof(HamtNode*) * len));
  n->refs = 1;
  n->tag = (len << TAG_TYPE_NBITS) | typ;
  return n;
}

// steals reference of value
static HamtNode* value_alloc(HamtUInt key, void* value) {
  auto n = node_alloc(0, TValue);
  NODE_SET_KEY(n, key);
  n->entries[0] = value;
  return n;
}

static void node_release(HamtCtx* ctx, HamtNode* n);

// node_free_noentries assumes that n->entries are released or invalid
static void node_free_noentries(HamtCtx* ctx, HamtNode* n) {
  u32 len = NODE_LEN(n);
  if (len > HAMT_BRANCHES) {
    HAMT_MEMFREE(n);
  } else {
    // put into free list pool
    n->refs = 1;
    PoolAdd(&_nodepool[len], (PoolEntry*)&n->entries[0]);
  }
}

static void node_free(HamtCtx* ctx, HamtNode* n) {
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

inline static HamtNode* node_retain(HamtNode* n) {
  AtomicAdd(&n->refs, 1);
  return n;
}

inline static void node_release(HamtCtx* ctx, HamtNode* n) {
  if (AtomicSub(&n->refs, 1) == 1)
    node_free(ctx, n);
}

void _hamt_free(Hamt h) {
  // called by hamt_release when a Hamt handle's refcount drops to 0
  node_free(h.ctx, (HamtNode*)h.root);
}

// node_clone makes a copy of a THamt or TCollision node, including entries.
//
//                 A B C     A B  C                  len(src)+dstlendelta
//                 | | |     | |  |                           |
// node_clone([0,1,2,3,4,5], 2,3, 4, 0)   =>  [0,1] + [ , ,3,4]
//                           | |  |                |       |
//                           | |  +- dstoffs       |       +- dstoffs
//                           | +- srcsplit2        |       +- srcsplit2
//                           +- srcsplit1          +- srcsplit1
//
// Examples:
//
//   node_clone([0,1,2,3,4,5], 2,2, 2, 0)   =>  [0,1] + [2,3,4,5]
//   node_clone([0,1,2,3,4,5], 2,2, 3, 0)   =>  [0,1] + [ ,2,3,4]
//   node_clone([0,1,2,3,4,5], 2,2, 4, 0)   =>  [0,1] + [ , ,2,3]
//   node_clone([0,1,2,3,4,5], 2,2, 5, 0)   =>  [0,1] + [ , , ,2]
//
//   node_clone([0,1,2,3,4,5], 2,2, 2, 0)   =>  [0,1] + [2,3,4,5]
//   node_clone([0,1,2,3,4,5], 2,3, 2, 0)   =>  [0,1] + [3,4,5, ]
//   node_clone([0,1,2,3,4,5], 2,4, 2, 0)   =>  [0,1] + [3,4, , ]
//   node_clone([0,1,2,3,4,5], 2,5, 2, 0)   =>  [0,1] + [3, , , ]
//
//   node_clone([0,1,2,3,4,5], 2,2, 2, 0)   =>  [0,1] + [2,3,4,5]
//   node_clone([0,1,2,3,4,5], 2,2, 2, 1)   =>  [0,1] + [2,3,4,5, ]
//   node_clone([0,1,2,3,4,5], 2,2, 2, 2)   =>  [0,1] + [2,3,4,5, , ]
//   node_clone([0,1,2,3,4,5], 2,2, 2, -1)  =>  [0,1] + [2,3,4]
//   node_clone([0,1,2,3,4,5], 2,2, 2, -2)  =>  [0,1] + [2,3]
//
//   node_clone([0,1,2,3,4,5], 2,2, 2, -1)  =>  [0,1] + [2,3,4]
//   node_clone([0,1,2,3,4,5], 2,2, 3, -1)  =>  [0,1] + [ ,2,3]
//   node_clone([0,1,2,3,4,5], 2,2, 4, -1)  =>  [0,1] + [ , ,2]
//   node_clone([0,1,2,3,4,5], 2,2, 5, -1)  =>  [0,1] + [ , , ]
//
//   node_clone([0,1,2,3,4,5], 2,3, 2, -1)  =>  [0,1] + [3,4,5]
//   node_clone([0,1,2,3,4,5], 2,3, 2, -2)  =>  [0,1] + [3,4]
//   node_clone([0,1,2,3,4,5], 2,3, 2, -3)  =>  [0,1] + [3]
//
//   node_clone([0,1,2,3,4,5], 2,3, 2, -1)  =>  [0,1] + [3,4,5]
//   node_clone([0,1,2,3,4,5], 2,4, 2, -2)  =>  [0,1] + [4,5]
//   node_clone([0,1,2,3,4,5], 2,4, 2, -3)  =>  [0,1] + [5]
//
//   node_clone([0,1,2,3,4,5], 2,2, 2, 0)   =>  [0,1] + [2,3,4,5]
//   node_clone([0,1,2,3,4,5], 2,3, 3, 0)   =>  [0,1] + [ ,3,4,5]
//   node_clone([0,1,2,3,4,5], 2,4, 4, 0)   =>  [0,1] + [ , ,3,4]
//   node_clone([0,1,2,3,4,5], 2,5, 5, 0)   =>  [0,1] + [ , , ,3]
//
//   node_clone([0,1,2,3,4,5], 2,2, 2, 1)   =>  [0,1] + [2,3,4,5, ]
//   node_clone([0,1,2,3,4,5], 2,2, 3, 1)   =>  [0,1] + [ ,2,3,4,5]
//   node_clone([0,1,2,3,4,5], 2,2, 4, 1)   =>  [0,1] + [ , ,2,3,4]
//   node_clone([0,1,2,3,4,5], 2,2, 5, 1)   =>  [0,1] + [ , , ,2,3]
//
static HamtNode* node_clone(
  HamtNode* src, u32 srcsplit1, u32 srcsplit2, u32 dstoffs, i32 dstlendelta)
{
  u32 srclen = NODE_LEN(src);
  u32 dstlen = (u32)MAX(0, (i32)srclen + dstlendelta);
  u32 srcend = (u32)((i32)srclen + MIN(0, dstlendelta + (srcsplit2 - srcsplit1)));

  assertop(srcsplit1, <= ,srclen);
  assertop(srcsplit2, <= ,srclen);
  assertop(srcsplit1, <= ,srcsplit2);

  // dlog("\nnode_clone(src %p  srcsplit %u:%u  dstoffs %u  dstlendelta %d)  srclen %u  dstlen %u",
  //   src, srcsplit1, srcsplit2, dstoffs, dstlendelta, srclen, dstlen);

  HamtNode* dst = node_alloc(dstlen, NODE_TYPE(src));
  dst->bmap = src->bmap;

  u32 srci = 0;
  // dlog("  segment A: dst[0:%u] <= src[0:%u]", srcsplit1, srcsplit1);
  for (; srci < srcsplit1; srci++) {
    // dlog("    copy dst->entries[%u] <= src->entries[%u]  %p", srci, srci, src->entries[srci]);
    dst->entries[srci] = node_retain(src->entries[srci]);
  }

  u32 dsti = dstoffs;
  srci = srcsplit2;
  // dlog("  segment B: dst[%u:%u] <= src[%u:%u]", dsti, dstlen, srci, srcend);
  for (; srci < srcend; srci++, dsti++) {
    // dlog("    copy dst->entries[%u] <= src->entries[%u]  %p", dsti, srci, src->entries[srci]);
    assert(dsti < dstlen);
    dst->entries[dsti] = node_retain(src->entries[srci]);
  }

  return dst;
}


// collision_with returns a new collection that is a copy of c1 with v2 added
static HamtNode* collision_with(HamtCtx* ctx, HamtNode* c1, HamtNode* v2, bool* didadd) {
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
      *didadd = false;
    } else {
      c2->entries[j] = node_retain(c1->entries[j]);
    }
  }
  return c2;
}

static HamtNode* make_collision(HamtNode* v1, HamtNode* v2) {
  auto c = node_alloc(2, TCollision);
  c->entries[0] = v1;
  c->entries[1] = v2;
  return c;
}


// make_branch creates a HAMT at level with two entries v1 and v2, or a collision.
// steals refs to both v1 and v2
static HamtNode* make_branch(u32 shift, HamtUInt key1, HamtNode* v1, HamtNode* v2) {
  // Compute the "path component" for v1.key and v2.key for level.
  // shift is the new level for the branch which is being created.
  u32 index1 = (key1 >> shift) & HAMT_MASK;
  u32 index2 = (NODE_KEY(v2) >> shift) & HAMT_MASK;

  // loop that creates new branches while key prefixes are shared.
  //
  // head and tail of a chain when there is subindex conflict,
  // representing intermediate branches.
  HamtNode* mHead = NULL;
  HamtNode* mTail = NULL;
  while (index1 == index2) {
    // the current path component is equivalent; either the key is larger than the max depth
    // of the hamt implementation (collision) or we need to create an intermediate branch.

    assert(shift < HAMT_BRANCHES);
    // if (shift >= HAMT_BRANCHES) {
    //   // create collision node
    //   auto c = make_collision(v1, v2);
    //   if (mHead == NULL)
    //     return c;
    //   // We have an existing head we build in the loop above.
    //   // Add c to its tail and return the head.
    //   mTail->entries[0] = c;
    //   return mHead;
    // }

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

    shift += HAMT_BITS;
    index1 = (key1 >> shift) & HAMT_MASK;
    index2 = (NODE_KEY(v2) >> shift) & HAMT_MASK;
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
static HamtNode* hamt_insert(HamtCtx* ctx, HamtNode* m, u32 shift, HamtNode* v2, bool* didadd) {
  assert(NODE_TYPE(m) == THamt);
  assert(v2 != NULL);

  u32 bitpos = 1u << ((NODE_KEY(v2) >> shift) & HAMT_MASK);  // key bit position
  u32 bi     = bitindex(m->bmap, bitpos);  // bucket index

  // Now, one of three cases may be encountered:
  //
  // 1. The entry is empty indicating that the key is not in the tree.
  //    The value is inserted into the THamt.
  //
  // 2. The entry is an entry (user-provided value.)
  //    If a lookup is performed, check for a match to determine success or failure.
  //    If an insertion is performed, one of the following cases are encountered:
  //
  //    2.1. The existing value v1 is equivalent to the new value v2; v1 is replaced by v2.
  //
  //    2.2. Existing v1 is different from v2 but shares the same key (i.e. hash collision.)
  //         v1 and v2 are moved into a TCollision set; v1 is replaced by this TCollision set.
  //
  //    2.3. v1 and v2 are different with different keys. A new THamt m2 is created
  //         with v1 and v2 at entries2[0,2]={v1,v2}. v1 is replaced by m2.
  //
  // 3. The entry is a THamt; called a sub-hash table in the original HAMT paper.
  //    Evalute steps 1-3 on the map by calling hamt_insert() recursively.
  //
  // 4. The entry is a TCollision set.
  //

  // dlog("hamt_insert level=%u, v2.key=%u, bitindex(bitpos 0x%x) => %u",
  //   shift / HAMT_BITS, NODE_KEY(v2), bitpos, bi);

  if ((m->bmap & bitpos) == 0) {
    // empty; index bit not set in bmap. Set the bit and append value to entries list.
    // copy entries in m2 with +1 space for slot at bi
    HamtNode* m2 = node_clone(m, bi,bi, bi+1, 1); // e.g. [1,2,3,4] => [1,2, ,3,4] where bi=2
    m2->bmap = m->bmap | bitpos; // mark bi as being occupied
    m2->entries[bi] = v2;
    return m2;
  }

  // An entry or branch occupies the slot; replace m2->entries[bi]

  HamtNode* m2 = node_clone(m, bi,bi+1, bi+1, 0); // e.g. [1,2,3,4] => [1,2, ,4] where bi=2
  HamtNode* v1 = m->entries[bi]; // current entry
  HamtNode* newobj; // to be assigned as m2->entries[bi]

  switch (NODE_TYPE(v1)) {

  case THamt:
    // follow branch
    // Note: Consider converting this to use iteration instead of recursion
    // since this is a hot path. Most calls to hamt_insert is at least one level deep.
    newobj = hamt_insert(ctx, v1, shift + HAMT_BITS, v2, didadd);
    break;

  case TCollision: {
    // existing collision (invariant: last branch; shift >= (HAMT_BRANCHES - shift))
    HamtNode* c1 = v1;
    HamtUInt key1 = NODE_KEY(c1->entries[0]);
    if (key1 == NODE_KEY(v2)) {
      newobj = collision_with(ctx, c1, v2, didadd);
    } else {
      newobj = make_branch(shift + HAMT_BITS, key1, node_retain(c1), v2);
    }
    break;
  }

  case TValue: {
    // A value already exists at this path
    if (NODE_KEY(v1) == NODE_KEY(v2)) {
      if (ctx->enteq(ctx, (const void*)v1->entries[0], (const void*)v2->entries[0])) {
        // replace current value with v2 since they are equivalent
        newobj = v2;
        *didadd = false;
      } else {
        newobj = make_collision(node_retain(v1), v2);
      }
    } else {
      // branch
      newobj = make_branch(shift + HAMT_BITS, NODE_KEY(v1), node_retain(v1), v2);
    }
    break;
  }

  } // switch

  assert(v1 != newobj);
  m2->entries[bi] = newobj;
  return m2;
}


// collision_without returns a new collection that is a copy of c1 but without v2
// IMPORTANT: If v2 is not found then c1 is returned _without an incresed refcount_.
//            However when v2 is found a copy with a _+1 refcount_ is returned.
static HamtNode* collision_without(HamtCtx* ctx, HamtNode* c1, const void* refentry) {
  assert(NODE_TYPE(c1) == TCollision);
  u32 len = NODE_LEN(c1);
  for (u32 i = 0; i < len; i++) {
    HamtNode* v1 = c1->entries[i];
    if (ctx->enteq(ctx, (const void*)v1->entries[0], refentry)) {
      if (len == 2) {
        // collapse the collision set; return the other entry
        return node_retain(c1->entries[!i]); // !i = (i == 0 ? 1 : 0)
      }
      // clone without entry i
      return node_clone(c1, i,i+1, i, -1);  // e.g. [1,2,3,4] => [1,2,4] where bi=2
    }
  }
  return c1;
}


static HamtNode* hamt_remove(
  HamtCtx*     ctx,
  HamtNode*    m1,
  HamtUInt     key,
  const void*  refentry,
  u32          shift)
{
  assert(NODE_TYPE(m1) == THamt);
  u32 bitpos = 1u << ((key >> shift) & HAMT_MASK); // key bit position
  u32 bi     = bitindex(m1->bmap, bitpos);         // bucket index

  // dlog("hamt_remove level=%u, v2.key=%u, bitindex(bitpos 0x%x) => %u",
  //   shift / HAMT_BITS, key, bitpos, bi);

  if ((m1->bmap & bitpos) != 0) {
    HamtNode* n = m1->entries[bi];
    switch (NODE_TYPE(n)) {

    case THamt: {
      // enter branch, calling remove() recursively, then either collapse the path into just
      // a value in case remove() returned a HAMT with a single Value, or just copy m with
      // the map returned from remove() at bi.
      //
      // Note: consider making this iterative; non-recursive.
      HamtNode* m3 = hamt_remove(ctx, n, key, refentry, shift + HAMT_BITS);
      if (m3 != n) {
        HamtNode* m2 = node_clone(m1, bi,bi+1, bi+1, 0); // e.g. [1,2,3,4] => [1,2, ,4] where bi=2
        if (NODE_LEN(m3) == 1 && NODE_TYPE(m3->entries[0]) != THamt) {
          // collapse path
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
      HamtNode* v2 = collision_without(ctx, n, refentry);
      if (v2 != n) { // found & removed
        // Note: NODE_TYPE(v2) is either TCollision or TValue
        HamtNode* m2 = node_clone(m1, bi,bi+1, bi+1, 0); // e.g. [1,2,3,4] => [1,2, ,4] where bi=2
        m2->entries[bi] = v2;
        return m2;
      }
      break;
    }

    case TValue: {
      if (key == NODE_KEY(n) && ctx->enteq(ctx, (const void*)n->entries[0], refentry)) {
        // this value matches; remove it
        u32 z = NODE_LEN(m1);
        if (z == 1) { // last value of this hamt
          m1 = node_retain(&_empty_hamt);
        } else {
          // make a copy of m1 without entries[bi]
          HamtNode* m2 = node_clone(m1, bi,bi+1, bi, -1); // e.g. [1,2,3,4] => [1,2,4] where bi=2
          m2->bmap = m1->bmap & ~bitpos;
          return m2;
        }
      }
      break;
    }
    } // switch
  }

  return node_retain(m1);
}


static const HamtNode* nullable hamt_lookup(
  HamtCtx* ctx, HamtNode* m, HamtUInt key, const void* refent)
{
  assert(NODE_TYPE(m) == THamt);
  u32 shift = 0;
  while (1) {
    // Check if index bit is set in bitmap
    u32 bitpos = 1u << ((key >> shift) & HAMT_MASK);
    if ((m->bmap & bitpos) == 0)
      return NULL;

    // Compare to value at m.entries[bi]
    // where bi is the bucket index by mapping index bit -> bucket index.
    HamtNode* n = m->entries[bitindex(m->bmap, bitpos)];
    switch (NODE_TYPE(n)) {
      case THamt: {
        m = n;
        break;
      }
      case TCollision: {
        if (key == NODE_KEY(n->entries[0])) {
          // note: it may happen that we encounter a collision node on our way
          // to a non-existing entry. For example, if there's a collision node at
          // index path 1/2/3 and we are looking for an entry with key 1/2/3/1 where
          // that index path does not exist, we will end our search at the collision
          // node. Therefore the above key check is needed to avoid calling enteq for
          // all entries of a collision node that will never be true.
          for (u32 i = 0; i < NODE_LEN(n); i++) {
            HamtNode* v = n->entries[i];
            if (ctx->enteq(ctx, v->entries[0], refent))
              return v;
          }
        }
        return NULL;
      }
      case TValue: {
        if (key == NODE_KEY(n) && ctx->enteq(ctx, n->entries[0], refent))
          return n;
        return NULL;
      }
    }
    shift += HAMT_BITS;
  }
  UNREACHABLE;
}


static void node_count(HamtNode* n, size_t* count) {
  u32 len = NODE_LEN(n);
  switch (NODE_TYPE(n)) {
    case THamt:
      for (u32 i = 0; i < len; i++) {
        HamtNode* e = n->entries[i];
        if (NODE_TYPE(e) == TValue) {
          (*count)++;
        } else {
          node_count(e, count);
        }
      }
      break;
    case TCollision:
      *count += len;
      break;
    case TValue:
      UNREACHABLE;
  }
}

size_t hamt_count(Hamt h) {
  size_t count = 0;
  node_count(h.root, &count);
  return count;
}


void hamt_iter_init(Hamt h, HamtIter* it) {
  memset(it, 0, sizeof(HamtIter));
  it->n = h.root;
}


bool hamt_iter_next(HamtIter* it, const void** entry_out) {
  while (1) {
    HamtNode* n = (HamtNode*)it->n;
    assert(NODE_TYPE(n) == THamt || NODE_TYPE(n) == TCollision);
    if (it->i == NODE_LEN(n)) {
      // reached end of collection node n
      if (it->istacklen == 0)
        return false; // end of iteration
      // restore from stack
      it->i = it->istack[--it->istacklen];
      it->n = it->nstack[--it->nstacklen];
    } else {
      // load entry in n at it->i
      HamtNode* n2 = n->entries[it->i++];
      if (NODE_TYPE(n2) == TValue) {
        *entry_out = n2->entries[0];
        return true;
      }
      // only hamt nodes get this far; collision sets only contains value nodes
      asserteq(NODE_TYPE(n), THamt);
      // save on stack
      it->istack[it->istacklen++] = it->i;
      it->nstack[it->nstacklen++] = it->n;
      it->i = 0;
      it->n = n2;
    }
  } // while(1)
  UNREACHABLE;
}

// fmt_key returns a slash-separated representation of a Hamt key.
// Returns shared memory!
static const char* fmt_key(HamtUInt key) {
  static char buf[
    (
      (
        (
          sizeof(HamtUInt) == 1 ? 3 :  // 255
          sizeof(HamtUInt) == 2 ? 5 :  // 65535
          sizeof(HamtUInt) == 4 ? 10 : // 4294967295
                                  20   // 18446744073709551616
        ) + 1 // for '/'
      ) * (HAMT_MAXDEPTH + 1)
    ) + 1
  ];
  int len = 0;
  u32 shift = 0;
  u32 nonzero_index = 0;
  while (1) {
    HamtUInt part = (key >> shift) & HAMT_MASK;
    len += snprintf(&buf[len], sizeof(buf) - len, (sizeof(HamtUInt) == 8 ? "%llu" : "%u"), part);
    if (part != 0)
      nonzero_index = len;
    shift += HAMT_BITS;
    if (shift > HAMT_BRANCHES)
      break;
    buf[len++] = '/';
  }
  buf[nonzero_index] = 0;
  return buf;
}

typedef Str(*EntReprFun)(HamtCtx* ctx, Str s, const void* entry);

static Str node_repr(
  HamtCtx*   ctx,
  HamtNode*  n,
  Str        s,
  int        level,
  Str        indent,
  u32        rindex)
{
  assert(n != NULL);
  assert(level <= HAMT_MAXDEPTH + 1);

  u32 indent_len = str_len(indent);
  if (level > 0) {
    Str indent_check = indent;
    s = str_appendc(s, '\n');
    s = str_appendstr(s, indent);
    if (rindex == 1) {
      indent = str_appendcstr(indent, "   ");
      s = str_appendcstr(s, "└─ ");
    } else {
      indent = str_appendcstr(indent, "│  ");
      s = str_appendcstr(s, "├─ ");
    }
    assert(indent_check == indent); // must not memrealloc
  } else if (level < 0 && level > -100) {
    s = str_appendc(s, ' ');
  }

  switch (NODE_TYPE(n)) {
    case THamt:
      if (level < 0) {
        s = str_appendcstr(s, "(hamt");
      } else {
        s = str_appendfmt(s, "Hamt %p %u", n, NODE_LEN(n));
      }
      break;
    case TCollision:
      if (level < 0) {
        s = str_appendcstr(s, "(collision");
      } else {
        s = str_appendfmt(s, "Collision %p %u", n, NODE_LEN(n));
      }
      break;
    case TValue:
      if (level < 0) {
        if (ctx->entrepr) {
          s = ctx->entrepr(ctx, s, n->entries[0]);
        } else {
          s = str_appendfmt(s, "%p", n->entries[0]);
        }
      } else {
        s = str_appendfmt(s, "Value %p %s", n, fmt_key(NODE_KEY(n)));
        if (ctx->entrepr) {
          s = str_appendc(s, ' ');
          s = ctx->entrepr(ctx, s, n->entries[0]);
        }
      }
      str_setlen(indent, indent_len);
      return s;
  } // switch

  u32 len = NODE_LEN(n);
  for (u32 i = 0; i < len; i++) {
    s = node_repr(ctx, n->entries[i], s, level + 1, indent, len - i);
  }

  if (level < 0)
    s = str_appendc(s, ')');

  // restore indentation
  str_setlen(indent, indent_len);
  return s;
}

// map_repr returns a human-readable, printable string representation
Str hamt_repr(Hamt h, Str s, bool pretty) {
  assert(NODE_TYPE(h.root) == THamt);
  auto indent = str_new(HAMT_MAXDEPTH * 8);
  return node_repr(h.ctx, h.root, s, pretty ? 0 : -100, indent, 0);
}

Hamt hamt_new(HamtCtx* ctx) {
  assert(ctx->entkey != NULL);
  assert(ctx->enteq != NULL);
  assert(ctx->entfree != NULL);
  return (Hamt){ node_retain(&_empty_hamt), ctx };
}

// steals ref to entry
Hamt hamt_with(Hamt h, void* entry, bool* didadd) {
  // TODO: consider mutating h.root when h.root->refs==1
  auto v = value_alloc(h.ctx->entkey(h.ctx, entry), entry);
  *didadd = true;
  return (Hamt){ hamt_insert(h.ctx, h.root, 0, v, didadd), h.ctx };
}

// steals ref to entry, mutates h
bool hamt_set(Hamt* h, void* entry) {
  bool didadd;
  auto h2 = hamt_with(*h, entry, &didadd);
  node_release(h->ctx, h->root);
  h->root = h2.root;
  return didadd;
}

// returns borrowed ref to entry, or NULL if not found
bool hamt_getk(Hamt h, const void** entry, HamtUInt key) {
  const HamtNode* v = hamt_lookup(h.ctx, h.root, key, *entry);
  if (v == NULL)
    return false;
  *entry = v->entries[0];
  return true;
}

// borrows ref to entry
Hamt hamt_withoutk(Hamt h, const void* entry, HamtUInt key, bool* removed) {
  // TODO: consider mutating h when h.refs==1
  auto m2 = hamt_remove(h.ctx, h.root, key, entry, 0);
  *removed = m2 != h.root;
  return (Hamt){ m2, h.ctx };
}

bool hamt_delk(Hamt* h, const void* entry, HamtUInt key) {
  // TODO: consider mutating h when h.refs==1
  auto m2 = hamt_remove(h->ctx, h->root, key, entry, 0);
  if (h->root == m2)
    return false;
  node_release(h->ctx, h->root);
  h->root = m2;
  return true;
}


