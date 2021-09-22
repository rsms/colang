#include "../common.h"
#include "array.h"
#include <stdlib.h> // for qsort_r

// ARRAY_CAP_STEP defines a power-of-two which the cap must be aligned to.
// This is used to round up growth. I.e. grow by 60 with a cap of 32 would increase the cap
// to 96 (= 32 + (align2(60, ARRAY_CAP_STEP=32) = 64)).
#define ARRAY_CAP_STEP 32

typedef struct SortCtx {
  ArraySortFun f;
  void*        userdata;
} SortCtx;


static int _sort(void* ctx, const void* s1p, const void* s2p) {
  return ((SortCtx*)ctx)->f(
    *((const void**)s1p),
    *((const void**)s2p),
    ((SortCtx*)ctx)->userdata
  );
}

void ArraySort(Array* a, ArraySortFun f, void* userdata) {
  SortCtx ctx = { f, userdata };
  qsort_r(a->v, a->len, sizeof(void*), &ctx, &_sort);
}

void TArrayGrow(void** v, const void* init, u32* cap, size_t elemsize, Mem mem) {
  u32 newcap = align2(*cap + 1, ARRAY_CAP_STEP);
  if (*v != init) {
    *v = memrealloc(mem, *v, elemsize * newcap);
    *cap = newcap;
  } else {
    // moving array from stack to heap
    if (R_LIKELY(*v = memalloc(mem, elemsize * newcap)))
      memcpy(*v, init, elemsize * *cap);
    *cap = newcap;
  }
}

void ArrayGrow(Array* a, size_t addl, Mem mem) {
  u32 reqcap = a->cap + addl;
  u32 cap = align2(reqcap, ARRAY_CAP_STEP);
  if (!a->onstack || a->v == NULL) {
    a->v = memrealloc(mem, a->v, sizeof(void*) * cap);
  } else {
    // moving array from stack to heap
    void** v = (void**)memalloc(mem, sizeof(void*) * cap);
    memcpy(v, a->v, sizeof(void*) * a->len);
    a->v = v;
    a->onstack = false;
  }
  a->cap = cap;
}

ssize_t ArrayIndexOf(Array* a, void* nullable entry) {
  for (u32 i = 0; i < a->len; i++) {
    if (a->v[i] == entry)
      return (ssize_t)i;
  }
  return -1;
}

ssize_t ArrayLastIndexOf(Array* a, void* nullable entry) {
  for (u32 i = a->len; i--; ) {
    if (a->v[i] == entry)
      return (ssize_t)i;
  }
  return -1;
}

void ArrayRemove(Array* a, u32 start, u32 count) {
  assert(start + count <= a->len);
  // ArrayRemove( [0 1 2 3 4 5 6 7] start=2 count=3 ) => [0 1 5 6 7]
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


// ArrayCopy copies src of srclen to a, starting at a.v[start], growing a if needed using m.
void ArrayCopy(Array* a, u32 start, const void* src, u32 srclen, Mem mem) {
  u32 capNeeded = start + srclen;
  if (capNeeded > a->cap) {
    if (a->v == NULL) {
      // initial allocation to exactly the size needed
      a->v = (void*)memalloc(mem, sizeof(void*) * capNeeded);
      a->cap = capNeeded;
      a->onstack = false;
    } else {
      ArrayGrow(a, capNeeded - a->cap, mem);
    }
  }
  memcpy(&a->v[start], src, srclen * sizeof(void*));
  a->len = MAX(a->len, start + srclen);
}
