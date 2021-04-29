#pragma once
#include "util/sym.h"

typedef struct Pkg Pkg;
typedef struct Source Source;
typedef struct SrcPos SrcPos;

// CoOptType identifies an optimization type/strategy
typedef enum CoOptType {
  CoOptNone,       // -O0
  CoOptSmall,      // -Oz
  CoOptAggressive, // -O3
} CoOptType;

// ErrorHandler callback type
// msg is a preformatted error message and is only valid until this function returns
typedef void(ErrorHandler)(SrcPos pos, const Str msg, void* userdata);

// Build holds information for one "build" of one top-level package
typedef struct Build {
  Mem nullable           mem;      // memory space for AST nodes etc.
  Pkg*                   pkg;      // top-level package for which we are building
  CoOptType              opt;      // optimization type
  SymPool*               syms;     // symbol pool
  ErrorHandler* nullable errh;     // error handler
  void* nullable         userdata; // custom user data passed to error handler
} Build;

// SrcPos represents a source code location
// TODO: considering implementing something like lico and Pos/XPos from go
//       https://golang.org/src/cmd/internal/src/pos.go
//       https://golang.org/src/cmd/internal/src/xpos.go
typedef struct SrcPos {
  Source* src;   // source
  u32     offs;  // offset into src->buf
  u32     span;  // span length. 0 = unknown or does no apply.
} SrcPos;

typedef struct { u32 line; u32 col; } LineCol;

// Pkg represents a package; a directory of source files
typedef struct Pkg {
  Mem         mem;     // memory for resources only needed by this package
  const char* dir;     // directory filename
  Str         id;      // fully qualified name (e.g. "bar/cat/foo")
  Str         name;    // relative name (e.g. "foo")
  Source*     srclist; // linked list of sources
} Pkg;

// Source represents an input source file
typedef struct Source {
  Source*    next;     // next source in list
  const Pkg* pkg;      // package this source belongs to
  Str        filename; // copy of filename given to SourceOpen
  const u8*  body;     // file body (usually mmap'ed)
  size_t     len;      // size of body in bytes
  u8         sha1[20]; // SHA-1 checksum of body, set with SourceChecksum
  int        fd;       // file descriptor
  bool       ismmap;   // true if the file is memory-mapped

  // state used by SrcPos functions (lazy-initialized)
  u32* lineoffs; // line-start offsets into body
  u32  nlines;   // total number of lines
} Source;

// build_init initializes a Build structure
void build_init(Build*,
  Mem nullable           mem,
  SymPool*               syms,
  Pkg*                   pkg,
  ErrorHandler* nullable errh,
  void* nullable         userdata);

// build_dispose frees up internal resources used by Build
void build_dispose(Build*);

// build_errf formats a message including source position and invokes ctx->errh
void build_errf(const Build* ctx, SrcPos, const char* format, ...);

#if R_UNIT_TEST_ENABLED
// test_build_new creates a new Build in a new isolated Mem space with new pkg and syms
Build* test_build_new();
void   test_build_free(Build*);
#endif

bool PkgAddFileSource(Pkg* pkg, const char* filename); // false on error (check errno)
void PkgAddSource(Pkg* pkg, Source* src);
bool PkgScanSources(Pkg* pkg);

bool SourceOpen(Source* src, const Pkg*, const char* filename);
void SourceInitMem(Source* src, const Pkg*, const char* filename, const char* text, size_t len);
bool SourceOpenBody(Source* src);
bool SourceCloseBody(Source* src);
bool SourceClose(Source* src);
void SourceDispose(Source* src);
void SourceChecksum(Source* src);

// NoSrcPos is the "null" of SrcPos
#define NoSrcPos (({ SrcPos p = {NULL,0,0}; p; }))

// LineCol
LineCol SrcPosLineCol(SrcPos);

// SrcPosFmt appends "file:line:col: format ..." to s including contextual source details
Str SrcPosFmt(SrcPos, Str s, const char* fmt, ...) ATTR_FORMAT(printf, 3, 4);
Str SrcPosFmtv(SrcPos, Str s, const char* fmt, va_list);

// SrcPosStr appends "file:line:col" to s
Str SrcPosStr(SrcPos, Str s);
