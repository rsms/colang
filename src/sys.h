// sys -- host system functions like filesystem access
#pragma once
ASSUME_NONNULL_BEGIN

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
} END_TYPED_ENUM(FSDirEntType);

// sys_getcwd populates buf with the current working directory including a nul terminator.
// If bufsize is not enough, an error is returned.
error sys_getcwd(char* buf, usize bufsize);

error sys_dir_open(const char* filename, FSDir* result);
error sys_dir_open_fd(int fd, FSDir* result);
error sys_dir_read(FSDir, FSDirEnt* result); // returns 1 on success, 0 on EOF
error sys_dir_close(FSDir);

ASSUME_NONNULL_END
