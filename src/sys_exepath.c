#include "coimpl.h"
#include "sys.h"
#include "mem.c"
#include "path.h"

static char exepath_buf[1024];
static const char* exepath = NULL;


error sys_set_exepath(const char* path) {
  usize cwdlen = 0;

  if (!path_isabs(path)) {
    if (sys_getcwd(exepath_buf, sizeof(exepath_buf)) != 0)
      return err_name_too_long;
    cwdlen = strlen(exepath_buf);
    exepath_buf[cwdlen++] = PATH_SEPARATOR;
  }

  usize pathlen = strlen(path);
  if (pathlen + cwdlen > sizeof(exepath_buf)-1)
    return err_name_too_long;

  memcpy(&exepath_buf[cwdlen], path, pathlen);
  exepath_buf[cwdlen + pathlen] = 0;
  exepath = exepath_buf;
  return 0;
}


// --------------------------------------------------------------------------------------
#if !defined(CO_NO_LIBC) && defined(__MACH__) && defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h> // realpath
#include <limits.h> // PATH_MAX

// [from man 3 dyld]
// _NSGetExecutablePath() copies the path of the main executable into the buffer buf.
// The bufsize parameter should initially be the size of the buffer.
// This function returns 0 if the path was successfully copied, and *bufsize is left unchanged.
// It returns -1 if the buffer is not large enough, and *bufsize is set to the size required.
// Note that _NSGetExecutablePath() will return "a path" to the executable not a "real path"
// to the executable.
// That is, the path may be a symbolic link and not the real file. With deep directories the
// total bufsize needed could be more than MAXPATHLEN.

const char* sys_exepath() {
  if (exepath)
    return exepath;
  char* path = exepath_buf;
  u32 len = sizeof(exepath_buf);

  while (1) {
    if (_NSGetExecutablePath(path, &len) == 0) {
      path = realpath(path, NULL); // copies path with libc allocator
      break;
    }
    if (len >= PATH_MAX) {
      // Don't keep growing the buffer at this point.
      // _NSGetExecutablePath may be failing for other reasons.
      path[0] = '/';
      path[1] = 0;
      break;
    }
    // not enough space in path
    if (path == exepath_buf) {
      path = mem_alloc(mem_mkalloc_libc(), len*2);
    } else {
      u32 newlen;
      if (check_mul_overflow(len, 2, &newlen))
        break;
      path = mem_resize(mem_mkalloc_libc(), path, len, (usize)newlen);
    }
    len *= 2;
  }

  if (path == NULL || strlen(path) == 0)
    path = "/";

  if (exepath == NULL)
    exepath = path;
  return exepath;
}

// --------------------------------------------------------------------------------------
#else
  // not implemented for current target
  const char* sys_exepath() {
    if (exepath)
      return exepath;
    #ifdef WIN32
      return "C:\\";
    #else
      return "/";
    #endif
  }

  // Linux: readlink /proc/self/exe
  // FreeBSD: sysctl CTL_KERN KERN_PROC KERN_PROC_PATHNAME -1
  // FreeBSD if it has procfs: readlink /proc/curproc/file (FreeBSD doesn't have procfs by default)
  // NetBSD: readlink /proc/curproc/exe
  // DragonFly BSD: readlink /proc/curproc/file
  // Solaris: getexecname()
  // Windows: GetModuleFileName() with hModule = NULL
  // From https://stackoverflow.com/a/1024937
#endif
