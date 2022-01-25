#include "coimpl.h"

error error_from_errno(int errno) {
  if (errno == 0)
    return 0;
  return err_invalid; // TODO FIXME
}

const char* error_str(error e) {
  switch ((enum error)e) {
  case err_ok:            return "(no error)";
  case err_invalid:       return "invalid data or argument";
  case err_sys_op:        return "invalid syscall op or syscall op data";
  case err_badfd:         return "invalid file descriptor";
  case err_bad_name:      return "invalid or misformed name";
  case err_not_found:     return "resource not found";
  case err_name_too_long: return "name too long";
  case err_canceled:      return "operation canceled";
  case err_not_supported: return "not supported";
  case err_exists:        return "already exists";
  case err_end:           return "end of resource";
  case err_access:        return "permission denied";
  case err_nomem:         return "cannot allocate memory";
  case err_mfault:        return "bad memory address";
  case err_overflow:      return "value too large for defined data type";
  }
  return "(unknown error)";
}
