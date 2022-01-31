// source -- representations of input source files and packages
#pragma once
#include "../str.h"
ASSUME_NONNULL_BEGIN

typedef struct Source Source; // an input source file

struct Source {
  Source*   next;       // list link
  Str       filename;   // copy of filename given to source_open
  const u8* body;       // file body (usually mmap'ed)
  u32       len;        // size of body in bytes
  int       fd;         // file descriptor
  u8        sha256[32]; // SHA-256 checksum of body, set with source_checksum
  bool      ismmap;     // true if the file is memory-mapped
};

error source_open_file(Source* src, Mem mem, const char* filename);
error source_open_data(Source* src, Mem mem, const char* filename, const char* text, u32 len);
error source_body_open(Source* src);
error source_body_close(Source* src);
error source_close(Source* src); // src can be reused with open after this call
void  source_checksum(Source* src); // populates src->sha256 <= sha256(src->body)

ASSUME_NONNULL_END
