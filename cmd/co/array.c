#include "coimpl.h"

#ifdef CO_WITH_LIBC
  #include <stdlib.h> // for qsort_r
#endif

// ARRAY_CAP_STEP defines a power-of-two which the cap must be aligned to.
// This is used to round up growth. I.e. grow by 60 with a cap of 32 would increase the cap
// to 96 (= 32 + (align2(60, ARRAY_CAP_STEP=32) = 64)).
#define ARRAY_CAP_STEP 32

typedef struct SortCtx {
  array_sort_fun f;
  void*          userdata;
} SortCtx;


static int _sort(void* ctx, const void* s1p, const void* s2p) {
  return ((SortCtx*)ctx)->f(
    *((const void**)s1p),
    *((const void**)s2p),
    ((SortCtx*)ctx)->userdata
  );
}

void array_sort(Array* a, array_sort_fun f, void* userdata) {
  #ifdef CO_WITH_LIBC
    SortCtx ctx = { f, userdata };
    qsort_r(a->v, a->len, sizeof(void*), &ctx, &_sort);
  #else
    assert(!"array_sort not implemented");
  #endif
}

error array_grow(Array* a, u32 addl, Mem mem) {
  usize reqcap = (usize)a->cap + addl;
  usize cap = ALIGN2(reqcap, ARRAY_CAP_STEP);
  usize z = array_size(sizeof(void*), cap);
  if (z == USIZE_MAX || cap > U32_MAX)
    return err_overflow;

  if (!a->onstack || a->v == NULL) {
    void* v = memrealloc(mem, a->v, z);
    if (!v)
      return err_nomem;
    a->v = v;
    a->cap = (u32)cap;
    return 0;
  }

  // moving array from stack to heap
  void** v = (void**)memalloc(mem, z);
  if (!v)
    return err_nomem;
  memcpy(v, a->v, sizeof(void*) * a->len);
  a->v = v;
  a->cap = (u32)cap;
  a->onstack = false;
  return 0;
}

isize array_indexof(Array* a, void* nullable entry) {
  for (u32 i = 0; i < a->len; i++) {
    if (a->v[i] == entry)
      return (isize)i;
  }
  return -1;
}

isize array_lastindexof(Array* a, void* nullable entry) {
  for (u32 i = a->len; i--; ) {
    if (a->v[i] == entry)
      return (isize)i;
  }
  return -1;
}

void array_remove(Array* a, u32 start, u32 count) {
  assert(start + count <= a->len);
  // array_remove( [0 1 2 3 4 5 6 7] start=2 count=3 ) => [0 1 5 6 7]
  //
  for (u32 i = start + count; i < a->len; i++) {
    a->v[i - count] = a->v[i];
  }
  // [0 1 2 3 4 5 6 7]   a->v[5-3] = a->v[5]  =>  [0 1 5 3 4 5 6 7]
  //      ^     i
  //
  // [0 1 2 3 4 5 6 7]   a->v[6-3] = a->v[6]  =>  [0 1 5 6 4 5 6 7]
  //        ^     i
  //
  // [0 1 2 3 4 5 6 7]   a->v[7-3] = a->v[7]  =>  [0 1 5 6 7 5 6 7]
  //          ^     i
  //
  // len -= count                             =>  [0 1 5 6 7]
  a->len -= count;
}


// array_copy copies src of srclen to a, starting at a.v[start], growing a if needed using m.
void array_copy(Array* a, u32 start, const void* src, u32 srclen, Mem mem) {
  u32 capNeeded = start + srclen;
  if (capNeeded > a->cap) {
    if (a->v == NULL) {
      // initial allocation to exactly the size needed
      a->v = (void*)memalloc(mem, sizeof(void*) * capNeeded);
      a->cap = capNeeded;
      a->onstack = false;
    } else {
      array_grow(a, capNeeded - a->cap, mem);
    }
  }
  memcpy(&a->v[start], src, srclen * sizeof(void*));
  a->len = MAX(a->len, start + srclen);
}
