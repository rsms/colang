#include "coimpl.h"
#include "sys.h"

#ifndef CO_NO_LIBC
  #include <errno.h>
  #include <dirent.h>
  #include <unistd.h>
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
