#include "rbase.h"
#include <pwd.h> // getpwuid
#include <execinfo.h>

ASSUME_NONNULL_BEGIN


bool stacktrace_fwrite(FILE* nonull fp, int offset_frames) {
  void* buf[64];
  int framecount = backtrace(buf, countof(buf));
  if (framecount > 1) {
    if (offset_frames > -1) {
      char** strs = backtrace_symbols(buf, framecount);
      if (strs != NULL) {
        for (int i = offset_frames + 1; i < framecount; ++i) {
          fwrite(strs[i], strlen(strs[i]), 1, fp);
          fwrite("\n", 1, 1, fp);
        }
        free(strs);
        return true; // success
      }
      // Memory allocation failed;
      // fall back to backtrace_symbols_fd, which doesn't respect offset_frames.
    }
    // backtrace_symbols_fd writes entire backtrace to a file descriptor.
    // Make sure anything buffered for fp is written before we write to its fd
    fflush(fp);
    backtrace_symbols_fd(buf, framecount, fileno(fp));
    return true;
  }
  return false; // failure
}


// // writeline calls write(fd, ptr, len); prints a newline unless ptr[len-1]=='\n'
// // Does not write anything if len==0.
// void writeline(int fd, const void* ptr, size_t len) {
//   if (len > 0) {
//     write(fd, ptr, len);
//     if (((char*)ptr)[len-1] != '\n')
//       write(fd, "\n", 1);
//   }
// }

void _errlog(const char* fmt, ...) {
  FILE* fp = stderr;
  flockfile(fp);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  int err = errno;
  if (err != 0) {
    errno = 0;
    char buf[256];
    if (strerror_r(err, buf, countof(buf)) == 0)
      fprintf(fp, " ([%d] %s)\n", err, buf);
  }
  funlockfile(fp);
}

noreturn void _panic(const char* msg, const char* filename, int lineno) {
  fprintf(stderr, "\npanic: %s (%s:%d)\n", msg, filename, lineno);
  stacktrace_fwrite(stderr, /* offsetFrames = */ 1);
  fflush(stderr);
  exit(2);
}

static size_t _mempagesize = 0;

size_t mempagesize() {
  if (_mempagesize == 0) {
    _mempagesize = (size_t)sysconf(_SC_PAGESIZE);
    if (_mempagesize <= 0)
      _mempagesize = 1024 * 4;
  }
  return _mempagesize;
}

void fmthex(char* out, const u8* indata, int len) {
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < len; i++) {
    out[0] = hex[(indata[i]>>4) & 0xF];
    out[1] = hex[ indata[i]     & 0xF];
    out += 2;
    // sprintf((char*)dst+2*i, "%02x", digest[i]);
  }
}

const char* user_home_dir() {
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_dir)
    return pw->pw_dir;
  auto home = getenv("HOME");
  if (home)
    return home;
  return "";
}


ASSUME_NONNULL_END
