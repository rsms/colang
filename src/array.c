// dynamic array
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define ARRAY_IMPLEMENTATION
#endif
#include "mem.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

#define Array(T) struct { \
  T* nullable v; \
  u32 len;      /* number of valid entries at v */ \
  u32 cap : 31; /* capacity, in number of entries, of v */ \
  u32 ext : 1;  /* 1 if v is in external storage */ \
}
#if API_DOC

Array(T) array_make(Array(T), T* nullable storage, usize storagesize)
  // Create and initialize a new array with optional initial caller-managed storage

void array_init(Array(T)* a, T* nullable storage, usize storagesize)
  // Initializes a with optional initial caller-managed storage

void array_clear(Array(T)* a)
  // sets len=0

void array_free(Array(T)* a)
  // If a->v is on the memory heap, calls memfree

T array_at(Array(T)* a, u32 index)      // range assertion + a->v[index]
T array_at_safe(Array(T)* a, u32 index) // range safecheck + a->v[index]

bool array_push(Array(T)* a, T value)
  // Appends one value to the end of a.
  // Returns false on overflow or allocation failure.

bool array_append(Array(T)* a, const T* src, u32 len)
  // Appends len number of values to the end of a.
  // Returns false on overflow or allocation failure.

void array_remove(Array(T)* a, u32 start, u32 len)
  // Removes the chunk [start,start+len)

void array_move(Array(T)* a, u32 dst, u32 start, u32 end)
  // Moves the chunk [start,end) to dst, pushing [dst,a->len) to the end.

bool array_reserve(Array(T)* a, u32 addl)
  // Ensures that there's at least addl available additional capacity.
  // Returns false on overflow or allocation failure.

bool array_fill(Array(T)* a, u32 start, const T fillvalue, u32 len)
  // Sets a[start:len] to len copies of fillvalue
  // If there's not enough room, dst grows; false is returned if allocation failed.

bool array_splice(Array(T)* a, u32 start, u32 removelen,
                  u32 insertlen, const T* nullable insertvals);
  // Changes the contents of an array by removing or replacing existing elements
  // and/or adding new elements.
  // insertvals must not refer to contents of a (use array_move instead.)
  // If insertvals is NULL and insertlen > 0, the corresponding region is zeroed.
  // If you want to replace some elements with a repeat value that is not zero,
  // use array_fill.

#endif // API_DOC

typedef Array(void)  VoidArray;
typedef Array(void*) PtrArray;
typedef Array(u32)   U32Array;

//———————————————————————————————————————————————————————————————————————————————————————
// internal

// ARRAY_CAP_MAX is the capacity limit of an Array.
// sizeof(cap)*8 - 1 (one bit for "ext")
// 2^31-1 = 2 147 483 647 = I32_MAX
#define ARRAY_CAP_MAX 0x7fffffff

#define array_make(ARRAY, storage, storagesize) \
  ( ASSERT_U32SIZE((storagesize)/_ARRAY_ESIZE((ARRAY*)0)), \
    (ARRAY){ .v=(storage), .cap=(u32)((storagesize)/_ARRAY_ESIZE((ARRAY*)0)), .ext=true } )

#define array_init(a, storage, storagesize) \
  ( ASSERT_U32SIZE((storagesize)/_ARRAY_ESIZE(a)), \
    (a)->v = (storage), \
    (a)->len = 0, \
    (a)->cap = (u32)((storagesize)/_ARRAY_ESIZE(a)), \
    (a)->ext=true )

#define array_clear(a) ((a)->len = 0)

#define array_free(a) \
  ( ((a)->v && !(a)->ext) ? \
    memfree((a)->v, (usize)(a)->cap * _ARRAY_ESIZE(a)) : ((void)0) )

#define array_at(a, index)      (assert((index) < (a)->len), (a)->v[(index)])
#define array_at_safe(a, index) (safecheck((index) < (a)->len), (a)->v[(index)])

#define array_push(a, value) ( \
  ( UNLIKELY((a)->len == (a)->cap) && \
    UNLIKELY(!_array_grow((VoidArray*)(a), mem_ctx(), _ARRAY_ESIZE(a), 1)) ) ? false : \
  ( ((a)->v[(a)->len++] = value), true ) )

#define array_append(a, src, len) ({ \
  const __typeof__(*(a)->v)* __srcv = (src); /* catch incompatible types */ \
  _array_append((VoidArray*)(a), _ARRAY_ESIZE(a), __srcv, (len)); })

#define array_remove(a, start, len) \
  _array_remove((VoidArray*)(a), _ARRAY_ESIZE(a), (start), (len))

#define array_move(a, dst, start, end) \
  _array_move(_ARRAY_ESIZE(a), (a)->v, (dst), (start), (end))

#define array_reserve(a, addl) \
  _array_reserve((VoidArray*)(a), mem_ctx(), (addl), _ARRAY_ESIZE(a))

#define array_fill(a, start, fillvalue, len) ({ \
  const __typeof__(*(a)->v) __fillv = (fillvalue); \
  _array_fill((VoidArray*)(a), _ARRAY_ESIZE(a), (start), &__fillv, (len)); })

#define array_splice(a, start, removelen, insertlen, insertvals) ({ \
  const __typeof__(*(a)->v)* __srcv = (insertvals); /* catch incompatible types */ \
  _array_splice((VoidArray*)(a), _ARRAY_ESIZE(a), (start),(removelen),(insertlen), __srcv); \
})


// DEF_ARRAY_VENEER defines a bunch of inline functions on top of the array_* macros,
// useful for better type feedback & IDE integration, at the expense of code complexity.
#define DEF_ARRAY_VENEER(ARRAY, T, PREFIX) \
  inline static ARRAY PREFIX##make(T* nullable storage, usize storagesize) { \
    return array_make(ARRAY, storage, storagesize); } \
  inline static void PREFIX##init(ARRAY* a, T* nullable storage, usize storagesize) { \
    array_init(a, storage, storagesize); } \
  inline static void PREFIX##clear(ARRAY* a) { \
    array_clear(a); } \
  inline static void PREFIX##free(ARRAY* a) { \
    array_free(a); } \
  inline static T PREFIX##at(ARRAY* a, u32 index) { \
    return array_at(a, index); } \
  inline static T PREFIX##at_safe(ARRAY* a, u32 index) { \
    return array_at_safe(a, index); } \
  inline static bool PREFIX##push(ARRAY* a, T value) { \
    return array_push(a, value); } \
  inline static bool PREFIX##append(ARRAY* a, const T* src, u32 len) { \
    return array_append(a, src, len); } \
  inline static void PREFIX##remove(ARRAY* a, u32 start, u32 len) { \
    array_remove(a, start, len); } \
  inline static void PREFIX##move(ARRAY* a, u32 dst, u32 start, u32 end) { \
    array_move(a, dst, start, end); } \
  inline static bool PREFIX##reserve(ARRAY* a, u32 addl) { \
    return array_reserve(a, addl); } \
  inline static bool PREFIX##fill(ARRAY* a, u32 start, const T fillvalue, u32 len) { \
    return array_fill(a, start, fillvalue, len); } \
  inline static bool PREFIX##splice( \
    ARRAY* a, u32 start, u32 removelen, u32 insertlen, const T* nullable insertvals) { \
    return array_splice(a, start, removelen, insertlen, insertvals); }


// element size of array a
#define _ARRAY_ESIZE(a) sizeof(*(a)->v)

// implementation prototypes
bool _array_grow(VoidArray* a, Mem, usize elemsize, u32 addl);
bool _array_reserve(VoidArray* a, Mem, u32 addl, usize elemsize);
void _array_remove(VoidArray* a, u32 elemsize, u32 start, u32 len);
void _array_free(VoidArray* a, Mem, usize elemsize);
void* nullable _array_push(VoidArray* a, Mem, usize elemsize);
bool _array_append(VoidArray* a, usize elemsize, const void* restrict src, u32 len);
bool _array_fill_memset(VoidArray* a, u32 start, int c, u32 len);
bool _array_fill_memcpy(VoidArray* a, usize elemsize, u32 start, const void* p, u32 len);
bool _array_splice(VoidArray *a, usize elemsize,
  u32 start, u32 removelen, u32 insertlen, const void* restrict nullable insertvals);

inline static bool _array_fill(
  VoidArray* a, usize elemsize, u32 start, const void* fillvalp, u32 len)
{
  if (elemsize == 1)
    return _array_fill_memset(a, start, *(const u8*)fillvalp, len);
  return _array_fill_memcpy(a, elemsize, start, fillvalp, len);
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

// // arotate rotates the order of v in the range [first,last) in such a way
// // that the element pointed to by "mid" becomes the new "first" element.
// // Assumes first <= mid < last.
// #define arotate(elemsize, v, first, mid, last) (                          \
//   (elemsize) == 4 ? _arotate32((u32* const)(v), (first), (mid), (last)) : \
//   (elemsize) == 8 ? _arotate64((u64* const)(v), (first), (mid), (last)) : \
//   _arotatemem((elemsize), (v), (first), (mid), (last)) )
void _arotatemem(u32 stride, void* v, u32 first, u32 mid, u32 last);
void _arotate32(u32* const v, u32 first, u32 mid, u32 last);
void _arotate64(u64* const v, u32 first, u32 mid, u32 last);

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef ARRAY_IMPLEMENTATION


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
  assert(src < a->v || src >= a->v + a->cap * elemsize);
  memcpy(a->v + (a->len * elemsize), src, len * elemsize);
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


//———————————————————————————————————————————————————————————————————————————————————————
#endif // ARRAY_IMPLEMENTATION
