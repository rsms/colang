#pragma once
#include <dirent.h> // DIR, ino_t
ASSUME_NONNULL_BEGIN

#ifndef NAME_MAX
  #ifdef MAXNAMLEN /* BSD */
    #define NAME_MAX MAXNAMLEN
  #else
    #define NAME_MAX 256 /* musl */
  #endif
#endif

bool fs_mkdirs(Mem nullable mem, const char* dir, mode_t mode);

// DirEntry is a portable dirent struct
typedef struct DirEntry {
  ino_t d_ino;            // inode number
  u8    d_type;           // type of file (not supported by all filesystems; 0 if unknown)
  char  d_name[NAME_MAX]; // filename (null terminated)
  u16   d_namlen;         // length of d_name (not including terminating null byte)
} DirEntry;
//
// d_type values:
//   DT_UNKNOWN 0   unknown
//   DT_FIFO    1   named pipe or FIFO
//   DT_CHR     2   character device
//   DT_DIR     4   directory
//   DT_BLK     6   block device
//   DT_REG     8   regular file
//   DT_LNK     10  symbolic link
//   DT_SOCK    12  local-domain socket

// fs_readdir is a portable readdir(). Populates ent on success.
// Returns 1 when a new entry was read and ent was populated.
// Returns 0 when there are no more entries to read.
// Returns <0 on error.
int fs_readdir(DIR* dirp, DirEntry* ent);

ASSUME_NONNULL_END
