// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"

#ifndef CO_NO_LIBC
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
#endif // CO_NO_LIBC
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
    assertf((uintptr)p == ALIGN2((uintptr)p, sizeof(void*)), "bad address %p", p);
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
// mem_ctx

_Thread_local Mem _mem_ctx = {&_mem_null_alloc, NULL};

void _mem_ctx_scope_cleanup(Mem* prev) {
  // dlog("_mem_ctx_scope_cleanup prev->a=%p", prev->a);
  if (prev->a)
    mem_ctx_set(*prev);
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
