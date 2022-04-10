// host system functions, like filesystem access
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef uintptr         FSDir;        // file directory handle
typedef struct FSDirEnt FSDirEnt;     // directory entry
typedef u8              FSDirEntType; // type of directory entry

struct FSDirEnt {
  isize        ino;       // inode number
  FSDirEntType type;      // type of file (not supported by all filesystems; 0 if unknown)
  char         name[256]; // filename (null terminated)
  u16          namlen;    // length of d_name (not including terminating null byte)
};

enum FSDirEntType {
  FSDirEnt_UNKNOWN = 0,  // unknown
  FSDirEnt_FIFO    = 1,  // named pipe or FIFO
  FSDirEnt_CHR     = 2,  // character device
  FSDirEnt_DIR     = 4,  // directory
  FSDirEnt_BLK     = 6,  // block device
  FSDirEnt_REG     = 8,  // regular file
  FSDirEnt_LNK     = 10, // symbolic link
  FSDirEnt_SOCK    = 12, // local-domain socket
  FSDirEnt_WHT     = 14, // BSD whiteout
} END_ENUM(FSDirEntType);

// sys_cwd returns the current working directory
const char* sys_cwd();

// sys_homedir returns the current user's home directory
const char* sys_homedir();

error sys_dir_open(const char* filename, FSDir* result);
error sys_dir_open_fd(int fd, FSDir* result);
error sys_dir_read(FSDir, FSDirEnt* result); // returns 1 on success, 0 on EOF
error sys_dir_close(FSDir);

// sys_exepath returns the absolute path of the current executable.
// Returns an empty string if the path could not be computed.
const char* sys_exepath();

// sys_init_exepath initializes exepath
error sys_init_exepath(const char* argv0);


#ifndef CO_NO_LIBC
  // sys_stacktrace_fwrite writes a stacktrace (aka backtrace) to fp.
  // offset: number of stack frames to skip (starting at the top.)
  // limit: max number of stack frames to print.
  // Returns the approximate number of lines written to fp.
  int sys_stacktrace_fwrite(FILE* fp, int offset, int limit);
#endif

END_INTERFACE
