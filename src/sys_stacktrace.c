#ifdef CO_WITH_LIBC

#include "coimpl.h"
#include "sys.h"
#include "path.h"

#include <execinfo.h> // backtrace*
#include <stdlib.h> // free
#include <stdio.h>

int sys_stacktrace_fwrite(FILE* fp, int offset, int limit) {
  offset++; // always skip the top frame for this function

  if (limit < 1)
    return 0;

  void* buf[128];
  int framecount = backtrace(buf, countof(buf));
  if (framecount < offset)
    return 0;

  char** strs = backtrace_symbols(buf, framecount);
  if (strs == NULL) {
    // Memory allocation failed;
    // fall back to backtrace_symbols_fd, which doesn't respect offset.
    // backtrace_symbols_fd writes entire backtrace to a file descriptor.
    // Make sure anything buffered for fp is written before we write to its fd
    fflush(fp);
    backtrace_symbols_fd(buf, framecount, fileno(fp));
    return 1;
  }

  limit = MIN(offset + limit, framecount);
  for (int i = offset; i < limit; ++i) {
    fwrite(strs[i], strlen(strs[i]), 1, fp);
    fputc('\n', fp);
  }

  free(strs);
  return framecount - offset;
}

#endif // defined(CO_WITH_LIBC)
