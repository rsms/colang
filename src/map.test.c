#include "colib.h"

// #define tlog dlog
#define tlog(...) ((void)0)
#define S strslice_make

static u8 membuf[4096*2];

static StrSlice samples[] = {
  S("i32"), S("div"), S("cmpgt"), S("and"),
  S("add"), S("brz"), S("brnz"), S("cmpeq"),
  S("cmplt"), S("i1"), S("fun"), S("i16"),
  S("move"), S("i8"), S("i64"), S("mod"),
  S("loadk"), S("shrs"), S("or"), S("mul"),
  S("shl"), S("ret"), S("sub"), S("shru"),
  S("xor"),
};


DEF_TEST(pmap) {
  tlog("testing hmap via PMap");
  static_assert(sizeof(uintptr) >= sizeof(usize), "");
  Mem mem = mem_mkalloc_buf(membuf, sizeof(membuf));
  usize nsamples = countof(samples);

  HMap m = {0};
  assertnotnull(pmap_init(&m, mem, nsamples, MAPLF_2)); // hint=1 so that it grows
  m.hash0 = 1234;

  // // log hash values
  // for (usize i = 0; i < nsamples; i++) {
  //   Hash h = hash_ptr2(&samples[i]);
  //   // Hash h = hash_ptr(&samples[i], m.hash0);
  //   usize slot = (usize)h % m.cap;
  //   printf("%16llx %4lu %-5s %p\n", (u64)h, slot, samples[i].p, &samples[i]);
  // }

  // add all
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(pmap_assign(&m, &samples[i]));
    *vp = i;
  }
  asserteq(m.len, nsamples);

  // find all
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(pmap_find(&m, &samples[i]));
    asserteq(*vp, i);
  }

  // remove all
  for (usize i = 0; i < nsamples; i++)
    assert(pmap_del(&m, &samples[i]));

  // find none
  for (usize i = 0; i < nsamples; i++)
    assertnull(pmap_find(&m, &samples[i]));

  // add all
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(pmap_assign(&m, &samples[i]));
    *vp = i;
  }

  // find all
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(pmap_find(&m, &samples[i]));
    asserteq(*vp, i);
  }

  // iterate over all
  const PMapEnt* e = assertnotnull(hmap_citstart(&m));
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(pmap_find(&m, e->key)); // find an entry ...
    asserteq(*vp, e->value); // ... with matching value
    if (i+1 < nsamples)
      assertf(hmap_citnext(&m, &e), "%zu", i); // iterator yields an entry
  }
  assert(hmap_citnext(&m, &e) == false); // iterator is exhausted

  // empty
  hmap_clear(&m);

  // find none
  for (usize i = 0; i < nsamples; i++)
    assertnull(pmap_find(&m, &samples[i]));

  hmap_dispose(&m);
}


DEF_TEST(hmap) {
  tlog("testing hmap via SMap");
  Mem mem = mem_mkalloc_buf(membuf, sizeof(membuf));
  usize nsamples = countof(samples);

  HMap m = {0};
  assertnotnull(smap_init(&m, mem, 1, MAPLF_2)); // hint=1 so that it grows
  m.hash0 = 1234;
  tlog("m.cap %u, m.gcap %u", m.cap, m.gcap);

  // // log hash values
  // for (usize i = 0; i < nsamples; i++) {
  //   Hash h = hash_mem(samples[i].p, samples[i].len, m.hash0);
  //   printf("%16llx %4llu %-5s\n", (u64)h, (u64)h % m.cap, samples[i].p);
  // }

  // smap_assign
  tlog("insert all");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_assign(&m, samples[i]);
    assertf(vp != NULL, "vp=NULL  key=\"%s\"", samples[i].p);
    *vp = i;
    assert(m.len == i+1);
  }
  asserteq(m.len, nsamples);

  // smap_find
  tlog("find all");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_find(&m, samples[i]);
    assertf(vp != NULL, "vp=NULL  key=\"%s\"", samples[i].p);
    assertf(*vp == i, "vp=%zu != i=%zu  key=\"%s\"", *vp, i, samples[i].p);
  }

  // smap_del
  tlog("delete all");
  for (usize i = 0; i < nsamples; i++) {
    bool ok = smap_del(&m, samples[i]);
    assertf(ok, "smap_del \"%s\"", samples[i].p);
  }

  // smap_find
  tlog("find none");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_find(&m, samples[i]);
    assertf(vp == NULL, "vp!=NULL  key=\"%s\"", samples[i].p);
  }

  // smap_assign (again)
  tlog("insert all");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(smap_assign(&m, samples[i]));
    *vp = i;
  }
  asserteq(m.len, nsamples);

  // iter
  tlog("smap_itstart & smap_itnext");
  // for (const SMapEnt* e = hmap_citstart(&m); hmap_citnext(&m, &e); )
  //   printf("%s=%zu\t", e->key.p, (usize)e->value);
  // printf("\n");
  const SMapEnt* e = assertnotnull(hmap_citstart(&m));
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = assertnotnull(smap_find(&m, e->key)); // find an entry ...
    asserteq(*vp, e->value); // ... with matching value
    if (i+1 < nsamples)
      assert(hmap_citnext(&m, &e)); // iterator yields an entry
  }
  assert(hmap_citnext(&m, &e) == false); // iterator is exhausted

  // clear
  tlog("hmap_clear");
  hmap_clear(&m);

  // smap_find
  tlog("find none");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_find(&m, samples[i]);
    assertf(vp == NULL, "vp!=NULL  key=\"%s\"", samples[i].p);
  }

  hmap_dispose(&m);
}
