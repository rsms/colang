#include "coimpl.h"

#ifndef CO_NO_LIBC
  #include <errno.h>
#endif

const char* error_str(error e) {
  switch ((enum error)e) {
  #define _(NAME, DESCR) case err_##NAME: return DESCR;
  CO_FOREACH_ERROR(_)
  #undef _
  }
  return "(unknown error)";
}

error error_from_errno(int e) {
  #ifdef CO_NO_LIBC
    return e == 0 ? 0 : err_invalid;
  #else
    switch (e) {
    case 0: return 0;

    case EACCES:
    case EPERM: // Operation not permitted
    case EAUTH: // Authentication error
      return err_access;

    case ENOENT:
    case ESRCH:
    case ENODEV:
      return err_not_found;

    case ENOTSUP:
    case EOPNOTSUPP:
    case EPROTONOSUPPORT:
    case ESOCKTNOSUPPORT:
    case EPFNOSUPPORT:
    case EAFNOSUPPORT:
      return err_not_supported;

    case EOVERFLOW:
    case ERANGE:
      return err_overflow;

    case EBADF: return err_badfd;
    case ENOMEM: return err_nomem;
    case EFAULT: return err_mfault;
    case EEXIST: return err_exists;
    case ENAMETOOLONG: return err_name_too_long;
    case ECANCELED: return err_canceled;
    // case EINVAL: return err_invalid; // default case

    // TODO:
    // case ESRCH: return err_;
    // case EINTR: return err_;
    // case EIO: return err_;
    // case ENXIO: return err_;
    // case E2BIG: return err_;
    // case ENOEXEC: return err_;
    // case ECHILD: return err_;
    // case EAGAIN: case EWOULDBLOCK: return err_;
    // case ENOTBLK: return err_;
    // case EBUSY: return err_;
    // case EXDEV: return err_;
    // case ENOTDIR: return err_;
    // case EISDIR: return err_;
    // case ENFILE: return err_;
    // case EMFILE: return err_;
    // case ENOTTY: return err_;
    // case ETXTBSY: return err_;
    // case EFBIG: return err_;
    // case ENOSPC: return err_;
    // case ESPIPE: return err_;
    // case EROFS: return err_;
    // case EMLINK: return err_;
    // case EPIPE: return err_;
    // case EDOM: return err_;
    // case EDEADLK: case EDEADLOCK: return err_;
    // case ENOLCK: return err_;
    // case ENOSYS: return err_;
    // case ENOTEMPTY: return err_;
    // case ELOOP: return err_;
    // case ENOMSG: return err_;
    // case EIDRM: return err_;
    // case ECHRNG: return err_;
    // case EL2NSYNC: return err_;
    // case EL3HLT: return err_;
    // case EL3RST: return err_;
    // case ELNRNG: return err_;
    // case EUNATCH: return err_;
    // case ENOCSI: return err_;
    // case EL2HLT: return err_;
    // case EBADE: return err_;
    // case EBADR: return err_;
    // case EXFULL: return err_;
    // case ENOANO: return err_;
    // case EBADRQC: return err_;
    // case EBADSLT: return err_;
    // case EBFONT: return err_;
    // case ENOSTR: return err_;
    // case ENODATA: return err_;
    // case ETIME: return err_;
    // case ENOSR: return err_;
    // case ENONET: return err_;
    // case ENOPKG: return err_;
    // case EREMOTE: return err_;
    // case ENOLINK: return err_;
    // case EADV: return err_;
    // case ESRMNT: return err_;
    // case ECOMM: return err_;
    // case EPROTO: return err_;
    // case EMULTIHOP: return err_;
    // case EDOTDOT: return err_;
    // case EBADMSG: return err_;
    // case ENOTUNIQ: return err_;
    // case EBADFD: return err_;
    // case EREMCHG: return err_;
    // case ELIBACC: return err_;
    // case ELIBBAD: return err_;
    // case ELIBSCN: return err_;
    // case ELIBMAX: return err_;
    // case ELIBEXEC: return err_;
    // case EILSEQ: return err_;
    // case ERESTART: return err_;
    // case ESTRPIPE: return err_;
    // case EUSERS: return err_;
    // case ENOTSOCK: return err_;
    // case EDESTADDRREQ: return err_;
    // case EMSGSIZE: return err_;
    // case EPROTOTYPE: return err_;
    // case ENOPROTOOPT: return err_;
    // case EADDRINUSE: return err_;
    // case EADDRNOTAVAIL: return err_;
    // case ENETDOWN: return err_;
    // case ENETUNREACH: return err_;
    // case ENETRESET: return err_;
    // case ECONNABORTED: return err_;
    // case ECONNRESET: return err_;
    // case ENOBUFS: return err_;
    // case EISCONN: return err_;
    // case ENOTCONN: return err_;
    // case ESHUTDOWN: return err_;
    // case ETOOMANYREFS: return err_;
    // case ETIMEDOUT: return err_;
    // case ECONNREFUSED: return err_;
    // case EHOSTDOWN: return err_;
    // case EHOSTUNREACH: return err_;
    // case EALREADY: return err_;
    // case EINPROGRESS: return err_;
    // case ESTALE: return err_;
    // case EUCLEAN: return err_;
    // case ENOTNAM: return err_;
    // case ENAVAIL: return err_;
    // case EISNAM: return err_;
    // case EREMOTEIO: return err_;
    // case EDQUOT: return err_;
    // case ENOMEDIUM: return err_;
    // case EMEDIUMTYPE: return err_;
    // case ENOKEY: return err_;
    // case EKEYEXPIRED: return err_;
    // case EKEYREVOKED: return err_;
    // case EKEYREJECTED: return err_;
    // case EOWNERDEAD: return err_;
    // case ENOTRECOVERABLE: return err_;
    // case ERFKILL: return err_;
    // case EHWPOISON: return err_;
    default: return err_invalid;
  }
  #endif
}
