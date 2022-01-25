#include "coimpl.h"

#ifdef CO_WITH_LIBC
  #include <stdio.h>
  #include <stdlib.h> // abort
  #include <unistd.h> // STDERR_FILENO
#endif

NORETURN void _panic(const char* file, int line, const char* fun, const char* fmt, ...) {
  #ifndef CO_WITH_LIBC
    UNREACHABLE;
  #else
    file = path_cwdrel(file);
    FILE* fp = stderr;
    flockfile(fp);

    // panic: {message} in {function} at {source_location}
    fprintf(stderr, "\npanic: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, " in %s at %s:%d\n", fun, file, line);

    // TODO: stack trace
    // const int offsetFrames = 1;
    // int limit = 0;
    // int limit_src = 0;
    // panic_get_stacktrace_limits(&limit, &limit_src);
    // os_stacktrace_fwrite(stderr, offsetFrames, limit, limit_src);

    funlockfile(fp);
    fflush(fp);
    fsync(STDERR_FILENO);

    abort();
  #endif
}
