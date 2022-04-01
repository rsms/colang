#include "colib.h"

#if __has_builtin(__builtin_alloca)
  #undef alloca
  #define alloca __builtin_alloca
#elif !defined(CO_NO_LIBC)
  #include <alloca.h>
#endif

#define U32ArrayDUMP(a) ((void)0)
// #define U32ArrayDUMP(a) { \
//   printf("["); \
//   for (u32 i = 0; i < (a)->len; i++) \
//     printf(i?",%u":"%u", (a)->v[i]); \
//   printf("]\n"); \
// }

#define MAKE_STACK_ARRAY(ARRAY, values...) ({ \
  const __typeof__(*((ARRAY*)0)->v) vtmp__[] = { values }; \
  __typeof__(*((ARRAY*)0)->v)* CONCAT(_adata_, __LINE__) = alloca(sizeof(vtmp__)); \
  memcpy(CONCAT(_adata_, __LINE__), vtmp__, sizeof(vtmp__)); \
  ARRAY atmp__ = array_make(ARRAY, CONCAT(_adata_, __LINE__), sizeof(vtmp__)); \
  atmp__.len = countof(vtmp__); \
  atmp__; \
})

#define TEST_HEAD_U32Array \
  void* membuf[512]; \
  Mem m = mem_mkalloc_buf(membuf, sizeof(membuf)); \
  mem_ctx_set_scope(m);

#define ASSERT_U32ARRAY(a, expectvals...) { \
  u32 expect[] = { expectvals }; \
  asserteq((a)->len, countof(expect)); \
  assertf(memcmp((a)->v, expect, sizeof(expect)) == 0, "expected " #expectvals); \
}


DEF_TEST(array_push) {
  TEST_HEAD_U32Array;

  u32 storage[4];
  U32Array a; array_init(&a, storage, sizeof(storage));

  array_push(&a, 1);
  array_push(&a, 2);
  array_push(&a, 3);
  array_push(&a, 4);
  U32ArrayDUMP(&a);
  asserteq(a.len, 4);

  array_push(&a, 5);
  U32ArrayDUMP(&a);
  asserteq(a.len, 5);

  ASSERT_U32ARRAY(&a, 1,2,3,4,5);
}


DEF_TEST(array_append) {
  TEST_HEAD_U32Array;
  U32Array a = MAKE_STACK_ARRAY(U32Array, 1,2,3,4,5);

  u32 values[] = {6,7,8,9};
  assert(array_append(&a, values, countof(values)));
  U32ArrayDUMP(&a);
  asserteq(a.len, 9);
  ASSERT_U32ARRAY(&a, 1,2,3,4,5, 6,7,8,9);

  // this should fail to compile "incompatible pointer types":
  //u8 values2[] = {6,7,8,9};
  //array_append(&a, values2, countof(values2));
}


DEF_TEST(array_fill) {
  TEST_HEAD_U32Array;
  U32Array a = MAKE_STACK_ARRAY(U32Array, 1,2,3,4,5);

  assert(array_fill(&a, 4, 3, 8));
  U32ArrayDUMP(&a);
  ASSERT_U32ARRAY(&a, 1,2,3,4, 3,3,3,3,3,3,3,3);
}


DEF_TEST(array_splice) {
  TEST_HEAD_U32Array;
  U32Array a = MAKE_STACK_ARRAY(U32Array, 1,2,3,4,5,6,7,8,9);

  // splice(1,2) : [1,2,3,4 ...] => [1,4 ...]
  assert(array_splice(&a, 1, 2, 0, NULL));
  U32ArrayDUMP(&a);
  ASSERT_U32ARRAY(&a, 1, 4,5,6,7,8,9);

  // splice(1,0,{2,3}) : [1,4 ...] => [1,2,3,4 ...]
  u32 insert[] = {2,3};
  assert(array_splice(&a, 1, 0, countof(insert), insert));
  U32ArrayDUMP(&a);
  ASSERT_U32ARRAY(&a, 1, 2,3, 4,5,6,7,8,9);

  // splice(2,2,{33,44}) : [1,2,3,4 ...] => [1,2,33,44 ...]
  insert[0] = 33; insert[1] = 44;
  assert(array_splice(&a, 2, 2, countof(insert), insert));
  U32ArrayDUMP(&a);
  ASSERT_U32ARRAY(&a, 1,2, 33,44, 5,6,7,8,9);
}
