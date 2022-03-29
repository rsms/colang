// representations of source files
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PARSE_SOURCE_IMPLEMENTATION
#endif
#include "array.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

typedef struct Source Source; // an input source file
typedef Array(Source*) SourceArray;

struct Source {
  Source*   next;       // list link
  char*     filename;   // copy of filename given to source_open
  const u8* body;       // file body (usually mmap'ed)
  u32       len;        // size of body in bytes
  int       fd;         // file descriptor
  u8        sha256[32]; // SHA-256 checksum of body, set with source_checksum
  bool      ismmap;     // true if the file is memory-mapped
};

error source_open_file(Source* src, const char* filename);
error source_open_data(Source* src, const char* filename, const char* text, u32 len);
error source_body_open(Source* src);
error source_body_close(Source* src);
error source_close(Source* src); // src can be reused with open after this call
void  source_checksum(Source* src); // populates src->sha256 <= sha256(src->body)

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PARSE_SOURCE_IMPLEMENTATION

#include "sha256.c"

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

error source_open_file(Source* src, const char* filename) {
  #ifdef CO_NO_LIBC
    return err_not_supported;
  #else
    error err = source_init(src, filename);
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

error source_open_data(Source* src, const char* filename, const char* text, u32 len){
  error err = source_init(src, filename);
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
  memfree(src->filename, strlen(src->filename) + 1);
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

#endif // PARSE_SOURCE_IMPLEMENTATION
