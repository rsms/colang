#include <stdarg.h>
#include "rbase.h"
#include "mem_dlmalloc.h"

// define a "portable" macro MEM_GETPAGESIZE returning a size_t value
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <tchar.h>
#  define MEM_GETPAGESIZE malloc_getpagesize
#elif defined(malloc_getpagesize)
#  define MEM_GETPAGESIZE malloc_getpagesize
#else
#  include <unistd.h>
#  ifdef _SC_PAGESIZE         /* some SVR4 systems omit an underscore */
#    ifndef _SC_PAGE_SIZE
#      define _SC_PAGE_SIZE _SC_PAGESIZE
#    endif
#  endif
#  ifdef _SC_PAGE_SIZE
#    define MEM_GETPAGESIZE sysconf(_SC_PAGE_SIZE)
#  else
#    if defined(BSD) || defined(DGUX) || defined(HAVE_GETPAGESIZE)
       extern size_t getpagesize();
#      define MEM_GETPAGESIZE getpagesize()
#    else
#      ifndef LACKS_SYS_PARAM_H
#        include <sys/param.h>
#      endif
#      ifdef EXEC_PAGESIZE
#        define MEM_GETPAGESIZE EXEC_PAGESIZE
#      else
#        ifdef NBPG
#          ifndef CLSIZE
#            define MEM_GETPAGESIZE NBPG
#          else
#            define MEM_GETPAGESIZE (NBPG * CLSIZE)
#          endif
#        else
#          ifdef NBPC
#            define MEM_GETPAGESIZE NBPC
#          else
#            ifdef PAGESIZE
#              define MEM_GETPAGESIZE PAGESIZE
#            else /* just guess */
#              define MEM_GETPAGESIZE ((size_t)4096U)
#            endif
#          endif
#        endif
#      endif
#    endif
#  endif
#endif /* _WIN32 */

// value of MEM_GETPAGESIZE
static size_t g_pagesize = 0;

// the default global memory space used by dlmalloc's "simple" functions
// /*__thread*/ Mem _gmem = NULL; // _gmem is defined in mem_dlmalloc.c

static void __attribute__((constructor)) init() {
  g_pagesize = MEM_GETPAGESIZE;
  //_gmem = create_mspace(0, 0);
}

size_t mem_pagesize() {
  return g_pagesize;
}

Mem MemNew(size_t initcap) {
  if (initcap == 0) {
    initcap = g_pagesize;
  }
  return create_mspace(/*capacity*/initcap, /*locked*/0);
}

void MemRecycle(Mem* memptr) {
  // TODO: see if there is a way to make dlmalloc reuse msp
  destroy_mspace(*memptr);
  *memptr = create_mspace(/*capacity*/g_pagesize, /*locked*/0);
}

void MemFree(Mem mem) {
  destroy_mspace(mem);
}

void* memdup2(Mem mem, const void* src, size_t len, size_t extraspace) {
  if (!mem)
    mem = _gmem;
  void* dst = mspace_malloc(mem, len + extraspace);
  memcpy(dst, src, len);
  return dst;
}

char* memstrdupcat(Mem mem, const char* s1, ...) {
  va_list ap;

  size_t len1 = strlen(s1);
  size_t len = len1;
  u32 count = 0;
  va_start(ap, s1);
  while (1) {
    const char* s = va_arg(ap,const char*);
    if (s == NULL || count == 20) { // TODO: warn about limit somehow?
      break;
    }
    len += strlen(s);
  }
  va_end(ap);

  char* newstr = (char*)memalloc(mem, len + 1);
  char* dstptr = newstr;
  memcpy(dstptr, s1, len1);
  dstptr += len1;

  va_start(ap, s1);
  for (u32 i = 0; i < count; i++) {
    const char* s = va_arg(ap,const char*);
    auto len = strlen(s);
    memcpy(dstptr, s, len);
    dstptr += len;
  }
  va_end(ap);

  *dstptr = 0;

  return newstr;
}


// memsprintf is like sprintf but uses memory from mem
char* memsprintf(Mem mem, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  size_t bufsize = (strlen(format) * 2) + 1;
  char* buf = memalloc(mem, bufsize);
  size_t idealsize = (size_t)vsnprintf(buf, bufsize, format, ap);
  if (idealsize >= bufsize) {
    // buf is too small
    buf = mspace_realloc(mem, buf, idealsize + 1);
    idealsize = (size_t)vsnprintf(buf, bufsize, format, ap);
    assert(idealsize < bufsize); // according to libc docs, this should be true
  }
  va_end(ap);
  return buf;
}
