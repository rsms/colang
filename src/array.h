// Array -- dynamic linear container. Valid when zero-initialized.
#pragma once
#include "mem.h"
ASSUME_NONNULL_BEGIN

#define TYPED_ARRAY_CAP_MAX 0x7fffffff

// #if __has_builtin(__is_convertible)

// DEF_TYPED_ARRAY defines an array type named A with T elements
#define DEF_TYPED_ARRAY(A, T) \
  DEF_TYPED_ARRAY_TYPES(A, T) \
  DEF_TYPED_ARRAY_FUNCS(A, T)

// DEF_TYPED_ARRAY defines an array type named A with PTRT elements.
// PTRT must be a pointer type.
// This produces less code than DEF_TYPED_ARRAY(A,T*) as it uses PtrArray.
#define DEF_TYPED_PTR_ARRAY(A, PTRT) \
  DEF_TYPED_ARRAY_TYPES(A, PTRT)     \
  DEF_TYPED_PTR_ARRAY_FUNCS(A, PTRT)

#define DEF_TYPED_ARRAY_TYPES(A, T)           \
  typedef struct {                            \
    T*  v;        /* entries                */\
    u32 len;      /* valid entries at v     */\
    u32 cap : 31; /* capacity of v          */\
    u32 ext : 1;  /* true if v is external  */\
  } A; \
  typedef int (*A##SortFun)(void* nullable ctx, const T* elemp1, const T* elemp2);



/* DEF_TYPED_ARRAY_FUNCS defines the following inline functions:

// {A}Init initializes a to zero, equivalent of *a=(A){0}
static void {A}Init({A}* a);

// {A}InitStorage initializes a with initial storage.
// As a grows, memcpy may be used to move storage to memalloc allocated memory.
static void {A}InitStorage({A}* a, {T}* storage, u32 storagecap);

// {A}Free frees up a->v (but does not zero len or cap.)
// If a->v is storage provided with {A}InitStorage, this does nothing.
// m must be the same allocator used with past operations on a.
static void {A}Free({A}* a, Mem m);

// {A}Clear sets a->len to 0
static void {A}Clear({A}* a);

// {A}Push adds v to the end of a. Returns false if memory allocation failed.
static bool {A}Push({A}* a, {T} v, Mem);

// {A}Pop removes the last value from a (a must not be empty)
static {T} {A}Pop({A}* a);

// {A}Remove removes values in the range [startindex .. startindex+count)
static void {A}Remove({A}* a, u32 startindex, u32 count);

// {A}IndexOf performs a search for entry. Returns index or -1 if not found.
static i32 {A}IndexOf(const {A}* a, const {T} entry);

// {A}IndexOf performs a search for entry, starting with the last entry.
static i32 {A}LastIndexOf(const {A}* a, const {T} entry);

// {A}Copy copies srclen number of values from src to dst,
// starting at a.v[startindex], growing dst if needed using m.
// Returns an error on overflow or allocation failure.
static error {A}Copy({A}* dst, u32 startindex, const {T}* src, u32 srclen, Mem m);

// {A}MakeRoom ensures that there is at least addl_count free slots available.
// Returns an error on overflow or allocation failure.
static error {A}MakeRoom({A}* a, u32 addl_count, Mem);

// {A}Sort reorders a in place according to comparator, which is called with ctx
// and pointers to two values. The comparator should return -1 if *p1 is lesser
// than *p2, +1 if *p1 is greater than *p2 or 0 if the values are equivalent.
static void {A}Sort({A}* a, {A}SortFun comparator, void* nullable ctx);
typedef int (*{A}SortFun)(void* nullable ctx, const {T}* p1, const {T}* p2);

*/
#define DEF_TYPED_ARRAY_FUNCS(A, T)                                                \
  static void  A##Init(A* a);                                                      \
  static void  A##InitStorage(A* a, T* storage, u32 storagecap);                   \
  static void  A##Free(A* a, Mem);                                                 \
  static void  A##Clear(A* a);                                                     \
  static bool  A##Push(A* a, T v, Mem);                                            \
  static T     A##Pop(A* a);                                                       \
  static void  A##Remove(A* a, u32 startindex, u32 count);                         \
  static i32   A##IndexOf(const A* a, const T entry);                              \
  static i32   A##LastIndexOf(const A* a, const T entry);                          \
  static error A##Copy(A* dst, u32 startindex, const T* src, u32 srclen, Mem);     \
  static error A##MakeRoom(A* a, u32 addl_count, Mem);                             \
  static void  A##Sort(A* a, A##SortFun comparator, void* nullable ctx);           \
\
inline static void A##Init(A* a) { *a = (A){0}; } \
inline static void A##InitStorage(A* a, T* storage, u32 storagecap) { \
  assert(storagecap <= TYPED_ARRAY_CAP_MAX); \
  *a = (A){ .v=storage, .cap=storagecap, .ext=true }; \
} \
inline static void A##Free(A* a, Mem m) { \
  if (!a->ext && a->v != NULL) \
    memfree(m, a->v); \
} \
inline static void A##Clear(A* a) { a->len = 0; } \
inline static bool A##Push(A* a, T v, Mem m) { \
  if (UNLIKELY(a->len == a->cap) && array_grow((PtrArray*)a, sizeof(T), 1, m) != 0) \
    return false; \
  a->v[a->len++] = v; \
  return true; \
} \
inline static T A##Pop(A* a) { \
  assert(a->len > 0); \
  return a->v[--a->len]; \
} \
inline static void A##Remove(A* a, u32 startindex, u32 count) { \
  array_remove((PtrArray*)a, sizeof(T), startindex, count); \
} \
inline static i32 A##IndexOf(const A* a, const T entry) { \
  return array_indexof((const PtrArray*)a, sizeof(T), &entry); \
} \
inline static i32 A##LastIndexOf(const A* a, const T entry) { \
  return array_lastindexof((const PtrArray*)a, sizeof(T), &entry); \
} \
inline static error A##Copy(A* dst, u32 startindex, const T* src, u32 srclen, Mem m) { \
  return array_copy((PtrArray*)dst, sizeof(T), startindex, src, srclen, m); \
} \
inline static error A##MakeRoom(A* a, u32 addl_count, Mem m) { \
  if (a->cap - a->len < addl_count) \
    return array_grow((PtrArray*)a, sizeof(T), addl_count, m); \
  return 0; \
} \
inline static void A##Sort(A* a, A##SortFun comparator, void* nullable ctx) { \
  array_sort((PtrArray*)a, sizeof(T), (PtrArraySortFun)comparator, ctx); \
} \
// end DEF_TYPED_ARRAY_API

#define DEF_TYPED_PTR_ARRAY_FUNCS(A, PT)                                             \
  ALWAYS_INLINE static void A##Init(A* a) { PtrArrayInit((PtrArray*)a); }            \
  ALWAYS_INLINE static void A##InitStorage(A* a, PT* storage, u32 storagecap) {      \
    PtrArrayInitStorage((PtrArray*)a, (void**)storage, storagecap); }                \
  ALWAYS_INLINE static void A##Free(A* a, Mem m) { PtrArrayFree((PtrArray*)a, m); }  \
  ALWAYS_INLINE static void A##Clear(A* a) { PtrArrayClear((PtrArray*)a); }          \
  ALWAYS_INLINE static bool A##Push(A* a, PT v, Mem m) {                             \
    return PtrArrayPush((PtrArray*)a, v, m); }                                       \
  ALWAYS_INLINE static PT A##Pop(A* a) { return PtrArrayPop((PtrArray*)a); }         \
  ALWAYS_INLINE static void A##Remove(A* a, u32 startindex, u32 count) {             \
    PtrArrayRemove((PtrArray*)a, startindex, count); }                               \
  ALWAYS_INLINE static i32 A##IndexOf(const A* a, const PT entry) {                  \
    return PtrArrayIndexOf((PtrArray*)a, entry); }                                   \
  ALWAYS_INLINE static i32 A##LastIndexOf(const A* a, const PT entry) {              \
    return PtrArrayLastIndexOf((PtrArray*)a, entry); }                               \
  ALWAYS_INLINE static error A##Copy(                                                \
    A* dst, u32 startindex, const PT* src, u32 srclen, Mem m) {                      \
    return PtrArrayCopy((PtrArray*)dst, startindex, (const void**)src, srclen, m); } \
  ALWAYS_INLINE static error A##MakeRoom(A* a, u32 addl_count, Mem m) {              \
    return PtrArrayMakeRoom((PtrArray*)a, addl_count, m); }                          \
  ALWAYS_INLINE static void A##Sort(A* a, A##SortFun cmpf, void* nullable ctx) {     \
    PtrArraySort((PtrArray*)a, (PtrArraySortFun)cmpf, ctx); }                        \
// end DEF_TYPED_PTR_ARRAY_FUNCS

// PtrArray
DEF_TYPED_ARRAY_TYPES(PtrArray, void*)
error array_grow(PtrArray*, usize elemsize, usize count, Mem);
i32 array_indexof(const PtrArray*, usize elemsize, const void* elemp);
i32 array_lastindexof(const PtrArray*, usize elemsize, const void* elemp);
void array_remove(PtrArray* a, usize elemsize, u32 startindex, u32 count);
error array_copy(PtrArray*, usize elemsize, u32 si, const void* srcv, u32 srclen, Mem);
void array_sort(PtrArray*, usize elemsize, PtrArraySortFun, void* nullable ctx);
DEF_TYPED_ARRAY_FUNCS(PtrArray, void*)


// U32Array
DEF_TYPED_ARRAY(U32Array, u32)

// CStrArray
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wduplicate-decl-specifier\"")
DEF_TYPED_ARRAY(CStrArray, const char*)
_Pragma("GCC diagnostic pop")

ASSUME_NONNULL_END
