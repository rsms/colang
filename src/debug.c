#include "colib.h"

#ifndef CO_NO_LIBC
  #include <stdio.h>
  #include <stdlib.h> // abort
  #include <unistd.h> // STDERR_FILENO
#endif

NORETURN void _panic(const char* file, int line, const char* fun, const char* fmt, ...) {
  #ifndef CO_NO_LIBC
    file = path_cwdrel(file);
    FILE* fp = stderr;
    flockfile(fp);

    // panic: {message} in {function} at {source_location}
    fprintf(fp, "\npanic: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fprintf(fp, " in %s at %s:%d\n", fun, file, line);

    // stack trace
    sys_stacktrace_fwrite(fp, /*offset*/1, /*limit*/30);

    funlockfile(fp);
    fflush(fp);
    fsync(STDERR_FILENO);
  #endif
  abort();
}

#ifdef DEBUG
  const char* debug_tmpsprintf(int buffer, const char* fmt, ...) {
    #ifdef CO_NO_LIBC
      return fmt;
    #else
      static char bufs[6][256];
      char* buf = bufs[MIN(6, MAX(0, buffer))];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof(bufs[0]), fmt, ap);
      va_end(ap);
      return buf;
    #endif
  }
#endif