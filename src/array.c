#include "coimpl.h"
#include "array.h"

#ifndef CO_NO_LIBC
  #include <stdlib.h> // for qsort_r
#endif

#define MAX_EXTRA_CAP(elemsize) ((usize)4096/elemsize)


error array_grow(PtrArray* a, usize elemsize, usize count, Mem mem) {

  // count = cap < MAX_EXTRA_CAP ? min(MAX_EXTRA_CAP, count * 2) : count
  if (a->cap < MAX_EXTRA_CAP(elemsize))
    count = (usize)MIN( (u64)(MAX_EXTRA_CAP(elemsize) - a->cap), (u64)count * 2 );

  // newcap = a->cap + count
  usize newcap;
  if (check_add_overflow((usize)a->cap, count, &newcap) || newcap > TYPED_ARRAY_CAP_MAX)
    return err_overflow;

  // nbyte = newcap * elemsize
  usize nbyte;
  if (check_mul_overflow(newcap, elemsize, &nbyte))
    return err_overflow;

  void** v;
  if (a->ext) { // moving data from external storage to mem-allocated storage
    v = (void**)memalloc(mem, nbyte);
    if (!v)
      return err_nomem;
    memcpy(v, a->v, elemsize * a->len);
  } else {
    v = memresize(mem, a->v, nbyte);
    if (!v)
      return err_nomem;
  }

  a->v = v;
  a->cap = (u32)newcap;
  a->ext = 0;
  return 0;
}

i32 array_indexof(const PtrArray* a, usize elemsize, const void* elemp) {
  for (u32 i = 0; i < a->len; i++) {
    if (memcmp(&a->v[i], elemp, elemsize) == 0)
      return (i32)i;
  }
  return -1;
}

i32 array_lastindexof(const PtrArray* a, usize elemsize, const void* elemp) {
  for (u32 i = a->len; i--; ) {
    if (memcmp(&a->v[i], elemp, elemsize) == 0)
      return (i32)i;
  }
  return -1;
}

void array_remove(PtrArray* a, usize elemsize, u32 startindex, u32 count) {
  safecheckf(startindex == 0 || startindex < a->len, "out of bounds");
  count = MIN(count, a->len - startindex);
  // array_remove( [0 1 2 3 4 5 6 7] startindex=2 count=3 ) => [0 1 5 6 7]
  //
  for (u32 i = startindex + count; i < a->len; i++)
    a->v[(i - count) * elemsize] = a->v[i * elemsize];
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

// copies src of srclen to dst, starting at dst->v[start], growing dst if needed.
error array_copy(
  PtrArray* dst, usize elemsize, u32 startindex, const void* srcv, u32 srclen, Mem mem)
{
  safecheckf(startindex == 0 || startindex < dst->len, "out of bounds");

  // needcap = startindex + srclen;
  u32 needcap;
  if (check_add_overflow(startindex, srclen, &needcap) || needcap > TYPED_ARRAY_CAP_MAX)
    return err_overflow;

  if (dst->cap < needcap) {
    if (dst->v == NULL) {
      // nbyte = needcap * elemsize
      usize nbyte;
      if (check_mul_overflow((usize)needcap, elemsize, &nbyte))
        return err_overflow;
      dst->v = (void*)memalloc(mem, nbyte);
      dst->cap = needcap;
      dst->ext = 0;
    } else {
      error err = array_grow(dst, elemsize, needcap - dst->cap, mem);
      if (err)
        return err;
    }
  }

  // note: no overflow check on srclen*elemsize since we check cap already
  memcpy(&dst->v[startindex * elemsize], srcv, srclen * elemsize);
  dst->len = MAX(dst->len, startindex + srclen);
  return 0;
}

void array_sort(PtrArray* a, usize elemsize, PtrArraySortFun f, void* ctx) {
  #ifdef CO_NO_LIBC
    assert(!"array_sort not implemented");
  #else
    qsort_r(a->v, a->len, elemsize, ctx, (int(*)(void*,const void*,const void*))f);
  #endif
}

