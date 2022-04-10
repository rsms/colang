// dynamic array
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

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

typedef Array(void)        VoidArray;
typedef Array(void*)       PtrArray;
typedef Array(u32)         U32Array;
typedef Array(const char*) CStrArray;

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

END_INTERFACE
