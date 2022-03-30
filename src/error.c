#include "colib.h"

#ifndef CO_NO_LIBC
  #include <errno.h>
#endif

const char* error_str(error e) {
  switch ((enum error)e) {
  #define _(NAME, DESCR) case err_##NAME: return DESCR;
  CO_FOREACH_ERROR(_)
  #undef _
  }
  return "unspecified error";
}

error error_from_errno(int e) {
  #ifdef CO_NO_LIBC
    return e == 0 ? 0 : err_invalid;
  #else
    switch (e) {
    case 0: return 0;

    case EACCES: // Permission denied
    case EPERM: // Operation not permitted
      return err_access;

    case ENOENT: // No such file or directory
    case ESRCH:  // No such process
    case ENXIO:  // No such device or address
    case ENODEV: // No such device
    case ENOPROTOOPT: // Protocol not available
    case EADDRNOTAVAIL: // Address not available
      return err_not_found;

    case ENOTSUP: // Not supported
    case EOPNOTSUPP: // Operation not supported
    case EPROTONOSUPPORT: // Protocol not supported
    case ESOCKTNOSUPPORT: // Socket type not supported
    case EPFNOSUPPORT: // Protocol family not supported
    case EAFNOSUPPORT: // Address family not supported by protocol
      return err_not_supported;

    case EOVERFLOW: // Value too large for data type
    case ERANGE: // Result not representable
      return err_overflow;

    case EBADF: return err_badfd; // File descriptor in bad state
    case ENOMEM: return err_nomem; // Out of memory
    case ENOSPC: return err_nospace; // No space left on device
    case EFAULT: return err_mfault; // Bad address
    case EEXIST: return err_exists; // File exists
    case ENAMETOOLONG: return err_name_too_long; // Filename too long
    case ECANCELED: return err_canceled; // Operation canceled
    case EINVAL: return err_invalid; // Invalid argument

    // TODO: rest of the error codes (listed below)

    default: return err_invalid;
  }
  #endif
}


/* errno strerror
0                "No error information"

EILSEQ           "Illegal byte sequence"
EDOM             "Domain error"
ERANGE           "Result not representable"

ENOTTY           "Not a tty"
EACCES           "Permission denied"
EPERM            "Operation not permitted"
ENOENT           "No such file or directory"
ESRCH            "No such process"
EEXIST           "File exists"

EOVERFLOW        "Value too large for data type"
ENOSPC           "No space left on device"
ENOMEM           "Out of memory"

EBUSY            "Resource busy"
EINTR            "Interrupted system call"
EAGAIN           "Resource temporarily unavailable"
ESPIPE           "Invalid seek"

EXDEV            "Cross-device link"
EROFS            "Read-only file system"
ENOTEMPTY        "Directory not empty"

ECONNRESET       "Connection reset by peer"
ETIMEDOUT        "Operation timed out"
ECONNREFUSED     "Connection refused"
EHOSTDOWN        "Host is down"
EHOSTUNREACH     "Host is unreachable"
EADDRINUSE       "Address in use"

EPIPE            "Broken pipe"
EIO              "I/O error"
ENXIO            "No such device or address"
ENOTBLK          "Block device required"
ENODEV           "No such device"
ENOTDIR          "Not a directory"
EISDIR           "Is a directory"
ETXTBSY          "Text file busy"
ENOEXEC          "Exec format error"

EINVAL           "Invalid argument"

E2BIG            "Argument list too long"
ELOOP            "Symbolic link loop"
ENAMETOOLONG     "Filename too long"
ENFILE           "Too many open files in system"
EMFILE           "No file descriptors available"
EBADF            "Bad file descriptor"
ECHILD           "No child process"
EFAULT           "Bad address"
EFBIG            "File too large"
EMLINK           "Too many links"
ENOLCK           "No locks available"

EDEADLK          "Resource deadlock would occur"
ENOTRECOVERABLE  "State not recoverable"
EOWNERDEAD       "Previous owner died"
ECANCELED        "Operation canceled"
ENOSYS           "Function not implemented"
ENOMSG           "No message of desired type"
EIDRM            "Identifier removed"
ENOSTR           "Device not a stream"
ENODATA          "No data available"
ETIME            "Device timeout"
ENOSR            "Out of streams resources"
ENOLINK          "Link has been severed"
EPROTO           "Protocol error"
EBADMSG          "Bad message"
EBADFD           "File descriptor in bad state"
ENOTSOCK         "Not a socket"
EDESTADDRREQ     "Destination address required"
EMSGSIZE         "Message too large"
EPROTOTYPE       "Protocol wrong type for socket"
ENOPROTOOPT      "Protocol not available"
EPROTONOSUPPORT  "Protocol not supported"
ESOCKTNOSUPPORT  "Socket type not supported"
ENOTSUP          "Not supported"
EPFNOSUPPORT     "Protocol family not supported"
EAFNOSUPPORT     "Address family not supported by protocol"
EADDRNOTAVAIL    "Address not available"
ENETDOWN         "Network is down"
ENETUNREACH      "Network unreachable"
ENETRESET        "Connection reset by network"
ECONNABORTED     "Connection aborted"
ENOBUFS          "No buffer space available"
EISCONN          "Socket is connected"
ENOTCONN         "Socket not connected"
ESHUTDOWN        "Cannot send after socket shutdown"
EALREADY         "Operation already in progress"
EINPROGRESS      "Operation in progress"
ESTALE           "Stale file handle"
EREMOTEIO        "Remote I/O error"
EDQUOT           "Quota exceeded"
ENOMEDIUM        "No medium found"
EMEDIUMTYPE      "Wrong medium type"
EMULTIHOP        "Multihop attempted"
*/
