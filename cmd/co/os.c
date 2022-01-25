#include "coimpl.h"

#ifdef CO_WITH_LIBC
  #include <errno.h>
  #include <unistd.h>
#endif

error os_getcwd(char* buf, usize bufcap) {
  #ifdef CO_WITH_LIBC
    if (buf == NULL) // don't allow libc heap allocation semantics
      return err_invalid;
    if (getcwd(buf, bufcap) != buf)
      return error_from_errno(errno);
    return 0;
  #else
    if (bufcap < 2)
      return err_name_too_long;
    buf[0] = '/';
    buf[1] = 0;
    return 0;
  #endif
}
