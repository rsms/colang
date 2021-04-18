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
#define popcount __builtin_popcount  /* __builtin_popcountll for u64 */

// TestValue is a user-defined type
typedef struct TestValue {
  atomic_u32  refs;
  u32         key;
  const char* str;
} TestValue;
static u32 TestValueHash(const TestValue* v) {
  return v->key;
  //return hash_fnv1a32((const u8*)v, strlen(v));
}
static bool TestValueEqual(const TestValue* a, const TestValue* b) {
  return strcmp(a->str, b->str) == 0;
}
static void _TestValueFree(TestValue* v) {
  dlog("_TestValueFree %u \"%s\"", v->key, v->str);
  memset(v, 0, sizeof(TestValue));
  memfree(NULL, v);
}
static void TestValueIncRef(TestValue* v) {
  AtomicAdd(&v->refs, 1);
}
static void TestValueDecRef(TestValue* v) {
  if (AtomicSub(&v->refs, 1) == 1)
    _TestValueFree(v);
}


typedef enum {
  TValue     = 0, // tag bits 00
  THamt      = 1, // tag bits 01
  TCollision = 2, // tag bits 10
} ObjType;

#define TAG_TYPE_MASK 3 // 0b11
#define OBJ_LEN_MAX   0x40000000  // 2^30

// Using last 29 bits of tag as len, we can store up to 1 073 741 824 values
// on one collision node. That's more than enough.

#define OBJ_HEAD \
  atomic_u32 refs; \
  u32        tag; /* bit 0-2 = type, bit 3-31 = len */
#define OBJ_INIT(obj, typ, len) ({ \
  (obj)->refs = 1; \
  (obj)->tag = ((len) << 2) | (typ); \
  (obj); \
})
#define OBJ_LEN(obj)           ( ((Obj*)(obj))->tag >> 2 )
#define OBJ_TYPE(obj)          ((ObjType)( ((Obj*)(obj))->tag & TAG_TYPE_MASK ))
#define OBJ_SET_TYPE(obj, typ) ({ (obj)->tag = ((obj)->tag & ~TAG_TYPE_MASK) | (typ); })


typedef struct Obj {
  OBJ_HEAD
} Obj;

typedef struct Value {
  OBJ_HEAD // TODO store first 30 bits of value hash in tag
  void* value;
} Value;

typedef struct Hamt { // used for both THamt and TCollision
  OBJ_HEAD
  u32  bmap;
  Obj* entries[]; // THamt:[THamt|TCollision|TValue], TCollision:[TValue]
} Hamt;


// free lists for each Hamt length class and for Value boxes
static Pool _hamtpool[BRANCHES];
static Pool _valpool;

// static NodeBitmap _empty_bitmap_node = {};
// static Node       _empty_map = {};

// static inline u32 hamt_mask(u32 hash, u32 shift) { return ((hash >> shift) & MASK); }
// static inline u32 bitpos(u32 hash, u32 shift) { return (u32)1 << hamt_mask(hash, shift); }
static inline u32 bitindex(u32 bitmap, u32 bit) { return popcount(bitmap & (bit - 1)); }


// steals reference of value
static Value* value_alloc(void* value) {
  auto e = PoolTake(&_valpool);
  Value* v;
  if (e != NULL) {
    // PoolEntry offset at Value.value
    v = (Value*)(((char*)e) - offsetof(Value,value));
  } else {
    v = (Value*)memalloc_raw(HAMT_MEM, sizeof(Value));
    OBJ_INIT(v, TValue, 0);
  }
  v->value = value;
  return v;
}

static Hamt* hamt_alloc(u32 len, ObjType typ) {
  assert((typ == THamt && len <= BRANCHES) || (typ == TCollision && len <= OBJ_LEN_MAX));
  assert(typ == THamt || typ == TCollision);
  if (len <= BRANCHES) {
    // try to take from freelist
    auto e = PoolTake(&_hamtpool[len]);
    if (e != NULL) {
      // PoolEntry offset at &Hamt.entries[0]
      auto h = (Hamt*)(((char*)e) - offsetof(Hamt,entries));
      OBJ_SET_TYPE(h, typ);
      assert(h->refs == 1);
      return h;
    }
  }
  auto h = (Hamt*)memalloc_raw(HAMT_MEM, sizeof(Hamt) + sizeof(Obj*) * len);
  OBJ_INIT(h, typ, len);
  return h;
}

static void value_release(Value*);
static void hamt_release(Hamt*);
static void obj_release(void*);
#define obj_retain(obj) ({ AtomicAdd(&(obj)->refs, 1); (obj); })

static void value_free(Value* v) {
  dlog("free Value %p", v);
  TestValueDecRef((TestValue*)v->value);
  v->refs = 1;
  PoolAdd(&_valpool, (PoolEntry*)(&v->value));
  // memfree(HAMT_MEM, v);
}

static void hamt_free(Hamt* h) {
  // dlog("free Hamt %p", h);
  for (u32 i = 0; i < OBJ_LEN(h); i++)
    obj_release(h->entries[i]);
  if (OBJ_LEN(h) <= BRANCHES) {
    h->refs = 1;
    PoolAdd(&_hamtpool[OBJ_LEN(h)], (PoolEntry*)&h->entries[0]);
  } else {
    // release large ones into shared memory manager
    memfree(HAMT_MEM, h);
  }
}

static void obj_release(void* obj) {
  if (OBJ_TYPE(obj) == TValue) {
    value_release((Value*)(obj));
  } else {
    hamt_release((Hamt*)(obj));
  }
}

inline static void value_release(Value* obj) {
  if (AtomicSub(&obj->refs, 1) == 1)
    value_free(obj);
}

inline static void hamt_release(Hamt* obj) {
  if (AtomicSub(&obj->refs, 1) == 1)
    hamt_free(obj);
}


static Obj* make_branch(u32 shift, u32 key1, u32 key2, Value* v1, Value* v2);


static Hamt* collision_add(Hamt* c, u32 key2, Value* v2) {
  dlog("collision_add c=%p", c);
  // auto c1 = (Hamt*)obj;
  // auto c2 = hamt_alloc(OBJ_LEN(c1) + 1, TCollision);
  // u32 i = 0;
  // for (; i < OBJ_LEN(c1); i++) {
  //   dlog("cpy %u", i);
  //   c2->entries[i] = obj_retain(c1->entries[i]);
  // }
  // dlog("set %u = v2 \"%s\"", i, ((TestValue*)v2->value)->str);
  // c2->entries[i] = (Obj*)v2;
  // newobj = (Obj*)c2;
  return obj_retain(c); // FIXME
}

// steals ref to v2
static Hamt* hamt_insert(Hamt* m, u32 shift, u32 key2, Value* v2) {
  assert(OBJ_TYPE(m) == THamt);
  u32 bitpos = 1u << ((key2 >> shift) & MASK);  // key bit position
  u32 bi     = bitindex(m->bmap, bitpos);  // bucket index

  dlog("hamt_insert level=%u, key2=%u, bitindex(bitpos 0x%x) => %u",
    shift / BITS, key2, bitpos, bi);

  if ((m->bmap & bitpos) == 0) {
    dlog("A");
    // empty; index bit not set in bmap. Set the bit and append value to entries list.
    // copy entries in m2 with +1 space for slot at bi
    Hamt* m2 = hamt_alloc(OBJ_LEN(m) + 1, THamt);
    m2->bmap = m->bmap | bitpos;
    // rsms: would memcpy + obj_retain without stores be faster here?
    // copy up to bi:
    for (u32 i = 0; i < bi; i++) {
      dlog("cpy m->entries[%u] => m2->entries[%u] (%p)", i, i, m->entries[i]);
      m2->entries[i] = obj_retain(m->entries[i]);
    }
    m2->entries[bi] = (Obj*)v2;
    // copy after bi:
    for (u32 i = bi+1, j = bi; j < OBJ_LEN(m); i++, j++) {
      dlog("cpy m->entries[%u] => m2->entries[%u] (%p)", j, i, m->entries[j]);
      m2->entries[i] = obj_retain(m->entries[j]);
    }
    return m2;
  }

  // An entry or branch occupies the slot; replace m2->entries[bi]
  // Note: Consider converting this to use iteration instead of recursion on hamt_insert
  Hamt* m2 = hamt_alloc(OBJ_LEN(m), THamt);
  m2->bmap = m->bmap;
  for (u32 i = 0; i < OBJ_LEN(m); i++)
    m2->entries[i] = obj_retain(m->entries[i]);
  Obj* obj = m->entries[bi]; // current entry
  Obj* newobj; // to be assigned as m2->entries[bi]

  switch (OBJ_TYPE(obj)) {

  case THamt:
    dlog("B1");
    // branch
    newobj = (Obj*)hamt_insert((Hamt*)obj, shift + BITS, key2, v2);
    break;

  case TCollision:
    dlog("B2");
    // existing collision (invariant: last branch; shift >= (BRANCHES - shift))
    newobj = (Obj*)collision_add((Hamt*)obj, key2, v2);
    break;

  case TValue: {
    dlog("B3");
    // A value already exists at this path
    Value* v1 = (Value*)obj;
    u32 key1 = TestValueHash((const TestValue*)v1->value);
    if (key1 == key2 &&
        TestValueEqual((const TestValue*)v1->value, (const TestValue*)v2->value))
    {
      // replace current value with v2 since they are equivalent
      // *resized--
      newobj = (Obj*)v2;
    } else {
      // branch
      newobj = make_branch(shift + BITS, key1, key2, obj_retain(v1), v2);
    }
    break;
  }

  } // switch

  // release the replaced object at m2->entries[bi]
  assert(obj != newobj);
  obj_release(obj);
  m2->entries[bi] = newobj;

  return m2;
}

// make_branch creates a HAMT at level with two entries v1 and v2, or a collision.
// steals refs to both v1 and v2
static Obj* make_branch(u32 shift, u32 key1, u32 key2, Value* v1, Value* v2) {
  // Compute the "path component" for key1 and key2 for level.
  // shift is the new level for the branch which is being created.
  u32 index1 = (key1 >> shift) & MASK;
  u32 index2 = (key2 >> shift) & MASK;

  // loop that creates new branches while key prefixes are shared.
  //
  // head and tail of a chain when there is subindex conflict,
  // representing intermediate branches.
  Hamt* mHead = NULL;
  Hamt* mTail = NULL;
  while (index1 == index2) {
    if (shift >= BRANCHES) {
      auto c = hamt_alloc(2, TCollision);
      c->entries[0] = (Obj*)v1;
      c->entries[1] = (Obj*)v2;
      if (mHead == NULL)
        return (Obj*)c;
      // We have an existing head we build in the loop above.
      // Add c to its tail and return the head.
      mTail->entries[0] = (Obj*)c;
      return (Obj*)mHead;
    }

    // append to tail of branch list
    auto m = hamt_alloc(1, THamt);
    m->bmap = 1u << index1;
    if (mTail) {
      // add to list
      mTail->entries[0] = (Obj*)m;
    } else {
      // begin list
      mHead = m;
    }
    mTail = m;

    shift += BITS;
    index1 = (key1 >> shift) & MASK;
    index2 = (key2 >> shift) & MASK;
  }

  // create map with v1,v2
  auto m = hamt_alloc(2, THamt);
  m->bmap = (1u << index1) | (1u << index2);
  if (index1 < index2) {
    m->entries[0] = (Obj*)v1;
    m->entries[1] = (Obj*)v2;
  } else {
    m->entries[0] = (Obj*)v2;
    m->entries[1] = (Obj*)v1;
  }

  if (mHead == NULL)
    return (Obj*)m;

  // We have an existing head we build in the loop above.
  // Add m to its tail and return the head.
  mTail->entries[0] = (Obj*)m;
  return (Obj*)mHead;
}


static const Value* nullable hamt_lookup(Hamt* m, u32 key, const void* lookupv) {
  assert(OBJ_TYPE(m) == THamt);
  u32 shift = 0;
  while (1) {
    // Check if index bit is set in bitmap
    u32 bitpos = 1u << ((key >> shift) & MASK);
    if ((m->bmap & bitpos) == 0)
      return NULL;

    // Compare to value at m.entries[bi]
    // where bi is the bucket index by mapping index bit -> bucket index.
    Obj* obj = m->entries[bitindex(m->bmap, bitpos)];
    switch (OBJ_TYPE(obj)) {
      case THamt: {
        dlog("%p THamt", obj);
        m = (Hamt*)obj;
        break;
      }
      case TCollision: {
        dlog("%p TCollision", obj);
        panic("TODO TCollision");
        //for _, v1 := range e {
        //  if v1.Equal(v) {
        //    return v1
        //  }
        //}
        return NULL;
      }
      case TValue: {
        dlog("%p TValue", obj);
        Value* v1 = (Value*)obj;
        if (TestValueEqual(v1->value, (const TestValue*)lookupv))
          return v1;
        return NULL;
      }
    }
    shift += BITS;
  }
  UNREACHABLE;
}

typedef Str(*ValueReprFunc)(Str s, const void* v);

static Str hamt_repr(Str s, Hamt* h, ValueReprFunc vreprf, int level) {
  dlog("%-*shamt_repr %p", level*4, "", h);
  s = str_appendfmt(s, "map (level %u)", level);
  for (u32 i = 0; i < OBJ_LEN(h); i++) {
    s = str_appendfmt(s, "\n%-*s#%d => ", level*2, "", i);
    Obj* obj = h->entries[i];
    // dlog("%-*sent %u => %p", level*4, "", i, obj);
    switch (OBJ_TYPE(obj)) {

      case THamt:
        dlog("%-*sTHamt", level*4, "");
        s = hamt_repr(s, (Hamt*)obj, vreprf, level + 1);
        break;

      case TCollision: {
        dlog("%-*sTCollision", level*4, "");
        auto c = (Hamt*)obj;
        s = str_appendcstr(s, "collision");
        for (u32 i = 0; i < OBJ_LEN(c); i++) {
          s = str_appendfmt(s, "\n%-*s  - Value ", level*2, "");
          s = vreprf(s, ((Value*)c->entries[i])->value);
        }
        break;
      }

      case TValue:
        dlog("%-*sTValue", level*4, "");
        s = vreprf(s, ((Value*)obj)->value);
        break;
    } // switch

  }
  return s;
}

static Hamt _empty_hamt = {
  .refs = 1,
  .tag = THamt, // len = 0
};
static Hamt* const _empty_hamtptr = &_empty_hamt;

Hamt* HamtNew() {
  return obj_retain(_empty_hamtptr);
  // Hamt* h = hamt_alloc(0);
  // h->map.bmap = 0;
  // return h;
}

void HamtRelease(Hamt* h) { hamt_release(h); }
Hamt* HamtRetain(Hamt* h) { return obj_retain(h); }

// map_repr returns a human-readable, printable string representation
static Str HamtRepr(Str s, Hamt* h, ValueReprFunc vreprf) {
  assert(OBJ_TYPE(h) == THamt);
  return hamt_repr(s, h, vreprf, 1);
}

// ------------------------------------------------------------------------------------------------

// steals ref to v
Hamt* HamtWith(Hamt* h, TestValue* value) {
  u32 key = TestValueHash(value);
  // TODO: consider mutating h when h.refs==1
  auto v = value_alloc(value);
  return hamt_insert(h, 0, key, v);
}

// steals ref to v, replaces *h
void HamtSet(Hamt** h, TestValue* value) {
  auto h2 = HamtWith(*h, value);
  HamtRelease(*h);
  *h = h2;
}

// returns borrwed ref to value, or NULL if not found
const TestValue* nullable HamtGet(Hamt* h, TestValue* lookupv) {
  u32 key = TestValueHash(lookupv);
  const Value* v = hamt_lookup(h, key, lookupv);
  if (v == NULL)
    return NULL;
  TestValueIncRef((TestValue*)v->value);
  TestValueDecRef((TestValue*)lookupv);
  return (const TestValue*)v->value;
}

// ------------------------------------------------------------------------------------------------
#if R_UNIT_TEST_ENABLED

static Str TestValueRepr(Str s, const TestValue* v) {
  return str_appendfmt(s, "TestValue(%s)", v->str);
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


R_UNIT_TEST(Hamt, {
  Str tmpstr = str_new(128);

  #define REPR(h) \
    (tmpstr = HamtRepr(str_setlen(tmpstr, 0), (h), (ValueReprFunc)TestValueRepr))

  { // test: basics
    Hamt* h = HamtNew();
    assert(h != NULL);
    auto v = MakeTestValue("1");
    v->str = "hello";
    h = HamtWith(h, v);
    dlog("%s", REPR(h));

    auto v2 = HamtGet(h, v);
    assert(v2 != NULL);
    assert(v2 == v);
    assert(strcmp(v->str, "hello") == 0);
    HamtRelease(h);
  }

  { // test: building trees
    Hamt* h = HamtNew();

    // key
    fprintf(stderr, "\n");
    auto v = MakeTestValue("1/2/3/4"); // 00100_00011_00010_00001 (LE)
    HamtSet(&h, v);
    dlog("%s", REPR(h));

    // cause a branch to be forked
    fprintf(stderr, "\n");
    v = MakeTestValue("1/2/1"); // 00001_00010_00001 (LE)
    HamtSet(&h, v);
    dlog("%s", REPR(h));

    // cause a collision; converts a value into a collision branch
    v = MakeTestValue("1/2/1");
    v->str = "1/2/1 (B)";
    HamtSet(&h, v);
    dlog("%s", REPR(h));

    // cause another collision; adds to existing collision set
    v = MakeTestValue("1/3");
    // v->str = "1/2/1 (C)";
    HamtSet(&h, v);
    dlog("%s", REPR(h));

    fprintf(stderr, "\n");
    HamtRelease(h);
  }

  fprintf(stderr, "\n");
});

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
