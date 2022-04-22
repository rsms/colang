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
  #include <pwd.h> // getpwuid
  #if defined(__MACH__) && defined(__APPLE__)
    #include <mach-o/dyld.h> // _NSGetExecutablePath
  #endif
#endif


static char g_cwdbuf[64];
static char* g_cwd = NULL;


const char* sys_cwd() {
  if (g_cwd)
    return g_cwd;

  char* cwd = g_cwdbuf;

  #ifndef CO_NO_LIBC
    cwd = getcwd(NULL, 0);
    // note: getcwd returns NULL on memory allocation failure
    if (cwd)
      return g_cwd = cwd;
  #endif

  #if defined(CO_NO_LIBC) && defined(WIN32)
    memcpy(cwd, "C:\\", 3);
    cwd[3] = 0;
  #elif defined(CO_NO_LIBC)
    cwd[0] = '/';
    cwd[1] = 0;
  #endif
  g_cwd = cwd;
  return cwd;
}


const char* sys_homedir() {
  #ifndef CO_NO_LIBC
    // try getpwuid
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
      return pw->pw_dir;
    // try HOME in env
    const char* home = getenv("HOME");
    if (home)
      return home;
  #endif
  // fall back to root path
  #if defined(WIN32)
    return "C:\\";
  #else
    return "/";
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
  *result = d;
  return 0;
}

error sys_dir_open_fd(int fd, FSDir* result) {
  DIR* d = fdopendir(fd);
  if (d == NULL)
    return error_from_errno(errno);
  *result = d;
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

bool sys_dir_read(FSDir d, FSDirEnt* ent, char* namebuf, usize namebufcap, error* err_out) {
  DIR* dirp = d;
  if (dirp == NULL) {
    *err_out = err_invalid;
    return false;
  }
  
  struct dirent* e = readdir(dirp);
  if (e == NULL) {
    *err_out = error_from_errno(errno);
    return false;
  }

  ent->ino = e->d_ino;

  #ifdef _DIRENT_HAVE_D_TYPE
    ent->type = e->type;
  #else
    ent->type = FSDirEnt_UNKNOWN;
  #endif

  assert(namebufcap-1 <= U32_MAX);
  #ifdef _DIRENT_HAVE_D_NAMLEN
    ent->namelen = e->d_namlen;
  #else
    usize namelen = strlen(e->d_name);
    safecheck(namelen <= U32_MAX);
    ent->namelen = (u32)namelen;
  #endif

  if (ent->namelen >= namebufcap) {
    *err_out = err_name_too_long;
    return false;
  }

  ent->name = namebuf;
  memcpy(namebuf, e->d_name, (usize)ent->namelen);
  namebuf[ent->namelen] = 0;

  *err_out = 0;
  return true;
}

#endif // !defined(CO_NO_LIBC)
//———————————————————————————————————————————————————————————————————————————————————————
// exepath

static char exepath_buf[128] = {0};
static char* exepath = exepath_buf;


const char* sys_exepath() {
  return exepath;
}


// init_exepath_system_api
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
  static bool init_exepath_system_api() {
    u32 cap = sizeof(exepath_buf);
  try:
    if (_NSGetExecutablePath(exepath, &cap) == 0) {
      exepath = realpath(exepath, NULL); // copies path with libc allocator
      return true;
    }
    // not enough space in exepath
    if (exepath == exepath_buf) {
      exepath = mem_alloc(mem_mkalloc_libc(), cap * 2);
    } else {
      exepath = mem_resize(mem_mkalloc_libc(), exepath, cap, cap * 2);
    }
    if (exepath) {
      cap *= 2;
      if (cap <= 4096)
        goto try;
    }
    return false;
  }
#else
  static bool init_exepath_system_api() {
    return false;
  }
  // Linux: readlink /proc/self/exe
  // FreeBSD: sysctl CTL_KERN KERN_PROC KERN_PROC_PATHNAME -1
  // FreeBSD if it has procfs: readlink /proc/curproc/file (FreeBSD doesn't have procfs by default)
  // NetBSD: readlink /proc/curproc/exe
  // DragonFly BSD: readlink /proc/curproc/file
  // Solaris: getexecname() maybe?
  // Windows: GetModuleFileName() with hModule = NULL
  // From https://stackoverflow.com/a/1024937
#endif


error sys_init_exepath(const char* argv0) {
  // try setting using system API
  if (init_exepath_system_api())
    return 0;

  // try setting from env _
  #ifndef CO_NO_LIBC
  exepath = getenv("_");
  if (exepath && path_isabs(exepath))
    return 0;
  #endif

  // use CWD + argv0
  #ifdef CO_NO_LIBC
    mem_ctx_set_scope(mem_mkalloc_libc());
  #else
    // if exepath_buf is not enough, then just fail
    mem_ctx_set_scope(mem_mkalloc_null());
  #endif
  assert(exepath == exepath_buf);
  Str s = str_make(exepath_buf, sizeof(exepath_buf));
  if (!path_abs(&s, argv0))
    return err_nomem;
  if ((exepath = str_cstr(&s)) == NULL)
    return err_nomem;
  // note: no str_free(&s) -- we keep holding on to it
  return 0;
}
