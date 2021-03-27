#pragma once
ASSUME_NONNULL_BEGIN

typedef struct Pkg Pkg;
typedef struct Source Source;

// Source represents an input source file
typedef struct Source {
  Source*   next;     // next source in list
  Pkg*      pkg;      // package this source belongs to
  char*     filename; // filename (null terminated, resides in pkg->memory)
  const u8* body;     // file body (usually mmap'ed)
  size_t    len;      // size of body in bytes
  u8        sha1[20]; // SHA-1 checksum of body, set with SourceChecksum
  int       fd;
  bool      ismmap;   // true if the file is memory-mapped
} Source;

bool SourceOpen(Pkg* pkg, Source* src, const char* filename);
bool SourceOpenBody(Source* src);
bool SourceCloseBody(Source* src);
bool SourceClose(Source* src);
void SourceDispose(Source* src);
void SourceChecksum(Source* src);

ASSUME_NONNULL_END
