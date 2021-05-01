#include <rbase/rbase.h>
#include "pos.h"

void posmap_init(PosMap* pm, Mem nullable mem) {
  ArrayInitWithStorage(&pm->a, pm->a_storage, countof(pm->a_storage));
  // the first slot is used to return NULL in pos_source for unknown positions
  pm->a.v[0] = NULL;
  pm->a.len++;
}

void posmap_dispose(PosMap* pm) {
  ArrayFree(&pm->a, pm->mem);
}

u32 posmap_origin(PosMap* pm, void* origin) {
  assertnotnull(origin);
  for (u32 i = 0; i < pm->a.len; i++) {
    if (pm->a.v[i] == origin)
      return i;
  }
  u32 i = pm->a.len;
  ArrayPush(&pm->a, origin, pm->mem);
  return i;
}

R_UNIT_TEST(pos) {
  PosMap pm;
  posmap_init(&pm, NULL);
  uintptr_t source1 = 1;
  uintptr_t source2 = 2;

  // allocate origins for two sources
  auto o1 = posmap_origin(&pm, (void*)source1);
  auto o2 = posmap_origin(&pm, (void*)source2);
  assertop(o1,<,o2);

  // should get the same origin values on subsequent queries
  asserteq(o1, posmap_origin(&pm, (void*)source1));
  asserteq(o2, posmap_origin(&pm, (void*)source2));

  // make some positions (origin, line, column)
  auto p1_1_1 = pos_make(o1, 1, 1);
  auto p1_1_9 = pos_make(o1, 1, 9);
  auto p1_7_3 = pos_make(o1, 7, 3);

  auto p2_1_1 = pos_make(o2, 1, 1);
  auto p2_1_9 = pos_make(o2, 1, 9);
  auto p2_7_3 = pos_make(o2, 7, 3);

  // lookup source
  asserteq((uintptr_t)pos_source(&pm, p1_1_1), source1);
  asserteq((uintptr_t)pos_source(&pm, p1_1_9), source1);
  asserteq((uintptr_t)pos_source(&pm, p1_7_3), source1);
  asserteq((uintptr_t)pos_source(&pm, p2_1_1), source2);
  asserteq((uintptr_t)pos_source(&pm, p2_1_9), source2);
  asserteq((uintptr_t)pos_source(&pm, p2_7_3), source2);

  // make sure line and column getters works as expected
  asserteq(pos_line(p1_1_1), 1); asserteq(pos_col(p1_1_1), 1);
  asserteq(pos_line(p1_1_9), 1); asserteq(pos_col(p1_1_9), 9);
  asserteq(pos_line(p1_7_3), 7); asserteq(pos_col(p1_7_3), 3);
  asserteq(pos_line(p2_1_1), 1); asserteq(pos_col(p2_1_1), 1);
  asserteq(pos_line(p2_1_9), 1); asserteq(pos_col(p2_1_9), 9);
  asserteq(pos_line(p2_7_3), 7); asserteq(pos_col(p2_7_3), 3);

  // known
  asserteq(pos_isknown(NoPos), false);
  assert(pos_isknown(p1_1_1));
  assert(pos_isknown(p1_1_9));
  assert(pos_isknown(p1_7_3));
  assert(pos_isknown(p2_1_1));
  assert(pos_isknown(p2_1_9));
  assert(pos_isknown(p2_7_3));

  // pos_isbefore
  assert(pos_isbefore(p1_1_1, p1_1_9)); // column 1 is before column 9
  assert(pos_isbefore(p1_1_9, p1_7_3)); // line 1 is before line 7
  assert(pos_isbefore(p1_7_3, p2_1_1)); // o1 is before o2
  assert(pos_isbefore(p1_1_1, p2_1_1)); // o1 is before o2
  assert(pos_isbefore(p2_1_1, p2_1_9)); // column 1 is before column 9
  assert(pos_isbefore(p2_1_9, p2_7_3)); // line 1 is before line 7

  // pos_isafter
  assert(pos_isafter(p1_1_9, p1_1_1)); // column 9 is after column 1
  assert(pos_isafter(p1_7_3, p1_1_9)); // line 7 is before line 1
  assert(pos_isafter(p2_1_1, p1_7_3)); // o2 is after o1
  assert(pos_isafter(p2_1_1, p1_1_1)); // o2 is after o1
  assert(pos_isafter(p2_1_9, p2_1_1)); // column 9 is before column 1
  assert(pos_isafter(p2_7_3, p2_1_9)); // line 7 is before line 1


  posmap_dispose(&pm);
  // exit(0);
}
