// representations of source files
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

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
error source_checksum(Source* src); // populates src->sha256 <= sha256(src->body)
error source_compute_lineoffs(Source* src); // populates src->lineoffs if needed

// source_line_bytes sets *out_linep to start of line in src->body and sets *out_len
// to the line's length in bytes (excluding "\n")
error source_line_bytes(Source* src, u32 line, const u8** out_linep, u32* out_len);


END_INTERFACE
