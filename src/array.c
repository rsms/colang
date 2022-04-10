// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


void _array_remove(VoidArray* a, u32 elemsize, u32 start, u32 len) {
  if (len == 0)
    return;
  safecheckf(start + len <= a->len, "out of bounds (%u)", start + len);
  if (start + len < a->len) {
    void* dst = a->v + elemsize*start;
    void* src = dst + elemsize*len;
    memmove(dst, src, elemsize*(a->len - start - len));
  }
  a->len -= len;
}


static u32 calc_initcap(VoidArray* a, Mem m, usize elemsize, u32 addl) {
  const u32 min_init_cap = (u32)( sizeof(void*) * 8 / elemsize );
  if (addl > ARRAY_CAP_MAX)
    return U32_MAX;
  return MAX(min_init_cap, addl);
}


static u32 calc_newcap(VoidArray* a, Mem m, usize elemsize, u32 addl) {
  // growth scheme inspired by folly:
  //   https://github.com/facebook/folly/blob/5bbfb175cb8fc7edab442f06105d4681654732e9
  //   /folly/docs/FBVector.md#memory-handling
  u32 newcap;
  if (check_mul_overflow((u32)a->cap, addl, &newcap) || newcap > ARRAY_CAP_MAX)
    return U32_MAX;
  if (newcap <= 4096 / elemsize) // small -- growth factor 2
    return MAX(a->cap * 2, newcap);
  if (newcap <= 4096 * 32 / elemsize) // medium -- growth factor 1.5
    return MAX((a->cap * 3 + 1) / 2, newcap);
  // large -- growth factor 2
  u64 nc = (u64)a->cap * 2;
  if UNLIKELY(nc > ARRAY_CAP_MAX)
    return U32_MAX;
  return MAX((u32)nc, newcap);
}


static usize calc_newsize(u32 newcap, usize elemsize) {
  usize newsize;
  #if USIZE_MAX > U32_MAX
    newsize = (usize)newcap * elemsize;
    if (newsize > ARRAY_CAP_MAX)
      return USIZE_MAX;
  #else
    if (check_mul_overflow((usize)newcap, elemsize, &newsize) || newsize > ARRAY_CAP_MAX)
      return USIZE_MAX;
  #endif
  return newsize;
}


bool _array_grow(VoidArray* a, Mem m, usize elemsize, u32 addl) {
  assert(addl > 0);
  usize newsize;
  void* newp;

  if (a->cap == 0) {
    u32 newcap = calc_initcap(a, m, elemsize, addl);
    newsize = calc_newsize(newcap, elemsize);
    if UNLIKELY(newcap == U32_MAX || newsize == USIZE_MAX)
      return false;
    newp = mem_allocx(m, &newsize);
  } else {
    u32 newcap = calc_newcap(a, m, elemsize, addl);
    newsize = calc_newsize(newcap, elemsize);
    if UNLIKELY(newcap == U32_MAX || newsize == USIZE_MAX)
      return false;
    if (a->ext) {
      // move externally-stored data to mem-allocated storage
      newp = mem_allocx(m, &newsize);
      if LIKELY(newp)
        memcpy(newp, a->v, (usize)a->len * elemsize);
    } else {
      // grow existing allocation
      newp = mem_resizex(m, a->v, a->cap * elemsize, &newsize);
    }
  }

  if UNLIKELY(newp == NULL)
    return false;

  a->v = newp;
  a->cap = (u32)MIN(newsize / elemsize, (usize)ARRAY_CAP_MAX);
  a->ext = false;
  return true;
}


void* nullable _array_push(VoidArray* a, Mem m, usize elemsize) {
  if (a->len == a->cap && UNLIKELY(!_array_grow(a, m, elemsize, 1)))
    return NULL;
  return a->v + (u32)elemsize*(a->len++);
}


bool _array_reserve(VoidArray* a, Mem m, u32 addl, usize elemsize) {
  u32 len;
  if (check_add_overflow(a->len, addl, &len))
    return err_overflow;
  if (len >= a->cap)
    return _array_grow(a, m, elemsize, addl);
  return true;
}


bool _array_append(VoidArray* a, usize elemsize, const void* restrict src, u32 len) {
  u32 avail = a->cap - a->len;
  if (avail < len && UNLIKELY(!_array_grow(a, mem_ctx(), elemsize, len - avail)))
    return false;
  // src must not be part of a->v, or behavior of memcpy is undefined
  void* dst = a->v + a->len*elemsize;
  usize size = len * elemsize;
  assertf(src < dst || src >= dst + len,
    "trying to append tail of array to itself: %p in bounds %p ... %p", src, dst, dst + len);
  memcpy(dst, src, size);
  a->len += len;
  return true;
}

static bool _array_prepare_insert(VoidArray* a, usize elemsize, u32 start, u32 len) {
  safecheckf(start == 0 || start <= a->len, "out of bounds (%u)", start);
  u32 avail = a->cap - start;
  return avail >= len || _array_grow(a, mem_ctx(), elemsize, len - avail);
}

bool _array_fill_memset(VoidArray* a, u32 start, int c, u32 len) {
  if UNLIKELY(!_array_prepare_insert(a, 1, start, len))
    return false;
  memset(a->v + start, c, len);
  a->len = MAX(a->len, start + len);
  return true;
}

bool _array_fill_memcpy(VoidArray* a, usize elemsize, u32 start, const void* p, u32 len) {
  if UNLIKELY(!_array_prepare_insert(a, elemsize, start, len))
    return false;
  void* dst = a->v + (start * elemsize);
  void* end = a->v + ((start + len) * elemsize);
  for (; dst < end; dst += elemsize)
    memcpy(dst, p, elemsize);
  a->len = MAX(a->len, start + len);
  return true;
}


bool _array_splice(VoidArray *a, usize elemsize,
  u32 start, u32 removelen, u32 insertlen, const void* nullable restrict insertvals)
{
  u32 removeend = start + removelen;
  safecheckf(removeend <= a->len, "out of bounds (%u)", removelen);

  if UNLIKELY(!_array_prepare_insert(a, elemsize, start, insertlen))
    return false;

  if (a->len > removeend) {
    // move forward items which are past the removal range.
    // e.g. splice([1 2 3 4 5], 1, 2): [1 2 3 4 5] => [1 _ _ 4 5] => [1 4 5 _ _]
    void* dst = a->v + ((start + insertlen) * elemsize);
    void* src = a->v + (removeend * elemsize);
    usize nbytes = (a->len - removeend) * elemsize;
    memmove(dst, src, nbytes);
  }

  if (insertlen) {
    void* dst = a->v + (start * elemsize);
    if (insertvals == NULL) {
      memset(dst, 0, insertlen * elemsize);
    } else {
      // insertvals must not be part of a->v, or behavior of memcpy is undefined.
      // Note: Use array_move to shuffle around items inside an array.
      assert(insertvals < a->v || insertvals >= a->v + (a->cap * elemsize));
      memcpy(dst, insertvals, insertlen * elemsize);
    }
  }

  a->len += insertlen - removelen;
  return true;
}


void _arotatemem(u32 stride, void* v, u32 first, u32 mid, u32 last) {
  assert(first <= mid); // if equal (zero length), do nothing
  assert(mid < last);
  usize tmp[16]; assert(sizeof(u32) <= sizeof(tmp));
  u32 next = mid;
  while (first != next) {
    // swap
    memcpy(tmp, v + first*stride, stride); // tmp = v[first]
    memcpy(v + first*stride, v + next*stride, stride); // v[first] = v[next]
    memcpy(v + next*stride, tmp, stride); // v[next] = tmp
    first++;
    next++;
    if (next == last) {
      next = mid;
    } else if (first == mid) {
      mid = next;
    }
  }
}

#define DEF_AROTATE(NAME, T)                                   \
  void NAME(T* const v, u32 first, u32 mid, u32 last) { \
    assert(first <= mid);                                      \
    assert(mid < last);                                        \
    u32 next = mid;                                            \
    while (first != next) {                                    \
      T tmp = v[first]; v[first++] = v[next]; v[next++] = tmp; \
      if (next == last) next = mid;                            \
      else if (first == mid) mid = next;                       \
    }                                                          \
  }

DEF_AROTATE(_arotate32, u32)
DEF_AROTATE(_arotate64, u64)

