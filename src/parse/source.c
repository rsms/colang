#include "../coimpl.h"
#include "../sha256.h"
#include "source.h"

#ifndef CO_NO_LIBC
  #include <errno.h> // errno
  #include <fcntl.h> // open
  #include <unistd.h> // close
  #include <sys/stat.h> // stat
  #include <sys/mman.h> // mmap, munmap
#endif


static error source_init(Source* src, Mem mem, const char* filename) {
  memset(src, 0, sizeof(Source));
  src->filename = str_make_cstr(mem, filename);
  if (!src->filename)
    return err_nomem;
  assert(src->filename->len > 0);
  return 0;
}

error source_open_file(Source* src, Mem mem, const char* filename) {
  #ifdef CO_NO_LIBC
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
  #endif // CO_NO_LIBC
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
  #ifdef CO_NO_LIBC
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
    #ifdef CO_NO_LIBC
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
    #ifdef CO_NO_LIBC
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
