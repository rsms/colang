// array -- dynamic array
// SPDX-License-Identifier: Apache-2.0
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define ARRAY_IMPLEMENTATION
#endif
#include "mem.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

typedef struct Array {
  u8* nullable v; // u8 so we get -Wincompatible-pointer-types if we access .v directly
  u32 len;        // number of valid entries at v
  u32 cap : 31;   // capacity, in number of entries, of v
  u32 ext : 1;    // 1 if v is external, 0 if originating in memory allocator
} Array;
#define DEF_ARRAY_TYPE(NAME, T) typedef struct NAME { T* nullable v; u32 len, cap; } NAME;

/* API
  void array_init(TYPE, Array* a, void* storage, usize storagesize)
    // initialize array a to use external storage
  u32 array_at(TYPE, Array* a, u32 index);
  u32 array_at_safe(TYPE, Array* a, u32 index);
  TYPE* nullable array_push(TYPE, Array* a, Mem mem); // NULL on mem_alloc failure
  void array_remove(TYPE, Array* a, u32 start, u32 len);
  void array_move(TYPE, Array* a, u32 dst, u32 start, u32 end);
    // moves the chunk [src,src+len) to dst
  void array_free(TYPE, Array* a, Mem mem);
  void array_reserve(TYPE, Array* a, Mem mem, u32 addl);
*/
#define array_init(T, a, storage, size)   _array_init((a),sizeof(T),(storage),(size))
#define array_at(T, a, index)             (((T*)(a)->v) + (index))
#define array_at_safe(T, a, i)            ({ safecheck((i)<(a)->len); array_at(T,(a),(i)); })
#define array_push(T, a, m)               ((T*)_array_push((a),(m),sizeof(T)))
#define array_remove(T, a, start, len)    _array_remove((a),sizeof(T),(start),(len))
#define array_move(T, a, dst, start, end) _array_move(sizeof(T),(a)->v,(dst),(start),(end))
#define array_reserve(T, a, m, addl)      _array_reserve((a),(m),(addl),sizeof(T))
#define array_free(T, a, m)               _array_free((a),(m),sizeof(T))

DEF_ARRAY_TYPE(U32Array, u32)
static void u32array_init(U32Array* a, void* nullable storage, usize storagesize);
static u32 u32array_at(U32Array* a, u32 index);
static u32 u32array_at_safe(U32Array* a, u32 index);
static bool u32array_push(U32Array* a, Mem mem, u32 value);
static void u32array_remove(U32Array* a, u32 start, u32 len);
static void u32array_move(U32Array* a, u32 dst, u32 start, u32 end);
static void u32array_reserve(U32Array* a, Mem mem, u32 addl);
static void u32array_free(U32Array* a, Mem mem);

DEF_ARRAY_TYPE(PtrArray, void*)
static void ptrarray_init(PtrArray* a, void* nullable storage, usize storagesize);
static void* ptrarray_at(PtrArray* a, u32 index);
static void* ptrarray_at_safe(PtrArray* a, u32 index);
static bool ptrarray_push(PtrArray* a, Mem mem, void* value);
static void ptrarray_remove(PtrArray* a, u32 start, u32 len);
static void ptrarray_move(PtrArray* a, u32 dst, u32 start, u32 end);
static void ptrarray_reserve(PtrArray* a, Mem mem, u32 addl);
static void ptrarray_free(PtrArray* a, Mem mem);


//———————————————————————————————————————————————————————————————————————————————————————
// internal

// ARRAY_CAP_MAX is the capacity limit of an Array.
// sizeof(cap)*8 - 1 (one bit for "ext")
// 2^31-1 = 2 147 483 647 = I32_MAX
#define ARRAY_CAP_MAX 0x7fffffff

bool _array_grow(Array* a, Mem, usize elemsize, u32 addl);
bool _array_reserve(Array* a, Mem, u32 addl, usize elemsize);
void _array_remove(Array* a, u32 elemsize, u32 start, u32 len);
void _array_free(Array* a, Mem m, usize elemsize);
void* nullable _array_push(Array* a, Mem m, usize elemsize);

inline static void _array_init(
  Array* a, u32 elemsize, void* nullable storage, usize storagesize)
{
  if (storagesize == 0) {
    memset(a, 0, sizeof(Array));
  } else {
    assertnotnull(storage);
    assert(storagesize/elemsize <= ARRAY_CAP_MAX);
    assert(storagesize > 0);
    *a = (Array){ .v=storage, .cap=storagesize/elemsize, .ext=true };
  }
}

// _array_move moves the chunk [src,src+len) to index dst. For example:
//   _array_move(z, v, 5, 1, 1+2) = [1  2 3  4 5|6 7 8] ⟹ [1 4 5  2 3  6 7 8]
//   _array_move(z, v, 1, 4, 4+2) = [1|2 3 4  5 6  7 8] ⟹ [1  5 6  2 3 4 7 8]
#define _array_move(elemsize, v, dst, start, end) (                                 \
  (elemsize) == 4 ? _AMOVE_ROTATE(_arotate32,(dst),(start),(end),(u32* const)(v)) : \
  (elemsize) == 8 ? _AMOVE_ROTATE(_arotate64,(dst),(start),(end),(u64* const)(v)) : \
                    _AMOVE_ROTATE(_arotatemem,(dst),(start),(end),(elemsize),(v)) )
#define _AMOVE_ROTATE(f, dst, start, end, args...) (     \
  ((start)==(dst)||(start)==(end)) ? ((void)0) :         \
  ((start) > (dst)) ? (f)(args, (dst), (start), (end)) : \
  (f)(args, (start), (end), (dst)) )

// arotate rotates the order of v in the range [first,last) in such a way
// that the element pointed to by "mid" becomes the new "first" element.
// Assumes first <= mid < last.
#define arotate(elemsize, v, first, mid, last) (                          \
  (elemsize) == 4 ? _arotate32((u32* const)(v), (first), (mid), (last)) : \
  (elemsize) == 8 ? _arotate64((u64* const)(v), (first), (mid), (last)) : \
  _arotatemem((elemsize), (v), (first), (mid), (last)) )
void _arotatemem(u32 stride, void* v, u32 first, u32 mid, u32 last);
void _arotate32(u32* const v, u32 first, u32 mid, u32 last);
void _arotate64(u64* const v, u32 first, u32 mid, u32 last);

//———————————————————————————————————————————————————————————————————————————————————————
// typed array veneer

#define DEF_ARRAY_IMPL(ARRAY, T, PREFIX) \
  inline static void PREFIX##_init(ARRAY* a, void* nullable storage, usize size) { \
    _array_init((Array*)a, sizeof(T), storage, size); \
  } \
  inline static void PREFIX##_free(ARRAY* a, Mem m) { \
    _array_free((Array*)a, m, sizeof(T)); \
  } \
  inline static T PREFIX##_at(ARRAY* a, u32 index) { return a->v[index]; } \
  inline static T PREFIX##_at_safe(ARRAY* a, u32 index) { \
    safecheck(index < a->len); \
    return a->v[index]; \
  } \
  inline static bool PREFIX##_push(ARRAY* a, Mem mem, T value) { \
    if (a->len == a->cap && UNLIKELY(!_array_grow((Array*)a, mem, sizeof(T), 1))) \
      return false; \
    a->v[a->len++] = value; \
    return true; \
  } \
  inline static void PREFIX##_remove(ARRAY* a, u32 start, u32 len) { \
    return _array_remove((Array*)a, sizeof(T), start, len); \
  } \
  inline static void PREFIX##_move(ARRAY* a, u32 dst, u32 start, u32 end) { \
    _array_move(sizeof(T), a->v, dst, start, end); \
  } \
  inline static void PREFIX##_reserve(ARRAY* a, Mem mem, u32 addl) { \
    _array_reserve((Array*)a, mem, addl, sizeof(T)); \
  } \
// end DEF_ARRAY_IMPL

#define DEF_ARRAY(ARRAY, T, PREFIX) \
  DEF_ARRAY_TYPE(ARRAY, T) \
  DEF_ARRAY_IMPL(ARRAY, T, PREFIX)

DEF_ARRAY_IMPL(U32Array, u32, u32array)
DEF_ARRAY_IMPL(PtrArray, void*, ptrarray)

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef ARRAY_IMPLEMENTATION


void _array_free(Array* a, Mem m, usize elemsize) {
  if (a->v && a->ext == 0)
    mem_free(m, a->v, (usize)a->cap * elemsize);
}


void* nullable _array_push(Array* a, Mem m, usize elemsize) {
  if (a->len == a->cap && UNLIKELY(!_array_grow(a, m, elemsize, 1)))
    return NULL;
  return a->v + (u32)elemsize*(a->len++);
}


void _array_remove(Array* a, u32 elemsize, u32 start, u32 len) {
  if (len == 0)
    return;
  safecheckf(start+len <= a->len, "end=%u > len=%u", start+len, a->len);
  if (start+len < a->len) {
    void* dst = a->v + elemsize*start;
    void* src = dst + elemsize*len;
    memmove(dst, src, elemsize*(a->len - start - len));
  }
  a->len -= len;
}


static u32 calc_initcap(Array* a, Mem m, usize elemsize, u32 addl) {
  const u32 min_init_cap = (u32)( sizeof(void*) * 8 / elemsize );
  if (addl > ARRAY_CAP_MAX)
    return U32_MAX;
  return MAX(min_init_cap, addl);
}


static u32 calc_newcap(Array* a, Mem m, usize elemsize, u32 addl) {
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


bool _array_grow(Array* a, Mem m, usize elemsize, u32 addl) {
  assert(addl > 0);

  dlog("grow array %p (addl %u)", a, addl);

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

bool _array_reserve(Array* a, Mem m, u32 addl, usize elemsize) {
  u32 len;
  if (check_add_overflow(a->len, addl, &len))
    return false;
  if (len >= a->cap && UNLIKELY(!_array_grow(a, m, elemsize, addl)))
    return false;
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

#endif // ARRAY_IMPLEMENTATION
