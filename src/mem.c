// memory management
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define MEM_IMPLEMENTATION
#endif
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

//——— memory utility functions

usize mem_pagesize(); // get virtual memory page size in bytes (usually 4096 bytes)
void* nullable vmem_alloc(usize nbytes); // allocate virtual memory
bool vmem_free(void* ptr, usize nbytes); // free virtual memory

// mem_strdup is like strdup but uses m
char* nullable mem_strdup(Mem m, const char* cstr);

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

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef MEM_IMPLEMENTATION

#ifndef RSM_NO_LIBC
  #include <stdio.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <unistd.h>
  #include <stdlib.h> // free
  #include <execinfo.h> // backtrace* (for _panic)
  #include <sys/stat.h>
  #include <sys/mman.h> // mmap

  #ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #define MEM_PAGESIZE malloc_getpagesize
  #elif defined(malloc_getpagesize)
    #define MEM_PAGESIZE malloc_getpagesize
  #else
    #include <unistd.h>
    #ifdef _SC_PAGESIZE  /* some SVR4 systems omit an underscore */
      #ifndef _SC_PAGE_SIZE
        #define _SC_PAGE_SIZE _SC_PAGESIZE
      #endif
    #endif
    #ifdef _SC_PAGE_SIZE
      #define MEM_PAGESIZE sysconf(_SC_PAGE_SIZE)
    #elif defined(BSD) || defined(DGUX) || defined(R_HAVE_GETPAGESIZE)
      extern size_t getpagesize();
      #define MEM_PAGESIZE getpagesize()
    #else
      #include <sys/param.h>
      #ifdef EXEC_PAGESIZE
        #define MEM_PAGESIZE EXEC_PAGESIZE
      #elif defined(NBPG)
        #ifndef CLSIZE
          #define MEM_PAGESIZE NBPG
        #else
          #define MEM_PAGESIZE (NBPG * CLSIZE)
        #endif
      #elif defined(NBPC)
          #define MEM_PAGESIZE NBPC
      #elif defined(PAGESIZE)
        #define MEM_PAGESIZE PAGESIZE
      #endif
    #endif
    #include <sys/types.h>
    #include <sys/mman.h>
    #include <sys/resource.h>
    #if defined(__MACH__) && defined(__APPLE__)
      #include <mach/vm_statistics.h>
      #include <mach/vm_prot.h>
    #endif
    #ifndef MAP_ANON
      #define MAP_ANON MAP_ANONYMOUS
    #endif
    #define HAS_MMAP
  #endif // _WIN32
#endif // RSM_NO_LIBC
#ifndef MEM_PAGESIZE
  // fallback value (should match wasm32)
  #define MEM_PAGESIZE ((usize)4096U)
#endif

// --------------------------------------------------------------------------------------
// virtual memory functions

usize mem_pagesize() {
  return MEM_PAGESIZE;
}

void* nullable vmem_alloc(usize nbytes) {
  #ifndef HAS_MMAP
    return NULL;
  #else
    if (nbytes == 0)
      return NULL;

    #if defined(DEBUG) && defined(HAS_MPROTECT)
      usize nbytes2;
      if (check_add_overflow(nbytes, MEM_PAGESIZE, &nbytes2)) {
        // nbytes too large
        nbytes2 = 0;
      } else {
        nbytes += MEM_PAGESIZE;
      }
    #endif

    #if defined(__MACH__) && defined(__APPLE__) && defined(VM_PROT_DEFAULT)
      // vm_map_entry_is_reusable uses VM_PROT_DEFAULT as a condition for page reuse.
      // See http://fxr.watson.org/fxr/source/osfmk/vm/vm_map.c?v=xnu-2050.18.24#L10705
      int mmapprot = VM_PROT_DEFAULT;
    #else
      int mmapprot = PROT_READ | PROT_WRITE;
    #endif

    int mmapflags = MAP_PRIVATE | MAP_ANON
      #ifdef MAP_NOCACHE
      | MAP_NOCACHE // don't cache pages for this mapping
      #endif
      #ifdef MAP_NORESERVE
      | MAP_NORESERVE // don't reserve needed swap area
      #endif
    ;

    // note: VM_FLAGS_PURGABLE implies a 2GB allocation limit on macos 10
    // #if defined(__MACH__) && defined(__APPLE__) && defined(VM_FLAGS_PURGABLE)
    //   int fd = VM_FLAGS_PURGABLE; // Create a purgable VM object for new VM region
    // #else
    int fd = -1;

    void* ptr = mmap(0, nbytes, mmapprot, mmapflags, fd, 0);
    if UNLIKELY(ptr == MAP_FAILED)
      return NULL;

    // protect the last page from access to cause a crash on out of bounds access
    #if defined(DEBUG) && defined(HAS_MPROTECT)
      if (nbytes2 != 0) {
        const usize pagesize = MEM_PAGESIZE;
        assert(nbytes > pagesize);
        void* protPagePtr = ptr;
        protPagePtr = &((u8*)ptr)[nbytes - pagesize];
        int status = mprotect(protPagePtr, pagesize, PROT_NONE);
        if LIKELY(status == 0) {
          *nbytes = nbytes - pagesize;
        } else {
          dlog("mprotect failed");
        }
      }
    #endif

    return ptr;
  #endif // HAS_MMAP
}

bool vmem_free(void* ptr, usize nbytes) {
  #ifdef HAS_MMAP
    return munmap(ptr, nbytes) == 0;
  #else
    return false;
  #endif
}

// --------------------------------------------------------------------------------------
// libc allocator

void* nullable _mem_libc_alloc(
  void* state, void* nullable p, usize oldsize, usize* nullable newsize)
{
  #ifdef CO_NO_LIBC
    return NULL;
  #else
    if (p == NULL) {
      assertnotnull(newsize);
      return malloc(*newsize);
    }
    if (newsize)
      return realloc(p, *newsize);
    free(p);
    return NULL;
  #endif
}

// --------------------------------------------------------------------------------------
// null allocator

void* nullable _mem_null_alloc(
  void* _, void* nullable p, usize oldsize, usize* nullable newsize)
{
  *newsize = 0;
  return NULL;
}

// --------------------------------------------------------------------------------------
// fixed buffer-backed allocator

typedef struct BufAlloc {
  void* buf; // memory buffer
  usize len; // number of bytes used up in the memory buffer
  usize cap; // memory buffer size in bytes
  void* _reserved;
} BufAlloc;
static_assert(sizeof(BufAlloc) == MEM_BUFALLOC_OVERHEAD, "");

inline static bool ba_istail(BufAlloc* a, void* p, usize size) {
  return a->buf + a->len == p + size;
}

inline static usize ba_avail(BufAlloc* a) { // available capacity
  return a->cap - a->len;
}

// ba_alloc(s, NULL,       0, newsize) = new allocation
// ba_alloc(s,    p, oldsize, newsize) = resize allocation
// ba_alloc(s,    p, oldsize,       0) = free allocation
static void* nullable ba_alloc(
  void* state, void* nullable p, usize oldsize, usize* nullable newsize)
{
  BufAlloc* a = state;
  usize nz = newsize == NULL ? 0 : *newsize;
  if UNLIKELY(p != NULL) {
    oldsize = ALIGN2(oldsize, sizeof(void*));
    if LIKELY(nz == 0) {
      // free -- ba_alloc(s,p,>0,0)
      if (ba_istail(a, p, oldsize))
        a->len -= oldsize;
      return NULL; // ignored by caller
    }
    nz = ALIGN2(nz, sizeof(void*));
    *newsize = nz;
    // resize -- ba_alloc(s,p,>0,>0)
    assertnotnull(newsize);
    assert(oldsize > 0);
    if UNLIKELY(nz <= oldsize) {
      // shrink
      if (ba_istail(a, p, oldsize))
        a->len -= oldsize - nz;
      return p;
    }
    // grow
    if (ba_istail(a, p, oldsize)) {
      // extend
      if UNLIKELY(ba_avail(a) < nz - oldsize)
        return NULL; // out of memory
      a->len += nz - oldsize;
      return p;
    }
    // relocate
    void* p2 = ba_alloc(state, NULL, 0, newsize); // new allocation
    if UNLIKELY(p2 == NULL)
      return NULL;
    return memcpy(p2, p, oldsize);
  }
  // new -- ba_alloc(s,NULL,0,>0)
  assert(oldsize == 0);
  assertnotnull(newsize);
  assert(*newsize > 0);
  nz = ALIGN2(nz, sizeof(void*)); // ensure all allocations are address aligned
  if UNLIKELY(ba_avail(a) < nz)
    return NULL; // out of memory
  void* newp = a->buf + a->len;
  a->len += nz;
  *newsize = nz;
  return newp;
}

static Mem mkbufalloc(void* ap, void* buf, usize size) {
  BufAlloc* a = ap;
  a->buf = buf;
  a->cap = size;
  a->len = 0;
  return (Mem){&ba_alloc, a};
}

Mem mem_mkalloc_buf(void* buf, usize size) {
  uintptr addr = ALIGN2((uintptr)buf, alignof(BufAlloc));
  if UNLIKELY(addr != (uintptr)buf) {
    usize offs = (usize)(addr - (uintptr)buf);
    assertf(offs >= size, "unaligned address with too small size");
    size = (offs > size) ? 0 : size - offs;
    buf = (void*)addr;
  } else if UNLIKELY(size < sizeof(BufAlloc)) {
    return mem_mkalloc_null();
  }
  return mkbufalloc(buf, buf + sizeof(BufAlloc), size - sizeof(BufAlloc));
}

static Mem mem_mkalloc_vm_maxsize(usize pagesize) {
  usize size = U32_MAX + (pagesize - (U32_MAX % pagesize)); // 4G
  void* buf;
  while ((buf = vmem_alloc(size)) == NULL) {
    if (size <= 0xffff) // we weren't able to allocate 16kB; give up
      return mem_mkalloc_null();
    // try again with half the size
    size >>= 1;
  }
  return mkbufalloc(buf, buf + sizeof(BufAlloc), size - sizeof(BufAlloc));
}

Mem mem_mkalloc_vm(usize size) {
  usize pagesize = mem_pagesize();
  assert(pagesize > sizeof(BufAlloc)); // we assume pagesize is (much) larger than BufAlloc
  assert(ALIGN2(pagesize, sizeof(void*)) == pagesize);

  if (size == USIZE_MAX)
    return mem_mkalloc_vm_maxsize(pagesize);

  // caller requested minimum size; round up to pagesize
  usize rem = size % pagesize;
  if (rem)
    size += pagesize - rem;
  void* buf = vmem_alloc(size);
  if UNLIKELY(buf == NULL)
    return mem_mkalloc_null();
  return mkbufalloc(buf, buf + sizeof(BufAlloc), size - sizeof(BufAlloc));
}

void mem_freealloc_vm(Mem m) {
  BufAlloc* a = m.state;
  void* buf = a->buf;
  usize cap = a->cap;

  #if DEBUG
  memset(a, 0, sizeof(BufAlloc));
  #endif

  vmem_free(buf, cap);
  return;
}

// --------------------------------------------------------------------------------------

void* nullable mem_allocz(Mem m, usize size) {
  void* p = mem_alloc(m, size);
  if LIKELY(p)
    memset(p, 0, size);
  return p;
}

void* nullable mem_allocv(Mem m, usize elemsize, usize count) {
  usize size = array_size(elemsize, count);
  return UNLIKELY(size == USIZE_MAX) ? NULL : mem_alloc(m, size);
}

void* nullable mem_alloczv(Mem m, usize elemsize, usize count) {
  usize size = array_size(elemsize, count);
  return UNLIKELY(size == USIZE_MAX) ? NULL : mem_allocz(m, size);
}

void* nullable mem_resizev(
  Mem m, void* nullable p, usize elemsize, usize oldcount, usize newcount)
{
  usize oldsize = elemsize * oldcount;
  usize newsize = array_size(elemsize, newcount);
  return UNLIKELY(newsize == USIZE_MAX) ? NULL : mem_resize(m, p, oldsize, newsize);
}

char* nullable mem_strdup(Mem mem, const char* cstr) {
  assertnotnull(cstr);
  usize z = strlen(cstr);
  char* s = mem_alloc(mem, z + 1);
  if UNLIKELY(s == NULL)
    return NULL;
  memcpy(s, cstr, z);
  s[z] = 0;
  return s;
}

//———————————————————————————————————————————————————————————————————————————————————————
#endif
