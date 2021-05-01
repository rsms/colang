#include <rbase/rbase.h>
#include "array.h"

#define ARRAY_CAP_STEP 32 /* copied from array.c */

R_UNIT_TEST(array_heap) {
  // starts empty and immediately becomes fully heap allocated
  Array a = Array_INIT;
  ArrayPush(&a, (void*)1, NULL); // visits ArrayGrow's "onheap" branch
  ArrayPush(&a, (void*)2, NULL);
  ArrayPush(&a, (void*)3, NULL);

  asserteq(a.len, 3);
  asserteq(a.cap, ARRAY_CAP_STEP);
  asserteq((uintptr_t)a.v[0], 1);
  asserteq((uintptr_t)a.v[1], 2);
  asserteq((uintptr_t)a.v[2], 3);

  asserteq(ArrayIndexOf(&a, (void*)2), 1);
  asserteq(ArrayIndexOf(&a, (void*)4), -1);

  asserteq((uintptr_t)ArrayPop(&a), 3);
  asserteq((uintptr_t)ArrayPop(&a), 2);
  asserteq((uintptr_t)ArrayPop(&a), 1);

  asserteq(a.len, 0);
  asserteq(a.cap, ARRAY_CAP_STEP);
  ArrayFree(&a, NULL);
}

R_UNIT_TEST(array_stack_to_heap) {
  // initially stack allocated, then moves to heap
  Array a; void* storage[2];
  ArrayInitWithStorage(&a, storage, 2);
  asserteq(a.onstack, true);
  ArrayPush(&a, (void*)1, NULL);
  asserteq(a.onstack, true);
  ArrayPush(&a, (void*)2, NULL);
  asserteq(a.onstack, true);
  ArrayPush(&a, (void*)3, NULL);  // visits ArrayGrow's "move stack to heap" branch
  asserteq(a.onstack, false); // should have moved to heap

  asserteq(a.len, 3);
  asserteq(a.cap, ARRAY_CAP_STEP);
  asserteq((uintptr_t)a.v[0], 1);
  asserteq((uintptr_t)a.v[1], 2);
  asserteq((uintptr_t)a.v[2], 3);
  asserteq((uintptr_t)ArrayPop(&a), 3);
  asserteq((uintptr_t)ArrayPop(&a), 2);
  asserteq((uintptr_t)ArrayPop(&a), 1);
  asserteq(a.len, 0);
  asserteq(a.cap, ARRAY_CAP_STEP);
  ArrayFree(&a, NULL);
}

R_UNIT_TEST(array_copy) {
  Array a = Array_INIT;
  for (intptr_t i = 0; i < 10; i++) {
    ArrayPush(&a, (void*)i, NULL);
  }
  // copy to an empty array. Causes initial, exact allocation
  Array a2 = Array_INIT;
  ArrayCopy(&a2, 0, a.v, a.len, NULL);
  asserteq(a2.len, 10);
  asserteq(a2.cap, 10); // should be exact after copy into empty array, not ARRAY_CAP_STEP
  ArrayPush(&a2, (void*)10, NULL);
  asserteq(a2.cap, align2(11, ARRAY_CAP_STEP)); // should have grown

  // copy to a non-empty array. Causes growth
  u32 nitems = (a2.cap - a2.len) + 1;
  auto items = (void**)memalloc(NULL, nitems * sizeof(void*));
  auto len1 = a2.len;
  ArrayCopy(&a2, len1, items, nitems, NULL);
  asserteq(a2.len, len1 + nitems);
  memfree(NULL, items);

  ArrayFree(&a2, NULL);
  ArrayFree(&a, NULL);
}

R_UNIT_TEST(array_remove) {
  Array a = Array_INIT;
  // a.v = [0 1 2 3 4 5 6 7 8 9]
  for (intptr_t i = 0; i < 10; i++) {
    ArrayPush(&a, (void*)i, NULL);
  }
  for (intptr_t i = 0; i < 10; i++) {
    asserteq(a.v[i], (void*)i);
  }
  asserteq(a.len, 10);

  // delete in middle
  // [0 1 2 3 4 5 6 7 8 9] => [0 1 6 7 8 9]
  //      ~~~~~~~
  Array a2 = Array_INIT;
  ArrayCopy(&a2, 0, a.v, a.len, NULL);
  asserteq(a2.len, 10);
  ArrayRemove(&a2, 2, 4);
  asserteq(a2.len, 6);
  asserteq(a2.v[0], (void*)0);
  asserteq(a2.v[1], (void*)1);
  asserteq(a2.v[2], (void*)6);
  asserteq(a2.v[3], (void*)7);
  asserteq(a2.v[4], (void*)8);
  asserteq(a2.v[5], (void*)9);

  // delete at beginning
  // [0 1 2 3 4 5 6 7 8 9] => [4 5 6 7 8 9]
  //  ~~~~~~~
  a2.len = 0;
  ArrayCopy(&a2, 0, a.v, a.len, NULL);
  asserteq(a2.len, 10);
  ArrayRemove(&a2, 0, 4);
  asserteq(a2.len, 6);
  asserteq(a2.v[0], (void*)4);
  asserteq(a2.v[1], (void*)5);
  asserteq(a2.v[2], (void*)6);
  asserteq(a2.v[3], (void*)7);
  asserteq(a2.v[4], (void*)8);
  asserteq(a2.v[5], (void*)9);

  // delete at end
  // [0 1 2 3 4 5 6 7 8 9] => [0 1 2 3 4 5]
  //              ~~~~~~~
  a2.len = 0;
  ArrayCopy(&a2, 0, a.v, a.len, NULL);
  asserteq(a2.len, 10);
  ArrayRemove(&a2, 6, 4);
  asserteq(a2.len, 6);
  asserteq(a2.v[0], (void*)0);
  asserteq(a2.v[1], (void*)1);
  asserteq(a2.v[2], (void*)2);
  asserteq(a2.v[3], (void*)3);
  asserteq(a2.v[4], (void*)4);
  asserteq(a2.v[5], (void*)5);

  ArrayFree(&a2, NULL);
  ArrayFree(&a, NULL);
}


