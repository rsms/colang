#include "rbase.h"
#include <execinfo.h>


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
  } else {
    fputc('\n', fp);
  }

  funlockfile(fp);
  fflush(fp);
}

_Noreturn void _panic(const char* filename, int lineno, const char* fname, const char* fmt, ...) {
  FILE* fp = stderr;
  flockfile(fp);

  fprintf(stderr, "\npanic: ");

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fprintf(stderr, " in %s at %s:%d\n", fname, filename, lineno);

  stacktrace_fwrite(stderr, /* offsetFrames = */ 1);

  funlockfile(fp);
  fflush(fp);

  // exit(2);
  abort();
  // _Exit(2);
}
