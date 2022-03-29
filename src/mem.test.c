#include "coimpl.h"
#include "test.c"
#include "mem.c"

#ifndef CO_NO_LIBC
DEF_TEST(mem_libc_allocator) {
  Mem m = mem_mkalloc_libc();

  void* p = mem_alloc(m, 123);
  assertnotnull(p);

  p = mem_resize(m, p, 123, 456);
  assertnotnull(p);

  mem_free(m, p, 456);
}
#endif


DEF_TEST(mem_bufalloc) {
  u8 buf[512];
  Mem m = mem_mkalloc_buf(buf, sizeof(buf));

  void* p = mem_alloc(m, 123);
  assertnotnull(p);

  p = mem_resize(m, p, 123, 456);
  assertnotnull(p);

  mem_free(m, p, 456);
}


DEF_TEST(mem_ctx) {
  u8 buf[512];
  Mem m = mem_mkalloc_buf(buf, sizeof(buf));
  assert(m.a != &_mem_null_alloc);
  assert(mem_ctx().a == &_mem_null_alloc);
  Mem prev_mem = mem_ctx_set(m);
  assert(mem_ctx().a == m.a);
  mem_ctx_set(prev_mem);
}


// mem_ctx_scope
#if __has_attribute(cleanup)

  static void test_mem_ctx_return(Mem m) {
    assert(mem_ctx().a != m.a);
    mem_ctx_scope(m) {
      assert(mem_ctx().a == m.a);
      // return before the for loop's update statement is run, which calls mem_ctx_set
      // to restore the allocator context. Instead the _mem_ctx_scope_cleanup cleanup
      // function should be run.
      return;
    }
  }

  DEF_TEST(mem_ctx_scope) {
    u8 buf[512];
    Mem m = mem_mkalloc_buf(buf, sizeof(buf));

    // leave scope "normally"
    assert(mem_ctx().a == &_mem_null_alloc);
    mem_ctx_scope(m) {
      void* p = memalloc(8);
      assert(p == buf + MEM_BUFALLOC_OVERHEAD);
      assert(mem_ctx().a == m.a);
    }
    assert(mem_ctx().a == &_mem_null_alloc);

    // leave scope "prematurely" by returning from it
    test_mem_ctx_return(m);
    assert(mem_ctx().a == &_mem_null_alloc);
  }

#endif
