#pragma once
ASSUME_NONNULL_BEGIN

// Array is a dynamic linear container
typedef struct Array {
  void** v;       // entries
  u32    cap;     // capacity of v
  u32    len;     // valid entries at v
  bool   onheap;  // false if v is space on stack
} Array;

#define Array_INIT { NULL, 0, 0, true }

static void  ArrayInit(Array* a);
static void  ArrayInitWithStorage(Array* a, void* storage, u32 storagecap);
static void  ArrayFree(Array* a, Mem nullable mem);
void         ArrayGrow(Array* a, size_t addl, Mem nullable mem); // cap=align2(len+addl)
static void  ArrayPush(Array* a, void* nullable v, Mem nullable mem);
static void* ArrayPop(Array* a);
void         ArrayRemove(Array* a, u32 start, u32 count);
ssize_t      ArrayIndexOf(Array* a, void* nullable entry); // -1 on failure
ssize_t      ArrayLastIndexOf(Array* a, void* nullable entry); // -1 on failure

// ArrayCopy copies src of srclen to a, starting at a.v[start], growing a if needed using m.
void ArrayCopy(Array* a, u32 start, const void* src, u32 srclen, Mem nullable m);

// The comparison function must return an integer less than, equal to, or greater than zero if
// the first argument is considered to be respectively less than, equal to, or greater than the
// second.
typedef int (*ArraySortFun)(const void* elem1, const void* elem2, void* nullable userdata);

// ArraySort sorts the array in place using comparator to rank entries
void ArraySort(Array* a, ArraySortFun comparator, void* nullable userdata);

// Macros:
//   ArrayForEach(Array* a, TYPE elemtype, NAME elemname) <body>
//

// ------------------------------------------------------------------------------------------------
// inline implementations

inline static void ArrayInit(Array* a) {
  a->v = 0;
  a->cap = 0;
  a->len = 0;
  a->onheap = true;
}

inline static void ArrayInitWithStorage(Array* a, void* ptr, u32 cap){
  a->v = ptr;
  a->cap = cap;
  a->len = 0;
  a->onheap = false;
}

inline static void ArrayFree(Array* a, Mem nullable mem) {
  if (a->onheap) {
    memfree(mem, a->v);
    #if DEBUG
    a->v = NULL;
    a->cap = 0;
    #endif
  }
}

inline static void ArrayPush(Array* a, void* nullable v, Mem nullable mem) {
  if (a->len == a->cap)
    ArrayGrow(a, 1, mem);
  a->v[a->len++] = v;
}

inline static void* ArrayPop(Array* a) {
  return a->len > 0 ? a->v[--a->len] : NULL;
}

#define ArrayForEach(a, ELEMTYPE, LOCALNAME)        \
  /* this for introduces LOCALNAME */               \
  for (auto LOCALNAME = (ELEMTYPE)(a)->v[0];        \
       LOCALNAME == (ELEMTYPE)(a)->v[0];            \
       LOCALNAME++)                                 \
  /* actual for loop */                             \
  for (                                             \
    u32 LOCALNAME##__i = 0,                         \
        LOCALNAME##__end = (a)->len;                \
    LOCALNAME = (ELEMTYPE)(a)->v[LOCALNAME##__i],   \
    LOCALNAME##__i < LOCALNAME##__end;              \
    LOCALNAME##__i++                                \
  ) /* <body should follow here> */

ASSUME_NONNULL_END
