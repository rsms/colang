#pragma once
ASSUME_NONNULL_BEGIN

// os_mempagesize returns the memory page size
size_t os_mempagesize();

// sys_ncpu returns the number of hardware threads.
// Returns 0 when the number of CPUs could not be determined.
u32 sys_ncpu();

// fmthex writes len*2 bytes to out, encoding indata in hexadecimal form
void fmthex(char* out, const u8* indata, int len);

// fmtduration appends human-readable time duration to buf
int fmtduration(char* buf, int bufsize, u64 timeduration);

// get the current user's home directory. Returns "" on failure.
const char* user_home_dir();

// int popcount<T>(T v)
#define popcount(x) _Generic((x), \
  int:                 __builtin_popcount, \
  unsigned int:        __builtin_popcount, \
  long:                __builtin_popcountl, \
  unsigned long:       __builtin_popcountl, \
  long long:           __builtin_popcountll, \
  unsigned long long:  __builtin_popcountll \
)(x)

// sha1
typedef struct {
  u32 state[5];
  u32 count[2];
  u8  buffer[64];
} SHA1Ctx;
void sha1_init(SHA1Ctx* ctx);
void sha1_update(SHA1Ctx* ctx, const u8* data, uint32_t len);
void sha1_final(u8 digest[20], SHA1Ctx* ctx);
void sha1_transform(uint32_t state[5], const u8 buffer[64]);
void sha1_fmt(char dst[40], const u8 digest[20]); // appends hex(digest) to dst

// i/o reader
typedef struct {
  int fd;
  u8  buf[512];
  int err;
} Reader;
int ReaderOpen(Reader* r, const char* filename);
int ReaderClose(Reader* r); // only call on r if you called ReaderOpen earlier
int ReaderRead(Reader* r); // returns bytes read. 0 on EOF.

// i/o writer
typedef struct {
  int fd;
  u8  buf[512];
  int len;
} Writer;
void WriterInit(Writer* w, int fd);
int WriterWrite(Writer* w, const void* data, int len); // 0 on success or errno
int WriterClose(Writer* w);
int WriterFlush(Writer* w);

ASSUME_NONNULL_END
