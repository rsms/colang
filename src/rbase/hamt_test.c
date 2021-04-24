#include "rbase.h"
#include "hamt.h"

#define XXH_INLINE_ALL
#include <xxhash/xxhash.h>

// define to silence all dlog messages for these tests
#define SILENCE_LOG

#if R_UNIT_TEST_ENABLED


#ifdef SILENCE_LOG
  #undef dlog
  #define dlog(...) do{}while(0)
#endif


// TestValue is a user-defined type
typedef struct TestValue {
  atomic_u32  refs;
  HamtUInt    key;
  const char* str;
} TestValue;

static HamtUInt TestValueKey(HamtCtx* ctx, const void* v) {
  return ((const TestValue*)v)->key;
  //return hash_fnv1a32((const u8*)v, strlen(v));
}

static bool TestValueEqual(HamtCtx* ctx, const void* a, const void* b) {
  //dlog("TestValueEqual %p == %p", a, b);
  return strcmp(((const TestValue*)a)->str, ((const TestValue*)b)->str) == 0;
}

static void TestValueFree(HamtCtx* ctx, void* vp) {
  TestValue* v = (TestValue*)vp;
  // dlog("_TestValueFree %u \"%s\"", v->key, v->str);
  memset(v, 0, sizeof(TestValue));
  memfree(NULL, v);
}

static Str TestValueRepr(Str s, const void* vp) {
  auto v = (const TestValue*)vp;
  return str_appendfmt(s,
    (sizeof(HamtUInt) == 8 ? "TestValue(0x%llX \"%s\")" : "TestValue(0x%X \"%s\")"),
    v->key, v->str);
}

// MakeTestValue takes a string of slash-separated integers and builds a Value
// where each integer maps to one level of branching in CHAMP.
//
// For instance, the key "1/2/3/4" produces the key:
//   0b00100_00011_00010_00001
//         4     3     2     1
//
static TestValue* MakeTestValue(const char* str) {
  HamtUInt key = 0;
  u32 shift = 0;
  StrSlice slice = {};
  while (str_splitcstr(&slice, '/', str)) {
    u32 index;
    if (!parseu32(slice.p, slice.len, 10, &index)) {
      dlog("warning: ignoring non-numeric part: \"%.*s\"", (int)slice.len, slice.p);
      continue;
    }
    key |= index << shift;
    shift += HAMT_BITS;
  }
  auto v = memalloct(NULL, TestValue);
  v->refs = 1;
  v->key = key;
  v->str = str;
  return v;
}

#ifndef SILENCE_LOG
static Str tmpstr = NULL;
#endif

static HamtCtx ctx = {
  // required
  .entkey  = TestValueKey,
  .enteq   = TestValueEqual,
  .entfree = TestValueFree,
  // optional
  .entrepr = TestValueRepr,
};

#define REPR(h) \
  (tmpstr = hamt_repr( \
    (h), str_setlen((tmpstr == NULL ? str_new(128) : tmpstr), 0), /*pretty*/true))

#define REPR_VAL(testval) \
  (tmpstr = TestValueRepr(str_setlen(tmpstr, 0), (testval)))


R_UNIT_TEST(HamtBasics, {
  Hamt h = hamt_new(&ctx);
  assert(h.root != NULL);
  auto v = MakeTestValue("1");
  v->str = "hello";
  bool didadd;
  auto h1 = h;
  h = hamt_with(h, v, &didadd);
  assert(h.root != h1.root);
  assert(didadd == true);
  dlog("%s", REPR(h));

  auto v2 = (TestValue*)hamt_getp(h, v);
  assert(v2 != NULL);
  assert(v2 == v);
  assert(strcmp(v->str, "hello") == 0);
  hamt_release(h);
})


R_UNIT_TEST(HamtBuildTrees, {
  Hamt h = hamt_new(&ctx);

  // key
  dlog("\n");
  auto v = MakeTestValue("1/2/3/4"); // 00100_00011_00010_00001 (LE)
  hamt_set(&h, v);
  dlog("%s", REPR(h));

  // cause a branch to be forked
  dlog("\n");
  v = MakeTestValue("1/2/1"); // 00001_00010_00001 (LE)
  hamt_set(&h, v);
  assert(hamt_getp(h, v) == v);
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

  // replace equivalent value in collision node
  v = MakeTestValue("1/2/1");
  v->str = "1/2/1 (C)";
  hamt_set(&h, v);
  dlog("%s", REPR(h));

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
  assert(hamt_del(&h, v));
  assert(hamt_getp(h, v) == NULL);
  TestValueFree(h.ctx, v);
  dlog("%s", REPR(h));

  // remove remaining values on the same branch
  v = MakeTestValue("1/3/1");
  assert(hamt_del(&h, v));
  assert(hamt_getp(h, v) == NULL);
  TestValueFree(h.ctx, v);

  v = MakeTestValue("1/3/3");
  assert(hamt_del(&h, v));
  assert(hamt_getp(h, v) == NULL);
  TestValueFree(h.ctx, v);
  dlog("%s", REPR(h));

  // remove value in collision node with multiple values
  v = MakeTestValue("1/2/1");
  v->str = "1/2/1 (B)";
  assert(hamt_del(&h, v));
  assert(hamt_getp(h, v) == NULL);
  TestValueFree(h.ctx, v);
  dlog("%s", REPR(h));

  // remove remaining values in a collision node
  v = MakeTestValue("1/2/1");
  assert(hamt_del(&h, v));
  assert(hamt_getp(h, v) == NULL);
  TestValueFree(h.ctx, v);
  dlog("%s", REPR(h));

  dlog("\n");
  hamt_release(h);
})


R_UNIT_TEST(HamtRemoveCollisions, {
  // removal in collision
  Hamt h = hamt_new(&ctx);

  auto A = MakeTestValue("1/2/3/4");
  A->str = "1/2/3/4 (A)";
  hamt_set(&h, A);

  auto B = MakeTestValue("1/2/3/4");
  B->str = "1/2/3/4 (B)";
  hamt_set(&h, B);

  auto C = MakeTestValue("1/2/3/4");
  C->str = "1/2/3/4 (C)";
  hamt_set(&h, C);

  hamt_set(&h, MakeTestValue("1/2/4"));
  dlog("%s", REPR(h));
  asserteq(hamt_count(h), 4);
  assert(!hamt_empty(h));

  assert(hamt_del(&h, A));
  dlog("%s", REPR(h));

  assert(hamt_del(&h, B));
  dlog("%s", REPR(h));

  assert(hamt_del(&h, C));
  dlog("%s", REPR(h));

  asserteq(hamt_count(h), 1);
  assert(!hamt_empty(h));

  auto v = MakeTestValue("1/2/4");
  assert(hamt_del(&h, v));
  TestValueFree(h.ctx, v);

  asserteq(hamt_count(h), 0);
  assert(hamt_empty(h));

  hamt_release(h);
})


static void HamtIterator() {
  auto A = MakeTestValue("1/2/2/1"); A->str = "1/2/2/1 (A)"; // collision w/ B
  auto B = MakeTestValue("1/2/2/1"); B->str = "1/2/2/1 (B)"; // collision w/ A
  // these test values should be in the order we expect them during iteration
  TestValue* values[] = {
    MakeTestValue("1/1/1"),
    MakeTestValue("1/2"),
    MakeTestValue("1/2/1"),
    MakeTestValue("1/2/2"),
    A,
    B,
    MakeTestValue("1/2/3"),
    MakeTestValue("1/3"),
  };

  Hamt h = hamt_new(&ctx);
  for (int i = 0; i < countof(values); i++)
    hamt_set(&h, values[i]);

  dlog("%s", REPR(h));

  // iterate and verify that we get all values and in the correct order
  HamtIter it;
  hamt_iter_init(h, &it);
  const TestValue* entry = NULL;
  u32 len = 0;
  while (hamt_iter_next(&it, (const void**)&entry) && len < countof(values)) {
    TestValue* expectentry = values[len];
    if (expectentry != entry) {
      errlog("expected %s; got %s",
        TestValueRepr(str_new(0), expectentry), TestValueRepr(str_new(0), entry));
      asserteq(entry, expectentry);
    }
    //dlog("it>> entry %p %s", entry, REPR_VAL(entry));
    entry = NULL;
    len++;
  }
  asserteq(countof(values), len);
  hamt_release(h);
}

R_UNIT_TEST(HamtIterator, {HamtIterator();})


// randkey returns a random Hamt key
static HamtUInt randkey() {
  long n = random();
  return (HamtUInt)n;
}

static void TestValueReset(HamtCtx* ctx, void* vp) {
  TestValue* v = (TestValue*)vp;
  // dlog("TestValueReset %x", v->key);
  memset(v, 0, sizeof(TestValue));
}

static void shuffle_array(void** a, size_t len) {
  if (len > 1) {
    for (size_t i = 0; i < len - 1; i++) {
      size_t j = i + rand() / (RAND_MAX / (len - i) + 1);
      void* v = a[j];
      a[j] = a[i];
      a[i] = v;
    }
  }
}

static inline void HamtFuzzTest(unsigned int randseed) {
  HamtCtx ctx = {
    // required
    .entkey  = TestValueKey,
    .enteq   = TestValueEqual,
    .entfree = TestValueReset, // <-- since we use heap-allocated TestValues
    // optional
    .entrepr = TestValueRepr,
  };
  Hamt h = hamt_new(&ctx);

  TestValue values[1000];
  char strs[countof(values)][17]; // "0000000000000000"-"ffffffffffffffff"

  // seed the pseudo-random number generator to make tests predictable
  srandom(randseed);

  // insert random entries
  for (u32 i = 0; i < countof(values); i++) {
    auto v = &values[i];
    v->refs = 1;
    v->key = randkey();
    snprintf(strs[i], sizeof(strs[0]), "%x", v->key);
    v->str = strs[i];
    // dlog("val %s", strs[i]);
    hamt_set(&h, v);
  }

  // dlog("\n");
  // dlog("%s", REPR(h));
  // dlog("\n");

  // lookup random entries which should all fail (not found)
  for (u32 i = 0; i < 100; i++) {
    TestValue v;
    v.str = ""; // unused; will never entcmp
    bool keycollision = true;
    while (keycollision) {
      v.key = randkey();
      keycollision = false;
      for (u32 i = 0; i < countof(values); i++) {
        if (values[i].key == v.key) {
          keycollision = true;
          break;
        }
      }
    }
    assert(hamt_getp(h, &v) == NULL);
  }

  // lookup all known values
  for (u32 i = 0; i < countof(values); i++) {
    auto v = &values[i];
    asserteq(hamt_getp(h, v), v);
  }

  // remove values in random order
  TestValue* randvals[countof(values)];
  for (u32 i = 0; i < countof(values); i++)
    randvals[i] = &values[i];
  shuffle_array((void**)randvals, countof(values));
  for (u32 i = 0; i < countof(values); i++) {
    auto v = randvals[i];
    assert(hamt_del(&h, v));
    asserteq(hamt_count(h), countof(values) - i - 1);
    // TestValueReset should have been called when node_release decremented
    // the refcount of the value. TestValueReset zeroes the value, so we check for that:
    assert(v->key == 0);
    assert(v->str == NULL);
  }

  assert(hamt_empty(h));
  hamt_release(h);
}

R_UNIT_TEST(HamtFuzz, {
  // spend 100ms on fuzzying
  u64 max_time_spend_ns = 100*1000000;
  auto starttm = nanotime();
  for (u32 i = 1; nanotime() - starttm < max_time_spend_ns; i++) {
    HamtFuzzTest(/* randseed */ i);
  }
})

R_UNIT_TEST(xxHash, {
  const char* buffer = "hello";
  size_t size = strlen(buffer);

  XXH32_hash_t seed32 = 1;

  { // oneshot
    XXH32_hash_t hash32 = XXH32(buffer, size, seed32);
    dlog("hash32 oneshot: %x", hash32);
  }

  { // state approach piece by piece
    XXH32_state_t* hstate = XXH32_createState();
    XXH32_reset(hstate, seed32);
    size_t len1 = size / 2;
    size_t len2 = size - len1;
    XXH32_update(hstate, buffer, len1);
    XXH32_update(hstate, &buffer[len1], len2);
    XXH32_hash_t hash32 = XXH32_digest(hstate);
    dlog("hash32 state:  %x", hash32);
    XXH32_freeState(hstate);
  }
})



#endif /* R_UNIT_TEST_ENABLED */
