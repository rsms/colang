#include "coimpl.h"
#include "sys.h"
#include "mem.h"
#include "path.h"

static char pathst[1024];
static const char* exepath = NULL;


error sys_set_exepath(const char* path) {
  usize cwdlen = 0;

  if (!path_isabs(path)) {
    if (sys_getcwd(pathst, sizeof(pathst)) != 0)
      return err_name_too_long;
    cwdlen = strlen(pathst);
    pathst[cwdlen++] = PATH_SEPARATOR;
  }

  usize pathlen = strlen(path);
  if (pathlen + cwdlen > sizeof(pathst)-1)
    return err_name_too_long;

  memcpy(&pathst[cwdlen], path, pathlen);
  pathst[cwdlen + pathlen] = 0;
  exepath = pathst;
  return 0;
}


// --------------------------------------------------------------------------------------
#if defined(CO_WITH_LIBC) && defined(__MACH__) && defined(__APPLE__)
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
  char* path = pathst;
  u32 len = sizeof(pathst);

  while (1) {
    if (_NSGetExecutablePath(path, &len) == 0) {
      path = realpath(path, NULL);
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
    len *= 2;
    if (path == pathst) {
      path = memalloc(mem_libc_allocator(), len);
    } else {
      path = memrealloc(mem_libc_allocator(), path, len);
    }
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
