#pragma once

typedef struct Source Source;

// Pkg represents a package; a directory of source files
typedef struct Pkg {
  Mem         mem;     // memory for resources only needed by this package
  const char* dir;     // directory filename
  Source*     srclist; // linked list of sources
} Pkg;

bool PkgAddSource(Pkg* pkg, const char* filename);
bool PkgScanSources(Pkg* pkg);


// Source represents an input source file
typedef struct Source {
  Source*   next;     // next source in list
  Pkg*      pkg;      // package this source belongs to
  Str       filename; // copy of filename given to SourceOpen
  const u8* body;     // file body (usually mmap'ed)
  size_t    len;      // size of body in bytes
  u8        sha1[20]; // SHA-1 checksum of body, set with SourceChecksum
  int       fd;       // file descriptor
  bool      ismmap;   // true if the file is memory-mapped

  // state used by SrcPos functions (lazy-initialized)
  u32* lineoffs; // line-start offsets into body
  u32  nlines;   // total number of lines
} Source;

bool SourceOpen(Pkg* pkg, Source* src, const char* filename);
bool SourceOpenBody(Source* src);
bool SourceCloseBody(Source* src);
bool SourceClose(Source* src);
void SourceDispose(Source* src);
void SourceChecksum(Source* src);


// SrcPos represents a source code location
// TODO: considering implementing something like lico and Pos/XPos from go
//       https://golang.org/src/cmd/internal/src/pos.go
//       https://golang.org/src/cmd/internal/src/xpos.go
typedef struct {
  Source* src;   // source
  u32     offs;  // offset into src->buf
  u32     span;  // span length. 0 = unknown or does no apply.
} SrcPos;

// NoSrcPos is the "null" of SrcPos
#define NoSrcPos (({ SrcPos p = {NULL,0,0}; p; }))

// LineCol
typedef struct { u32 line; u32 col; } LineCol;

Str SrcPosMsg(SrcPos, const char* nullable message);
Str SrcPosFmt(SrcPos pos); // appends "<file>:<line>:<col>" to dst
LineCol SrcPosLineCol(SrcPos);
