// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"

#ifndef CO_NO_LIBC
  #include <errno.h>
  #include <dirent.h>
  #include <unistd.h>
  #include <execinfo.h> // backtrace*
  #include <stdlib.h> // free, realpath
  #include <limits.h> // PATH_MAX
  #if defined(__MACH__) && defined(__APPLE__)
    #include <mach-o/dyld.h> // _NSGetExecutablePath
  #endif
#endif


error sys_getcwd(char* buf, usize bufcap) {
  #ifdef CO_NO_LIBC
    if (bufcap < 2)
      return err_name_too_long;
    buf[0] = '/';
    buf[1] = 0;
    return 0;
  #else
    if (buf == NULL) // don't allow libc heap allocation semantics
      return err_invalid;
    if (getcwd(buf, bufcap) != buf)
      return error_from_errno(errno);
    return 0;
  #endif
}


int sys_stacktrace_fwrite(FILE* fp, int offset, int limit) {
  offset++; // always skip the top frame for this function

  if (limit < 1)
    return 0;

  void* buf[128];
  int framecount = backtrace(buf, countof(buf));
  if (framecount < offset)
    return 0;

  char** strs = backtrace_symbols(buf, framecount);
  if (strs == NULL) {
    // Memory allocation failed;
    // fall back to backtrace_symbols_fd, which doesn't respect offset.
    // backtrace_symbols_fd writes entire backtrace to a file descriptor.
    // Make sure anything buffered for fp is written before we write to its fd
    fflush(fp);
    backtrace_symbols_fd(buf, framecount, fileno(fp));
    return 1;
  }

  limit = MIN(offset + limit, framecount);
  for (int i = offset; i < limit; ++i) {
    fwrite(strs[i], strlen(strs[i]), 1, fp);
    fputc('\n', fp);
  }

  free(strs);
  return framecount - offset;
}


//———————————————————————————————————————————————————————————————————————————————————————
// dirs
#ifdef CO_NO_LIBC
error sys_dir_open(const char* filename, FSDir* result) { return err_not_supported; }
error sys_dir_open_fd(int fd, FSDir* result) { return err_not_supported; }
error sys_dir_close(FSDir d) { return err_invalid; }
error sys_dir_read(FSDir d, FSDirEnt* ent) { return err_invalid; }
#else // defined(CO_NO_LIBC)

error sys_dir_open(const char* filename, FSDir* result) {
  DIR* d = opendir(filename);
  if (d == NULL)
    return error_from_errno(errno);
  *result = (FSDir)d;
  return 0;
}

error sys_dir_open_fd(int fd, FSDir* result) {
  DIR* d = fdopendir(fd);
  if (d == NULL)
    return error_from_errno(errno);
  *result = (FSDir)d;
  return 0;
}

error sys_dir_close(FSDir d) {
  assert(d != 0);
  DIR* dirp = (DIR*)d;
  if (dirp == NULL)
    return err_invalid;
  if (closedir(dirp))
    return error_from_errno(errno);
  return 0;
}

error sys_dir_read(FSDir d, FSDirEnt* ent) {
  memset(ent, 0, sizeof(FSDirEnt));

  DIR* dirp = (DIR*)d;
  if (dirp == NULL)
    return err_invalid;
  
  struct dirent* e = readdir(dirp);
  if (e == NULL)
    return error_from_errno(errno); // 0 if EOF

  ent->ino = e->d_ino;

  #ifdef _DIRENT_HAVE_D_TYPE
    ent->type = e->type;
  #else
    ent->type = FSDirEnt_UNKNOWN;
  #endif

  memcpy(ent->name, e->d_name, MIN(sizeof(ent->name), sizeof(e->d_name)));
  #ifdef _DIRENT_HAVE_D_NAMLEN
    ent->namlen = e->d_namlen;
  #else
    ent->namlen = strlen(e->d_name);
  #endif

  return 1;
}

#endif // !defined(CO_NO_LIBC)
//———————————————————————————————————————————————————————————————————————————————————————
// exepath

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

#if !defined(CO_NO_LIBC) && defined(__MACH__) && defined(__APPLE__)
  // [from man 3 dyld]
  // _NSGetExecutablePath() copies the path of the main executable into the buffer buf.
  // The bufsize parameter should initially be the size of the buffer.
  // This function returns 0 if the path was successfully copied, and *bufsize is left
  // unchanged. It returns -1 if the buffer is not large enough, and *bufsize is set to
  // the size required.
  // Note that _NSGetExecutablePath() will return "a path" to the executable not a
  // "real path" to the executable.
  // That is, the path may be a symbolic link and not the real file.
  // With deep directories the total bufsize needed could be more than MAXPATHLEN.

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
        if (check_mul_overflow(len, 2u, &newlen))
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

