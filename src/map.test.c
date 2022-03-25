#include "coimpl.h"
#include "map.c"

// #include <stdlib.h>
// #include <stdio.h>

DEF_TEST(hmap) {
  // #define tlog dlog
  #define tlog(...) ((void)0)
  #define S SSLICE

  tlog("testing SMap");

  static u8 membuf[4096*2];
  Mem mem = mem_mkalloc_buf(membuf, sizeof(membuf));

  static SSlice samples[] = {
    S("i32"), S("div"), S("cmpgt"), S("and"), S("add"), S("brz"), S("brnz"), S("cmpeq"),
    S("cmplt"), S("i1"), S("fun"), S("i16"), S("move"), S("i8"), S("i64"), S("mod"),
    S("loadk"), S("shrs"), S("or"), S("mul"), S("shl"), S("ret"), S("sub"), S("shru"),
    S("xor"),
  };
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

  // smap_lookup
  tlog("find all");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_lookup(&m, samples[i]);
    assertf(vp != NULL, "vp=NULL  key=\"%s\"", samples[i].p);
    assertf(*vp == i,   "vp=%zu != i=%zu  key=\"%s\"", *vp, i, samples[i].p);
  }

  // smap_del
  tlog("delete all");
  for (usize i = 0; i < nsamples; i++) {
    bool ok = smap_del(&m, samples[i]);
    assertf(ok, "smap_del \"%s\"", samples[i].p);
  }

  // smap_lookup
  tlog("find none");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_lookup(&m, samples[i]);
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
    assert(hmap_citnext(&m, &e)); // iterator yields an entry
    uintptr* vp = assertnotnull(smap_lookup(&m, e->key)); // lookup finds an entry ...
    asserteq(*vp, e->value); // ... with matching value
  }
  assert(hmap_citnext(&m, &e) == false); // iterator is exhausted

  // clear
  tlog("hmap_clear");
  hmap_clear(&m);

  // smap_lookup
  tlog("find none");
  for (usize i = 0; i < nsamples; i++) {
    uintptr* vp = smap_lookup(&m, samples[i]);
    assertf(vp == NULL, "vp!=NULL  key=\"%s\"", samples[i].p);
  }

  hmap_dispose(&m);
}
