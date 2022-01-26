// Mem -- heap memory allocator
#pragma once
ASSUME_NONNULL_BEGIN

typedef struct MemAllocator MemAllocator;
typedef const MemAllocator* nonull Mem;

// memalloc allocates memory of size bytes
static void* nullable memalloc(Mem m, usize size)
ATTR_MALLOC ATTR_ALLOC_SIZE(2) WARN_UNUSED_RESULT;

// T* nullable memalloct(Mem mem, type T)
// memalloct allocates memory the size of TYPE, returning a pointer of TYPE*
#define memalloct(mem, TYPE) ((TYPE*)memalloc((mem),sizeof(TYPE)))

// void* nullable memallocv(Mem mem, uint ELEMSIZE, uint COUNT)
// memallocv behaves similar to libc calloc, checking ELEMSIZE*COUNT for overflow.
// Returns NULL on overflow or allocation failure.
#define memallocv(mem, ELEMSIZE, COUNT) ({    \
  usize z = array_size((ELEMSIZE), (COUNT));  \
  z == USIZE_MAX ? NULL : memalloc((mem), z); \
})

// T* nullable memalloctv(Mem mem, type T, name VFIELD_NAME, uint VCOUNT)
// memalloctv allocates memory for struct type TYPE with variable number of tail elements.
// Returns NULL on overflow or allocation failure.
// e.g.
//   struct foo { int c; u8 v[]; }
//   struct foo* p = memalloctv(mem, struct foo, v, 3);
#define memalloctv(mem, TYPE, VFIELD_NAME, VCOUNT) ({         \
  usize z = STRUCT_SIZE( ((TYPE*)0), VFIELD_NAME, (VCOUNT) ); \
  z == USIZE_MAX ? NULL : (TYPE*)memalloc((mem), z);          \
})

// memrealloc resizes memory at ptr. If ptr is null, the behavior matches memalloc.
static void* nullable memrealloc(Mem m, void* nullable ptr, usize newsize)
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

  // realloc is called with the address of a previous allocation of the same allocator m.
  // It should either extend the contiguous memory segment at ptr to be at least newsize
  // long in total, or allocate a new contiguous memory of at least newsize.
  // If it's unable to fulfill the request it should return NULL.
  // Note that ptr is never NULL; calls to memrealloc with a NULL ptr are routed to alloc.
  void* nullable (* nonull realloc)(Mem m, void* nullable ptr, usize newsize);

  // free is called with the address of a previous allocation of the same allocator m.
  // The allocator now owns the memory at ptr and may recycle it or release it to the system.
  void (* nonull free)(Mem m, void* nonull ptr);
} MemAllocator;


// user-buffer allocator
typedef struct MemBufAllocator {
  MemAllocator a;
  u8*   buf;
  usize cap;
  usize len;
} MemBufAllocator;

Mem mem_buf_allocator_init(MemBufAllocator* a, void* buf, usize size);

#define DEF_MEM_STACK_BUF_ALLOCATOR(name, buf)                       \
  MemBufAllocator _memstk_a = {0};                                   \
  Mem name = mem_buf_allocator_init(&_memstk_a, (buf), sizeof(buf));


// invalid allocator (panic on allocation)
static Mem mem_nil_allocator();
static void* nullable _mem_nil_alloc(Mem _, usize size) {
  panic("attempt to allocate memory with nil allocator");
  return NULL;
}
static void* nullable _mem_nil_realloc(Mem m, void* nullable ptr, usize newsize) {
  return _mem_nil_alloc(m, 0);
}
static void _mem_nil_free(Mem _, void* nonull ptr) {}
static const MemAllocator _mem_nil = {
  .alloc   = _mem_nil_alloc,
  .realloc = _mem_nil_realloc,
  .free    = _mem_nil_free,
};
inline static Mem mem_nil_allocator() {
  return &_mem_nil;
}


// libc allocator
#ifdef CO_WITH_LIBC
ASSUME_NONNULL_END
#include <stdlib.h>
ASSUME_NONNULL_BEGIN

// mem_libc_allocator returns a shared libc allocator (malloc, realloc & free from libc)
static Mem mem_libc_allocator();

/*
Note on mem_libc_allocator runtime overhead:
  Recent versions of GCC or Clang will optimize calls to memalloc(mem_libc_allocator(), z) into
  direct calls to the clib functions and completely eliminate the code declared in this file.
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
static void* nullable _mem_libc_realloc(Mem _, void* nullable ptr, usize newsize) {
  return realloc(ptr, newsize);
}
static void _mem_libc_free(Mem _, void* nonull ptr) {
  free(ptr);
}
static const MemAllocator _mem_libc = {
  .alloc   = _mem_libc_alloc,
  .realloc = _mem_libc_realloc,
  .free    = _mem_libc_free,
};
inline static Mem mem_libc_allocator() {
  return &_mem_libc;
}

#endif // defined(CO_WITH_LIBC)

inline static void* nullable memalloc(Mem m, usize size) {
  assert(m != NULL);
  void* p = m->alloc(m, size);
  #ifdef CO_MEM_DEBUG_ALLOCATIONS
  dlog("[co memalloc] %p-%p (%zu)", p, p + size, size);
  #endif
  return p;
}

inline static void* nullable memrealloc(Mem m, void* nullable ptr, usize newsize) {
  assert(m != NULL);
  void* p = ptr ? m->realloc(m, ptr, newsize) : m->alloc(m, newsize);
  #ifdef CO_MEM_DEBUG_ALLOCATIONS
  dlog("[co realloc] %p -> %p-%p (%zu)", ptr, p, p + newsize, newsize);
  #endif
  return p;
}

// memfree frees memory allocated with memalloc
inline static void memfree(Mem m, void* nonull ptr) {
  assert(m != NULL);
  assert(ptr != NULL);
  #ifdef CO_MEM_DEBUG_ALLOCATIONS
  dlog("[co memfree] %p", ptr);
  #endif
  m->free(m, ptr);
}

ASSUME_NONNULL_END
