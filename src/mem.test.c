#include "coimpl.h"
#include "test.h"
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
