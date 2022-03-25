// Mem -- heap memory allocator
#pragma once
#if DISABLED
ASSUME_NONNULL_BEGIN

// CO_MEM_DEBUG_ALLOCATIONS   -- define to trace log allocation activity
// CO_MEM_DEBUG_PANIC_ON_FAIL -- define to panic on allocation failure
//#define CO_MEM_DEBUG_ALLOCATIONS
//#define CO_MEM_DEBUG_PANIC_ON_FAIL

typedef struct MemAllocator MemAllocator;
typedef const MemAllocator* nonull Mem;

// memalloc allocates memory of size bytes
static void* nullable memalloc(Mem m, usize size)
  ATTR_MALLOC ATTR_ALLOC_SIZE(2) WARN_UNUSED_RESULT;

// memallocz allocates zero-initialized memory of size bytes
static void* nullable memallocz(Mem m, usize size)
  ATTR_MALLOC ATTR_ALLOC_SIZE(2) WARN_UNUSED_RESULT;

// memallocv behaves similar to libc calloc, checking ELEMSIZE*COUNT for overflow.
// Returns NULL on overflow or allocation failure.
static void* nullable memallocv(Mem m, usize elemsize, usize count)
  ATTR_MALLOC ATTR_ALLOC_SIZE(2, 3) WARN_UNUSED_RESULT;

// memalloczv is like memallocv but returns zeroed memory
static void* nullable memalloczv(Mem m, usize elemsize, usize count)
  ATTR_MALLOC ATTR_ALLOC_SIZE(2, 3) WARN_UNUSED_RESULT;

// T* nullable memalloct(Mem mem, type T)
// memalloct allocates memory the size of TYPE, returning a pointer of TYPE*
#define memalloct(mem, TYPE) ((TYPE*)memalloc((mem),sizeof(TYPE)))

// T* nullable memalloczt(Mem mem, type T)
// Like memalloct but returns zeroed memory.
#define memalloczt(mem, TYPE) ((TYPE*)memallocz((mem),sizeof(TYPE)))

// T* nullable memalloctv(Mem mem, type T, name VFIELD_NAME, uint VCOUNT)
// memalloctv allocates memory for struct type TYPE with variable number of tail elements.
// Returns NULL on overflow or allocation failure.
// e.g.
//   struct foo { int c; u8 v[]; }
//   struct foo* p = memalloctv(mem, struct foo, v, 3);
#define memalloctv(mem, TYPE, VFIELD_NAME, VCOUNT) ({          \
  usize z = STRUCT_SIZE( ((TYPE*)0), VFIELD_NAME, (VCOUNT) );  \
  UNLIKELY(z == USIZE_MAX) ? NULL : (TYPE*)memalloc((mem), z); \
})

// T* nullable memallocztv(Mem mem, type T, name VFIELD_NAME, uint VCOUNT)
// Like memalloctv but returns zeroed memory.
#define memallocztv(mem, TYPE, VFIELD_NAME, VCOUNT) ({          \
  usize z = STRUCT_SIZE( ((TYPE*)0), VFIELD_NAME, (VCOUNT) );   \
  UNLIKELY(z == USIZE_MAX) ? NULL : (TYPE*)memallocz((mem), z); \
})

// memresize resizes memory at ptr. If ptr is null, the behavior matches memalloc.
static void* nullable memresize(Mem m, void* nullable ptr, usize newsize)
  ATTR_ALLOC_SIZE(3) WARN_UNUSED_RESULT;

// memfree frees memory allocated with memalloc
static void memfree(Mem m, void* nonull ptr);

// memdup2 makes a copy of src with optional extraspace at the end.
void* nullable mem_dup2(Mem m, const void* src, usize srclen, usize extraspace);

// memdup makes a copy of src
inline static void* nullable mem_dup(Mem m, const void* src, usize len) {
  return mem_dup2(m, src, len, 0);
}

// memstrdup is like strdup but uses m
char* nullable mem_strdup(Mem m, const char* cstr);

// MemAllocator is the implementation interface for an allocator.
typedef struct MemAllocator {
  // alloc should allocate at least size contiguous memory and return the address.
  // If it's unable to do so it should return NULL.
  void* nullable (* nonull alloc)(Mem m, usize size);

  // allocz is like alloc but must return zeroed memory
  void* nullable (* nonull allocz)(Mem m, usize size);

  // resize is called with the address of a previous allocation of the same allocator m.
  // It should either extend the contiguous memory segment at ptr to be at least newsize
  // long in total, or allocate a new contiguous memory of at least newsize.
  // If it's unable to fulfill the request it should return NULL.
  // Note that ptr is never NULL; calls to memresize with a NULL ptr are routed to alloc.
  void* nullable (* nonull resize)(Mem m, void* nullable ptr, usize newsize);

  // free is called with the address of a previous allocation of the same allocator m.
  // The allocator now owns the memory at ptr and may recycle it or release it.
  void (* nonull free)(Mem m, void* nonull ptr);
} MemAllocator;

// --------------------------------------------------------------------------------------
// FixBufAllocator -- fixed size-buffer allocator.
// This allocator is primarily useful for testing out of memory conditions.
// It is inefficient & slow with high fragmentation.
// It is really fast if the allocation pattern is a=alloc();b=alloc();free(b);free(a),
// and so can be useful for "real" code that is known to either use that pattern or to
// never call memfree or memresize (in which case you can avoid all freeing costs.)
typedef struct FixBufAllocator FixBufAllocator;

struct FixBufAllocator {
  MemAllocator   a;     // Mem callbacks
  void*          buf;   // memory
  usize          cap;   // size of buf
  usize          len;   // nbytes of buf bound up in use or free blocks
  void* nullable use;   // in-use list head, ordered by recency (most recent at head)
  void* nullable free;  // free list head, ordered by memory address
  u32            flags;
};

// FixBufAllocatorInit initializes a to use buf of size bytes as storage.
// This can also be used to "reset" or "clear" a previously used FBA.
Mem FixBufAllocatorInit(FixBufAllocator* a, void* buf, usize size);

// FixBufAllocatorInitz is like FixBufAllocatorInit but assumes that buf is zeroed
// and thus avoids a zeroing memset call.
Mem FixBufAllocatorInitz(FixBufAllocator* a, void* buf, usize size);

#define DEF_STACK_FixBufAllocator(NAME, buf) \
  FixBufAllocator memfba__;                  \
  Mem NAME = FixBufAllocatorInit(&memfba__, (buf), sizeof(buf));

// minimum buffer size needed to be able to complete a single allocation
#define kFixBufAllocatorMinCap (sizeof(void*)*3)

// number of extra bytes needed per allocation, used for testing
#define kFixBufAllocatorOverhead (sizeof(void*)*2)

// alignment of all allocations
// thus, actual allocation size is
//   ALIGN2(size + kFixBufAllocatorOverhead, kFixBufAllocatorAlign)
#define kFixBufAllocatorAlign sizeof(void*)

// --------------------------------------------------------------------------------------
// invalid allocator (panic on allocation)
static Mem mem_nil_allocator();
static void* nullable _mem_nil_alloc(Mem _, usize size) {
  assertf(0,"attempt to allocate memory with nil allocator");
  return NULL;
}
static void* nullable _mem_nil_resize(Mem m, void* nullable ptr, usize newsize) {
  return _mem_nil_alloc(m, 0);
}
static void _mem_nil_free(Mem _, void* nonull ptr) {}
static const MemAllocator _mem_nil = {
  .alloc  = _mem_nil_alloc,
  .allocz = _mem_nil_alloc,
  .resize = _mem_nil_resize,
  .free   = _mem_nil_free,
};
inline static Mem mem_nil_allocator() {
  return &_mem_nil;
}

// --------------------------------------------------------------------------------------
// libc allocator
#ifndef CO_NO_LIBC
ASSUME_NONNULL_END
#include <stdlib.h>
ASSUME_NONNULL_BEGIN

// mem_libc_allocator returns a shared libc allocator (malloc, realloc & free from libc)
static Mem mem_libc_allocator();

/*
Note on mem_libc_allocator runtime overhead:
  Recent versions of GCC or Clang will optimize calls to memalloc(mem_libc_allocator(), z)
  into direct calls to the clib functions and completely eliminate the code declared in
  this file.
Example code generation:
  int main() {
    void* p = memalloc(mem_libc_allocator(), 12);
    return (int)p;
  }
  —————————————————————————————————————————————————————————
  x86_64 clang-12 -O2:  | arm64 clang-11 -O2:
    main:               |   main:
      mov     edi, 1    |     stp     x29, x30, [sp, #-16]!
      mov     esi, 12   |     mov     x29, sp
      jmp     calloc    |     mov     w0, #1
                        |     mov     w1, #12
                        |     bl      calloc
                        |     ldp     x29, x30, [sp], #16
                        |     ret
  —————————————————————————————————————————————————————————
  x86_64 gcc-11 -O2:    | arm64 gcc-11 -O2:
    main:               |   main:
      sub     rsp, 8    |     stp     x29, x30, [sp, -16]!
      mov     esi, 12   |     mov     x1, 12
      mov     edi, 1    |     mov     x0, 1
      call    calloc    |     mov     x29, sp
      add     rsp, 8    |     bl      calloc
      ret               |     ldp     x29, x30, [sp], 16
                        |     ret
  —————————————————————————————————————————————————————————
  https://godbolt.org/z/MK757acnK
*/
static void* nullable _mem_libc_alloc(Mem _, usize size) {
  return malloc(size);
}
static void* nullable _mem_libc_allocz(Mem _, usize size) {
  return calloc(1, size);
}
static void* nullable _mem_libc_resize(Mem _, void* nullable ptr, usize newsize) {
  return realloc(ptr, newsize);
}
static void _mem_libc_free(Mem _, void* nonull ptr) {
  free(ptr);
}
static const MemAllocator _mem_libc = {
  .alloc  = _mem_libc_alloc,
  .allocz = _mem_libc_allocz,
  .resize = _mem_libc_resize,
  .free   = _mem_libc_free,
};
inline static Mem mem_libc_allocator() {
  return &_mem_libc;
}

#endif // !defined(CO_NO_LIBC)

// --------------------------------------------------------------------------------------
// inline implementations

inline static void* nullable memalloc(Mem m, usize size) {
  return assertnotnull(assertnotnull(m)->alloc)(m, size);
}
inline static void* nullable memallocz(Mem m, usize size) {
  return assertnotnull(assertnotnull(m)->allocz)(m, size);
}
inline static void* nullable memresize(Mem m, void* nullable p, usize newsize) {
  return assertnotnull(assertnotnull(m)->resize)(m, p, newsize);
}
inline static void memfree(Mem m, void* p) {
  assertnotnull(assertnotnull(m)->free)(m, assertnotnull(p));
}

inline static void* nullable memallocv(Mem m, usize elemsize, usize count) {
  usize z = array_size(elemsize, count);
  return UNLIKELY(z == USIZE_MAX) ? NULL : memalloc(m, z);
}
inline static void* nullable memalloczv(Mem m, usize elemsize, usize count) {
  usize z = array_size(elemsize, count);
  return UNLIKELY(z == USIZE_MAX) ? NULL : memallocz(m, z);
}

// functions w/o dedicated _trace_* substitutes must go after the following
// CO_MEM_DEBUG_ALLOCATIONS defines.

#ifdef CO_MEM_DEBUG_ALLOCATIONS
  // note: order matters since we shadow functions with preprocessor names

  #ifdef CO_MEM_DEBUG_PANIC_ON_FAIL
    #define _MEM_DEBUG_FAIL panic
  #else
    #define _MEM_DEBUG_FAIL log
  #endif

  inline static void* nullable
  _trace_memallocv(Mem m, usize elemsize, usize count, const char* file, int line) {
    usize z = array_size(elemsize, count);
    if UNLIKELY(z == USIZE_MAX) {
      _MEM_DEBUG_FAIL("[memallocv] overflow (%zu * %zu) %s:%d",
        elemsize, count, file, line);
      return NULL;
    }
    void* p = memalloc(m, z);
    if UNLIKELY(!p) {
      _MEM_DEBUG_FAIL("[memallocv] FAIL (%zu * %zu = %zu B) %s:%d",
        elemsize, count, z, file, line);
      return NULL;
    }
    log("[memallocv] %p .. %p (%zu * %zu = %zu B) %s:%d",
      p, p + z, elemsize, count, z, file, line);
    return p;
  }

  inline static void* nullable
  _trace_memalloczv(Mem m, usize elemsize, usize count, const char* file, int line) {
    usize z = array_size(elemsize, count);
    if UNLIKELY(z == USIZE_MAX) {
      _MEM_DEBUG_FAIL("[memalloczv] overflow (%zu * %zu) %s:%d",
        elemsize, count, file, line);
      return NULL;
    }
    void* p = memallocz(m, z);
    if UNLIKELY(!p) {
      _MEM_DEBUG_FAIL("[memalloczv] FAIL (%zu * %zu = %zu B) %s:%d",
        elemsize, count, z, file, line);
      return NULL;
    }
    log("[memalloczv] %p .. %p (%zu * %zu = %zu B) %s:%d",
      p, p + z, elemsize, count, z, file, line);
    return p;
  }


  inline static void* nullable
  _trace_memalloc(Mem m, usize size, const char* file, int line) {
    void* p = memalloc(m, size);
    if UNLIKELY(!p) {
      _MEM_DEBUG_FAIL("[memalloc] FAIL (%zu B) %s:%d", size, file, line);
      return NULL;
    }
    log("[memalloc] %p .. %p (%zu B) %s:%d", p, p + size, size, file, line);
    return p;
  }

  inline static void* nullable
  _trace_memallocz(Mem m, usize size, const char* file, int line) {
    void* p = memallocz(m, size);
    if UNLIKELY(!p) {
      _MEM_DEBUG_FAIL("[memallocz] FAIL (%zu B) %s:%d", size, file, line);
      return NULL;
    }
    log("[memallocz] %p .. %p (%zu B) %s:%d", p, p + size, size, file, line);
    return p;
  }

  inline static void* nullable
  _trace_memresize(Mem m, void* nullable p, usize newsize, const char* file, int line) {
    void* newp = memresize(m, p, newsize);
    if UNLIKELY(!newp) {
      _MEM_DEBUG_FAIL("[memresize] FAIL %p -> NULL (%zu B) %s:%d",
        p, newsize, file, line);
      return NULL;
    }
    log("[memresize] %p -> %p .. %p (%zu B) %s:%d",
      p, newp, newp + newsize, newsize, file, line);
    return newp;
  }

  inline static void _trace_memfree(Mem m, void* p, const char* file, int line) {
    log("[memfree] %p %s:%d", p, file, line);
    memfree(m, p);
  }

  #define memalloc(m, z)      _trace_memalloc((m),(z),__FILE__,__LINE__)
  #define memallocz(m, z)     _trace_memallocz((m),(z),__FILE__,__LINE__)
  #define memresize(m, p, z) _trace_memresize((m),(p),(z),__FILE__,__LINE__)
  #define memfree(m, p)       _trace_memfree((m),(p),__FILE__,__LINE__)
  #define memallocv(m,e,c)    _trace_memallocv((m),(e),(c),__FILE__,__LINE__)
  #define memalloczv(m,e,c)   _trace_memalloczv((m),(e),(c),__FILE__,__LINE__)
#elif defined(CO_MEM_DEBUG_PANIC_ON_FAIL)
  #undef CO_MEM_DEBUG_PANIC_ON_FAIL
  #warning CO_MEM_DEBUG_PANIC_ON_FAIL without CO_MEM_DEBUG_ALLOCATIONS has no effect
#endif


// functions w/o dedicated _trace_* substitutes must go here, after the above
// CO_MEM_DEBUG_ALLOCATIONS defines.

ASSUME_NONNULL_END
#endif
