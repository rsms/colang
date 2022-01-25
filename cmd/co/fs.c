#include "coimpl.h"

#ifdef CO_WITH_LIBC
  #include <errno.h>
  #include <dirent.h>
#endif

#ifndef CO_WITH_LIBC

error fs_dir_open(const char* filename, fs_dir* result) { return err_not_supported; }
error fs_dir_open_fd(int fd, fs_dir* result) { return err_not_supported; }
error fs_dir_close(fs_dir d) { return err_invalid; }
error fs_dir_read(fs_dir d, fs_dirent* ent) { return err_invalid; }

#else // CO_WITH_LIBC

error fs_dir_open(const char* filename, fs_dir* result) {
  DIR* d = opendir(filename);
  if (d == NULL)
    return error_from_errno(errno);
  *result = (fs_dir)d;
  return 0;
}

error fs_dir_open_fd(int fd, fs_dir* result) {
  DIR* d = fdopendir(fd);
  if (d == NULL)
    return error_from_errno(errno);
  *result = (fs_dir)d;
  return 0;
}

error fs_dir_close(fs_dir d) {
  assert(d != 0);
  DIR* dirp = (DIR*)d;
  if (dirp == NULL)
    return err_invalid;
  if (closedir(dirp))
    return error_from_errno(errno);
  return 0;
}

error fs_dir_read(fs_dir d, fs_dirent* ent) {
  memset(ent, 0, sizeof(fs_dirent));

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
    ent->type = FS_DIRENT_UNKNOWN;
  #endif

  memcpy(ent->name, e->d_name, MIN(sizeof(ent->name), sizeof(e->d_name)));
  #ifdef _DIRENT_HAVE_D_NAMLEN
    ent->namlen = e->d_namlen;
  #else
    ent->namlen = strlen(e->d_name);
  #endif

  return 1;
}

#endif // CO_WITH_LIBC
