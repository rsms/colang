#include "rbase.h"
#include <sys/stat.h>

#if __APPLE__
  #ifndef _DIRENT_HAVE_D_NAMLEN
    #define _DIRENT_HAVE_D_NAMLEN
  #endif
  #ifndef _DIRENT_HAVE_D_TYPE
    #define _DIRENT_HAVE_D_TYPE
  #endif
#endif

static bool _mkdirs(Mem mem, const char* dir, bool dirismut, mode_t mode) {
  struct stat st;
  if (stat(dir, &st) != 0) {
    int subcallResult = 0;
    while (mkdir(dir, mode) != 0 && subcallResult == 0) {
      if (errno != ENOENT)
        return false; // error
      // parent dir is missing
      if (!dirismut)
        dir = (const char*)memdup(mem, dir, strlen(dir));
      subcallResult = _mkdirs(mem, path_dir_mut((char*)dir), true, mode);
      if (!dirismut)
        memfree(mem, (void*)dir);
    }
    return true; // dir was created
  }
  if ((st.st_mode & S_IFMT) != S_IFDIR) {
    errno = ENOTDIR;
    return false; // exists but is not a dir
  }
  return true;
}

bool fs_mkdirs(Mem mem, const char* dir, mode_t mode) {
  return _mkdirs(mem, dir, false, mode);
}


// fs_readdir is a portable dirent(). Populates ent on success.
// Returns 1 when a new entry was read and ent was populated.
// Returns 0 when there are no more entries to read.
// Returns <0 on error.
int fs_readdir(DIR* dirp, DirEntry* ent) {
  struct dirent* result = NULL;
  struct dirent e;
  if (readdir_r(dirp, &e, &result) != 0)
    return -1;
  if (!result)
    return 0;
  ent->d_ino = e.d_ino;
  // must copy name since its allocated on the current stack frame.
  // TODO see if there's a simple way to avoid memcpy
  memcpy(ent->d_name, e.d_name, MIN(countof(ent->d_name), countof(e.d_name)));
  ent->d_namlen =
    #ifdef _DIRENT_HAVE_D_NAMLEN
      e.d_namlen;
    #else
      strlen(e.d_name);
    #endif
  ent->d_type =
    #ifdef _DIRENT_HAVE_D_TYPE
      e.d_type;
    #else
      DT_UNKNOWN;
    #endif
  return 1;
}

