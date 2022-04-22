// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

#ifndef CO_NO_LIBC
  #include <errno.h> // errno
  #include <fcntl.h> // open
  #include <unistd.h> // close
  #include <sys/stat.h> // stat
  #include <sys/mman.h> // mmap, munmap
#endif


static error source_init(Source* src, const char* filename) {
  memset(src, 0, sizeof(Source));
  src->filename = memstrdup(filename);
  if UNLIKELY(!src->filename)
    return err_nomem;
  return 0;
}

error source_close(Source* src) {
  error err = source_body_close(src);
  if (src->fd >= 0) {
    #ifdef CO_NO_LIBC
      return err_invalid;
    #else
      if (close(src->fd) != 0 && err == 0)
        err = error_from_errno(errno);
      src->fd = -1;
    #endif
  }
  memfree(src->filename, strlen(src->filename) + 1);
  array_free(&src->lineoffs);
  return err;
}

error source_open_data(Source* src, const char* filename, const u8* body, u32 len){
  error err = source_init(src, filename);
  if (err)
    return err;
  src->fd = -1;
  src->body = body;
  src->len = len;
  return 0;
}

error source_open_filex(Source* src, const char* filename, int fd, usize len) {
  #ifdef CO_NO_LIBC
    return err_not_supported;
  #else
    if (fd < 0)
      return err_badfd;
    error err = source_init(src, filename);
    if (err) {
      close(fd);
      return err;
    }
    src->fd = fd;
    src->len = len;
    return 0;
  #endif // CO_NO_LIBC
}

error source_open_file(Source* src, const char* filename) {
  #ifdef CO_NO_LIBC
    return err_not_supported;
  #else
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
      return error_from_errno(errno);

    struct stat st;
    if (fstat(fd, &st) != 0) {
      error err = error_from_errno(errno);
      close(fd);
      return err;
    }

    return source_open_filex(src, filename, fd, (usize)st.st_size);
  #endif // CO_NO_LIBC
}

error source_body_open(Source* src) {
  if (src->body)
    return 0;
  #ifdef CO_NO_LIBC
    return err_not_supported;
  #else
    src->body = mmap(0, src->len, PROT_READ, MAP_PRIVATE, src->fd, 0);
    if (!src->body)
      return error_from_errno(errno);
    src->ismmap = true;
  #endif
  assertnotnull(src->body);
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

error source_checksum(Source* src) {
  error err = source_body_open(src);
  if (err)
    return err;

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
  return 0;
}


error source_compute_lineoffs(Source* src) {
  if (src->lineoffs.len > 0) // already computed
    return 0;

  if (!src->body) {
    error err = source_body_open(src);
    if (err)
      return err;
  }

  // estimate total number of lines.
  // From analysis of the Co codebase, 30 columns is average
  if (!array_reserve(&src->lineoffs, MAX(8, src->len / 30)))
    return err_nomem;

  // line 0 is invalid (Pos 0 is invalid)
  array_push(&src->lineoffs, 0);

  for (u32 i = 0; i < src->len;) {
    if (src->body[i++] == '\n')
      array_push(&src->lineoffs, i);
  }

  return 0;
}


error source_line_bytes(Source* src, u32 line, const u8** out_linep, u32* out_len) {
  if (line == 0)
    return err_invalid;

  error err = source_compute_lineoffs(src);
  if (err)
    return err;

  if (src->lineoffs.len < line)
    return err_not_found;

  u32 start = array_at(&src->lineoffs, line - 1);
  *out_linep = src->body + start;

  if (line < src->lineoffs.len) {
    u32 next_start = array_at(&src->lineoffs, line) - 1;
    *out_len = next_start - start;
  } else {
    *out_len = (src->body + src->len) - *out_linep;
  }

  return 0;
}
