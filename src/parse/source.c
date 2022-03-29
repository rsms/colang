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
  U32Array  lineoffs;   // [lineno] => offset in body (populated by source_compute_lineoffs)
};

error source_open_file(Source* src, const char* filename);
error source_open_data(Source* src, const char* filename, const char* text, u32 len);
error source_body_open(Source* src);
error source_body_close(Source* src);
error source_close(Source* src); // src can be reused with open after this call
void  source_checksum(Source* src); // populates src->sha256 <= sha256(src->body)
error source_compute_lineoffs(Source* src); // populates src->lineoffs if needed

// source_line_bytes sets *out_linep to start of line in src->body and sets *out_len
// to the line's length in bytes (excluding "\n")
error source_line_bytes(Source* src, u32 line, const u8** out_linep, u32* out_len);

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
  array_free(&src->lineoffs);
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
    if (src->body[i++] == '\n' && i < src->len)
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


#endif // PARSE_SOURCE_IMPLEMENTATION
