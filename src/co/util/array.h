#pragma once
#include <alloca.h>
ASSUME_NONNULL_BEGIN

// Array is a dynamic linear container. Valid when zero-initialized.
typedef struct Array {
  void** v;       // entries
  u32    cap;     // capacity of v
  u32    len;     // valid entries at v
  bool   onstack; // true if v is space on stack
} Array;

// TODO: move onstack flag into a single bit of len or cap

#define CONCAT_(x,y) x##y
#define CONCAT(x,y)  CONCAT_(x,y)

#define Array_INIT { NULL, 0, 0, false }
#define Array_INIT_WITH_STORAGE(storage, initcap) { (storage), (initcap), 0, true }

// #define Array_INIT_ON_STACK(initcap) \
//   ((Array){ alloca(initcap * sizeof(void*)), initcap, 0, true })

static void  ArrayInit(Array* a);
static void  ArrayInitWithStorage(Array* a, void* storage, u32 storagecap);
static void  ArrayFree(Array* a, Mem mem);
static void  ArrayClear(Array* a); // sets len to 0
void         ArrayGrow(Array* a, size_t addl, Mem mem); // cap=align2(len+addl)
static void  ArrayPush(Array* a, void* nullable v, Mem mem);
static void* ArrayPop(Array* a);
void         ArrayRemove(Array* a, u32 start, u32 count);
ssize_t      ArrayIndexOf(Array* a, void* nullable entry); // -1 on failure
ssize_t      ArrayLastIndexOf(Array* a, void* nullable entry); // -1 on failure

// ArrayCopy copies src of srclen to a, starting at a.v[start], growing a if needed using m.
void ArrayCopy(Array* a, u32 start, const void* src, u32 srclen, Mem m);

// The comparison function must return an integer less than, equal to, or greater than zero if
// the first argument is considered to be respectively less than, equal to, or greater than the
// second.
typedef int (*ArraySortFun)(const void* elem1, const void* elem2, void* nullable userdata);

// ArraySort sorts the array in place using comparator to rank entries
void ArraySort(Array* a, ArraySortFun comparator, void* nullable userdata);

// Macros:
//   ArrayForEach(Array* a, TYPE elemtype, NAME elemname) <body>
//

// -----------------------------------------------------------------------------------------

#define TARRAY_TYPE(ELEMT,INITN)                  \
  struct Array_##ELEMT {                          \
    ELEMT* v;           /* entries */             \
    u32    cap;         /* capacity of v */       \
    u32    len;         /* valid entries at v */  \
    ELEMT  init[INITN]; /* storage */             \
  }

#define TARRAY_INIT(ptr) do { \
  auto a = (ptr);             \
  a->v = a->init;             \
  a->cap = countof(a->init);  \
  a->len = 0;                 \
} while(0)

#define TARRAY_AT(ptr, index) ({      \
  auto tmp_a_ = (ptr);                \
  auto tmp_i_ = (index);              \
  assert_debug(tmp_i_ < tmp_a_->len); \
  tmp_a_->v[tmp_i_];                  \
})

#define TARRAY_APPEND(ptr, mem, value) ({                      \
  auto tmp_a_ = (ptr);                                         \
  if (R_UNLIKELY(tmp_a_->len == tmp_a_->cap)) {                \
    Mem tmp_mem_ = (mem);                                      \
    TArrayGrow((void**)&tmp_a_->v, tmp_a_->init, &tmp_a_->cap, \
               sizeof(tmp_a_->v[0]), tmp_mem_);                \
  }                                                            \
  tmp_a_->v[tmp_a_->len++] = (value);                          \
})

#define TARRAY_DISPOSE(ptr, mem) ({   \
  auto tmp_a_ = (ptr);                \
  if (tmp_a_->v != tmp_a_->init)      \
    memfree((mem), tmp_a_->v);        \
  assert((tmp_a_->v = NULL) == NULL); \
  assert((tmp_a_->cap = 0) == 0);     \
  assert((tmp_a_->len = 0) == 0);     \
})

void TArrayGrow(void** v, const void* init, u32* cap, size_t elemsize, Mem mem);


// -----------------------------------------------------------------------------------------
// inline implementations

inline static void ArrayInit(Array* a) {
  a->v = NULL;
  a->cap = 0;
  a->len = 0;
  a->onstack = false;
}

inline static void ArrayInitWithStorage(Array* a, void* ptr, u32 cap) {
  a->v = (void**)ptr;
  a->cap = cap;
  a->len = 0;
  a->onstack = true;
}

inline static void ArrayFree(Array* a, Mem mem) {
  if (!a->onstack && a->v != NULL)
    memfree(mem, a->v);
  #if DEBUG
  memset(a, 0, sizeof(*a));
  #endif
}

ALWAYS_INLINE static void ArrayClear(Array* a) {
  a->len = 0;
}

ALWAYS_INLINE static void ArrayPush(Array* a, void* nullable v, Mem mem) {
  if (R_UNLIKELY(a->len == a->cap))
    ArrayGrow(a, 1, mem);
  a->v[a->len++] = v;
}

ALWAYS_INLINE static void* ArrayPop(Array* a) {
  assertop(a->len,>,0);
  return a->v[--a->len];
}

// ArrayForEach(const Array* a, ELEMTYPE, LOCALNAME) { use LOCALNAME }
//
// Note: It may be easier to just use a for loop:
//   for (u32 i = 0; i < a->len; i++) { auto ent = a->v[i]; ... }
//
#define ArrayForEach(a, ELEMTYPE, LOCALNAME)        \
  /* avoid LOCALNAME clashing with a */             \
  for (const Array* a__ = (a), * a1__ = (a); a__ == a1__; a1__ = NULL) \
    /* this for introduces LOCALNAME */               \
    for (auto LOCALNAME = (ELEMTYPE)a__->v[0];        \
         LOCALNAME == (ELEMTYPE)a__->v[0];            \
         LOCALNAME++)                                 \
      /* actual for loop */                           \
      for (                                           \
        u32 LOCALNAME##__i = 0,                       \
            LOCALNAME##__end = a__->len;              \
        LOCALNAME = (ELEMTYPE)a__->v[LOCALNAME##__i], \
        LOCALNAME##__i < LOCALNAME##__end;            \
        LOCALNAME##__i++                              \
      ) /* <body should follow here> */

ASSUME_NONNULL_END
