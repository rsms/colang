#include <rbase/rbase.h>
#include "schedimpl.h"


#ifdef _WIN32
  #error "TODO win32 VirtualAlloc"
#else
  #define USE_MMAP
  #define USE_MPROTECT
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
#endif


// SIGSTKSZ: system-default stack size with room for signal handling
#ifndef SIGSTKSZ
  #define SIGSTKSZ 32768 // 32 kB
#endif

// MINSIGSTKSZ: minimum stack size for a signal handler
#ifndef MINSIGSTKSZ
  #define MINSIGSTKSZ 131072 // 128 kB
#endif


// Allocates stack memory of approximately reqsize size (aligned to memory page size.)
// Returns the low address (top of stack; not the SB), the actual size in stacksize_out
// and guard size in guardsize_out (stacksize_out - guardsize_out = usable stack space.)
//
// On platforms that support it, stack memory is allocated "lazily" so that only when a page
// is used is it actually committed & allocated in actual memory. On POSIX systems mmap is used
// and on MS Windows VirtualAlloc is used (the latter is currently not implemented.)
//
void* stackalloc(size_t reqsize, size_t* stacksize_out, size_t* guardsize_out) {

  // read system page size (mem_pagesize returns a cached value; no syscall)
  const size_t pagesize = mem_pagesize();

  // additional page to use for stack protection
  const size_t stack_guard_size =
    #ifdef USE_MPROTECT
      pagesize;
    #else
      0;
    #endif

  // one-time init of OS resource limit for stack size
  // Note: no need for atomics since this function is called early during bootstrap.
  static size_t stacksize_limit = 0;
  if (stacksize_limit == 0) {
    // ensure pagesize is 2^pagesize since the rest of the code makes this assumption
    assert(POW2_CEIL(pagesize) == pagesize);
    // load resource limits
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0)
      stacksize_limit = limit.rlim_max;
    if (stacksize_limit == 0) {
      stacksize_limit = 0xFFFFFFFF; // no limit
    } else {
      // ensure that its an even multiple of pagesize
      size_t z = align2(stacksize_limit, pagesize); // ceil
      if (z > stacksize_limit) // through the ceil[ing]
        stacksize_limit = align2(stacksize_limit - pagesize, pagesize); // floor
      // ensure it's at least one page large
      stacksize_limit = MAX(stacksize_limit, pagesize + stack_guard_size);
    }
  }

  // Adjust reqsize to limits and page alignment.
  // If no specific stack size is requested, use a default size of 1MB (usually =256 pages)
  assert(STACK_SIZE_DEFAULT > stack_guard_size + pagesize);
  size_t stacksize = STACK_SIZE_DEFAULT; // default size
  if (reqsize > 0 && reqsize != STACK_SIZE_DEFAULT)
    stacksize = MIN(stacksize_limit, align2(reqsize, pagesize) + stack_guard_size);

  // allocate stack memory
  #ifdef USE_MMAP
    #if R_TARGET_OS_DARWIN && defined(VM_PROT_DEFAULT)
      // vm_map_entry_is_reusable uses VM_PROT_DEFAULT as a condition for page reuse.
      // See http://fxr.watson.org/fxr/source/osfmk/vm/vm_map.c?v=xnu-2050.18.24#L10705
      int prot = VM_PROT_DEFAULT;
    #else
      int prot = PROT_READ|PROT_WRITE;
    #endif

    int flags = MAP_PRIVATE
              | MAP_ANON
      #ifdef MAP_NOCACHE
              | MAP_NOCACHE // don't cache pages for this mapping
      #endif
      #ifdef MAP_NORESERVE
              | MAP_NORESERVE // don't reserve needed swap area
      #endif
      ;

    #if R_TARGET_OS_DARWIN && defined(VM_FLAGS_PURGABLE)
      int fd = VM_FLAGS_PURGABLE; // Create a purgable VM object for that new VM region.
    #else
      int fd = -1;
    #endif

    void* lo = mmap(0, stacksize, prot, flags, fd, 0);
    if (lo == MAP_FAILED)
      return NULL;

    #ifdef USE_MPROTECT
      if (mprotect(lo, stack_guard_size, PROT_NONE) != 0) {
        munmap(lo, stacksize);
        return NULL;
      }
    #endif
  #endif // defined(USE_MMAP)

  // TODO: For Windows, use VirtualAlloc
  //       See https://www.mikemarcin.com/post/coroutine_a_million_stacks/
  //       See https://github.com/MikeMarcin/stackshrink

  *guardsize_out = stack_guard_size;
  *stacksize_out = stacksize;
  return lo;
}


bool stackfree(void* lo, size_t size) {
  #ifdef USE_MMAP
    dlog("%p %zu", lo, size);
    return munmap(lo, size) == 0;
  #else
    #error "TODO"
  #endif
}
