#include "../coimpl.h"
#include "../sha256.h"
#include "../sys.h"
#include "source.h"

#ifdef CO_WITH_LIBC
  #include <errno.h> // errno
  #include <fcntl.h> // open
  #include <unistd.h> // close
  #include <sys/stat.h> // stat
  #include <sys/mman.h> // mmap, munmap
#endif


void pkg_add_source(Pkg* pkg, Source* src) {
  if (pkg->srclist)
    src->next = pkg->srclist;
  pkg->srclist = src;
}

error pkg_add_file(Pkg* pkg, Mem mem, const char* filename) {
  Source* src = memalloct(mem, Source);
  error err = source_open_file(src, mem, filename);
  if (err) {
    memfree(mem, src);
    return err;
  }
  pkg_add_source(pkg, src);
  return 0;
}

error pkg_add_dir(Pkg* pkg, Mem mem, const char* filename) {
  FSDir d;
  error err = sys_dir_open(filename, &d);
  if (err)
    return err;

  FSDirEnt e;
  error err1;
  while ((err = sys_dir_read(d, &e)) > 0) {
    switch (e.type) {
      case FSDirEnt_REG:
      case FSDirEnt_LNK:
      case FSDirEnt_UNKNOWN:
        if (e.namlen > 3 && e.name[0] != '.' && strcmp(&e.name[e.namlen-2], ".co") == 0) {
          if ((err = pkg_add_file(pkg, mem, e.name)))
            goto end;
        }
        break;
      default:
        break;
    }
  }
end:
  err1 = sys_dir_close(d);
  return err < 0 ? err : err1;
}


static error source_init(Source* src, Mem mem, const char* filename) {
  memset(src, 0, sizeof(Source));
  src->filename = str_make_cstr(mem, filename);
  if (!src->filename)
    return err_nomem;
  assert(src->filename->len > 0);
  return 0;
}

error source_open_file(Source* src, Mem mem, const char* filename) {
  #ifndef CO_WITH_LIBC
    return err_not_supported;
  #else
    error err = source_init(src, mem, filename);
    if (err)
      return err;

    src->fd = open(filename, O_RDONLY);
    if (src->fd < 0)
      return error_from_errno(errno);

    struct stat st;
    if (fstat(src->fd, &st) != 0) {
      int _errno = errno;
      close(src->fd);
      return error_from_errno(_errno);
    }
    src->len = (size_t)st.st_size;

    return 0;
  #endif // CO_WITH_LIBC
}

error source_open_data(Source* src, Mem mem, const char* filename, const char* text, u32 len) {
  error err = source_init(src, mem, filename);
  if (err)
    return err;
  src->fd = -1;
  src->body = (const u8*)text;
  src->len = len;
  return 0;
}

error source_body_open(Source* src) {
  if (src->body != NULL)
    return 0;
  #ifndef CO_WITH_LIBC
    return err_not_supported;
  #else
    src->body = mmap(0, src->len, PROT_READ, MAP_PRIVATE, src->fd, 0);
    if (!src->body)
      return error_from_errno(errno);
    src->ismmap = true;
  #endif
  return 0;
}

error source_body_close(Source* src) {
  if (src->body == NULL)
    return 0;

  if (src->ismmap) {
    #ifndef CO_WITH_LIBC
      return err_invalid;
    #else
      src->ismmap = false;
      if (munmap((void*)src->body, src->len))
        return error_from_errno(errno);
    #endif
  }

  src->body = NULL;
  return 0;
}

error source_close(Source* src) {
  error err = source_body_close(src);
  if (src->fd > -1) {
    #ifndef CO_WITH_LIBC
      return err_invalid;
    #else
      if (close(src->fd) != 0 && err == 0)
        err = error_from_errno(errno);
      src->fd = -1;
    #endif
  }
  str_free(src->filename);
  src->filename = NULL;
  return err;
}

void source_checksum(Source* src) {
  SHA256 state;
  sha256_init(&state, src->sha256);
  usize remaining = (usize)src->len;
  const u8* inptr = src->body;
  while (remaining > 0) {
    usize z = MIN(SHA256_CHUNK_SIZE, remaining);
    sha256_write(&state, inptr, z);
    inptr += z;
    remaining -= z;
  }
  sha256_close(&state);
}
