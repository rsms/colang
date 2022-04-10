// memory management
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// memory allocator type
typedef struct Mem Mem;

//——— allocation management functions

// core allocator functions
static void* nullable mem_alloc(Mem m, usize size);
static void* nullable mem_resize(Mem m, void* nullable p, usize oldsize, usize newsize);
static void mem_free(Mem m, void* p, usize size);
// *x versions returns effective potentially adjusted size (always >= input value)
static void* nullable mem_allocx(Mem m, usize* size);
static void* nullable mem_resizex(Mem m, void* nullable p, usize oldsize, usize* newsize);

// mem_allocz returns zeroed memory (calls mem_alloc and then memset(p,0,size))
void* nullable mem_allocz(Mem m, usize size) ATTR_MALLOC WARN_UNUSED_RESULT;

// T* nullable mem_alloct(Mem mem, type T)
// mem_alloct allocates memory the size of TYPE, returning a pointer of TYPE*
#define mem_alloct(mem, TYPE) ((TYPE*)mem_alloc((mem),sizeof(TYPE)))

// T* nullable memalloczt(Mem mem, type T)
// Like mem_alloct but returns zeroed memory.
#define mem_alloczt(mem, TYPE) ((TYPE*)mem_allocz((mem),sizeof(TYPE)))

// mem_allocv behaves similar to libc calloc, checking elemsize*count for overflow.
// Returns NULL on overflow or allocation failure.
void* nullable mem_allocv(Mem m, usize elemsize, usize count)
  ATTR_MALLOC WARN_UNUSED_RESULT;

// mem_alloczv is like mem_allocv but zeroes all memory using memset(p,0,size)
void* nullable mem_alloczv(Mem m, usize elemsize, usize count)
  ATTR_MALLOC WARN_UNUSED_RESULT;

// mem_resizev resizes an array, checking elemsize*newcount for overflow
void* nullable mem_resizev(
  Mem m, void* nullable p, usize elemsize, usize oldcount, usize newcount)
  ATTR_MALLOC WARN_UNUSED_RESULT;

// mem_strdup is like strdup but uses m
char* nullable mem_strdup(Mem m, const char* cstr);

//——— allocators

// mem_mkalloc_libc returns the shared libc allocator (using malloc, realloc and free.)
static Mem mem_mkalloc_libc();

// mem_mkalloc_buf creates an allocator using nbytes-MEM_BUFALLOC_OVERHEAD bytes from buf.
// Note: The address buf and size may be adjusted to pointer-size alignment.
Mem mem_mkalloc_buf(void* buf, usize nbytes);
#define MEM_BUFALLOC_OVERHEAD (sizeof(void*)*4)

// mem_mkalloc_vm creates an allocator backed by pages of system-managed virtual memory.
// If nbytes=USIZE_MAX, the largest possible allocation is created.
// On failure, the returned allocator's state is NULL.
Mem mem_mkalloc_vm(usize nbytes);
void mem_freealloc_vm(Mem); // release virtual memory to system; invalidates allocator

// mem_mkalloc_null creates an allocator which fails to allocate any size
static Mem mem_mkalloc_null();

//——— contextual allocation

static void* nullable memalloc(usize size);
static void* nullable memresize(void* nullable p, usize oldsize, usize newsize);
static           void memfree(void* p, usize size);
static void* nullable memallocx(usize* size);
static void* nullable memresizex(void* nullable p, usize oldsize, usize* newsize);
#define memallocz(args...)  mem_allocz(mem_ctx(),args)
#define memalloct(TYPE)     ((TYPE*)memalloc(sizeof(TYPE)))
#define memalloczt(TYPE)    ((TYPE*)memallocz(sizeof(TYPE)))
#define memallocv(args...)  mem_allocv(mem_ctx(),args)
#define memalloczv(args...) mem_alloczv(mem_ctx(),args)
#define memresizev(args...) mem_resizev(mem_ctx(),args)
#define memstrdup(args...)  mem_strdup(mem_ctx(),args)

static Mem mem_ctx();
static Mem mem_ctx_set(Mem m); // returns previous memory allocator

// mem_ctx_set_scope(m) -- sets m as mem_ctx until leaving the current scope.
// mem_ctx_scope(m) -- preceeds a statement or block, defines a new scope.
// Examples:
//   void foo() {
//     // mem_ctx() is whatever it was when calling foo
//     mem_ctx_set_scope(m);
//     // mem_ctx() is m here
//   } // mem_ctx() is restored when leaving foo()
//   void foo() {
//     // mem_ctx() is whatever it was when calling foo
//     mem_ctx_scope(m) {
//       ... // mem_ctx() is m here
//     } // mem_ctx() is restored when leaving the block or returning from current function
//   }
#if __has_attribute(cleanup)
  #define mem_ctx_set_scope(mem) \
    __attribute__((cleanup(_mem_ctx_scope_cleanup))) \
    UNUSED Mem CONCAT(_tmp,__COUNTER__) = mem_ctx_set(mem)
  #define mem_ctx_scope(mem) for ( \
    __attribute__((cleanup(_mem_ctx_scope_cleanup))) UNUSED Mem _memprev = mem_ctx_set(mem);\
    _memprev.a; \
    mem_ctx_set(_memprev), _memprev.a=NULL )
  void _mem_ctx_scope_cleanup(Mem* prev);
#else
  #define mem_ctx_set_scope(mem) \
    compiler does not support cleanup attribute
  #define mem_ctx_scope(mem) \
    compiler does not support cleanup attribute
#endif

//——— memory utility functions

usize mem_pagesize(); // get virtual memory page size in bytes (usually 4096 bytes)
void* nullable vmem_alloc(usize nbytes); // allocate virtual memory
bool vmem_free(void* ptr, usize nbytes); // free virtual memory

//———————————————————————————————————————————————————————————————————————————————————————
// internal interface

struct Mem {
  // a: allocator function
  //   ba_alloc(s, NULL,       0, newsize) = new allocation
  //   ba_alloc(s,    p, oldsize, newsize) = resize allocation
  //   ba_alloc(s,    p, oldsize,    NULL) = free allocation
  void* nullable (*a)(void* state, void* nullable p, usize oldsize, usize* nullable newsize);
  void* nullable state;
};

ATTR_MALLOC WARN_UNUSED_RESULT
inline static void* nullable mem_alloc(Mem m, usize size) {
  return m.a(m.state, NULL, 0, &size);
}

ATTR_MALLOC WARN_UNUSED_RESULT
inline static void* nullable mem_allocx(Mem m, usize* size) {
  return m.a(m.state, NULL, 0, size);
}

ATTR_MALLOC WARN_UNUSED_RESULT
inline static void* nullable mem_resize(
  Mem m, void* nullable p, usize oldsize, usize newsize)
{
  return m.a(m.state, p, oldsize, &newsize);
}

ATTR_MALLOC WARN_UNUSED_RESULT
inline static void* nullable mem_resizex(
  Mem m, void* nullable p, usize oldsize, usize* newsize)
{
  return m.a(m.state, p, oldsize, newsize);
}

inline static void mem_free(Mem m, void* p, usize size) {
  #if __has_attribute(unused)
  __attribute__((unused))
  #endif
  void* _ = m.a(m.state, p, size, NULL);
}


extern _Thread_local Mem _mem_ctx;
inline static Mem mem_ctx() { return _mem_ctx; }
inline static Mem mem_ctx_set(Mem m) { Mem prev = _mem_ctx; _mem_ctx = m; return prev; }


inline static void* nullable memalloc(usize size) {
  return mem_alloc(mem_ctx(), size);
}
inline static void* nullable memresize(void* nullable p, usize oldsize, usize newsize) {
  return mem_resize(mem_ctx(), p, oldsize, newsize);
}
inline static void memfree(void* p, usize size) {
  return mem_free(mem_ctx(), p, size);
}
inline static void* nullable memallocx(usize* size) {
  return mem_allocx(mem_ctx(), size);
}
inline static void* nullable memresizex(void* nullable p, usize oldsize, usize* newsize) {
  return mem_resizex(mem_ctx(), p, oldsize, newsize);
}


void* nullable _mem_libc_alloc(
  void* state, void* nullable, usize oldsize, usize* nullable newsize);
inline static Mem mem_mkalloc_libc() {
  return (Mem){ &_mem_libc_alloc, /*for consistency*/&_mem_libc_alloc };
}

void* nullable _mem_null_alloc(
  void* _, void* nullable p, usize oldsize, usize* nullable newsize);
static Mem mem_mkalloc_null() {
  return (Mem){&_mem_null_alloc, NULL};
}

END_INTERFACE
