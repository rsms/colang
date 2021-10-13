#pragma once
#include "util/sym.h"
#include "util/symmap.h"
#include "util/array.h"
#include "pos.h"
#include "types.h"

ASSUME_NONNULL_BEGIN

typedef struct Build  Build;
typedef struct Pkg    Pkg;
typedef struct Source Source;

// CoOptType identifies an optimization type/strategy
typedef enum CoOptType {
  CoOptNone,  // -O0
  CoOptFast,  // -O3
  CoOptSmall, // -Oz
} CoOptType;

// DiagLevel is the level of severity of a diagnostic message
typedef enum DiagLevel {
  DiagError,
  DiagWarn,
  DiagNote,
  DiagMAX = DiagNote,
} DiagLevel;

typedef struct Diagnostic {
  Build*      build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
} Diagnostic;

// DiagHandler callback type.
// msg is a preformatted error message and is only valid until this function returns.
typedef void(DiagHandler)(Diagnostic* d, void* userdata);

// Build holds information for one "build" of one top-level package
struct Build {
  Mem                   mem;       // memory space for AST nodes, diagnostics etc.
  Pkg*                  pkg;       // top-level package for which we are building
  CoOptType             opt;       // optimization type
  bool                  debug;     // build a debug build (include debug information etc)
  bool                  safe;      // enable boundary checks and memory ref checks
  SymPool*              syms;      // symbol pool
  SymMap                types;     // interned types
  DiagHandler* nullable diagh;     // diagnostics handler
  void* nullable        userdata;  // custom user data passed to error handler
  u32                   errcount;  // total number of errors since last call to build_init
  TypeCode              sint_type; // concrete type of "int"
  TypeCode              uint_type; // concrete type of "uint"
  DiagLevel             diaglevel; // diagnostics filter (some > diaglevel is ignored)
  Array                 diagarray; // all diagnostic messages produced. Stored in mem.
  PosMap                posmap;    // maps Source <-> Pos
};

// Pkg represents a package; a directory of source files
struct Pkg {
  Mem         mem;     // memory for resources only needed by this package
  const char* dir;     // directory filename
  Str         id;      // fully qualified name (e.g. "bar/cat"); TODO: consider using Sym
  Str         name;    // relative name (e.g. "foo")
  Source*     srclist; // linked list of sources
};

// Source represents an input source file
struct Source {
  Source*    next;     // next source in list
  const Pkg* pkg;      // package this source belongs to
  Str        filename; // copy of filename given to SourceOpen
  const u8*  body;     // file body (usually mmap'ed)
  size_t     len;      // size of body in bytes
  u8         sha1[20]; // SHA-1 checksum of body, set with SourceChecksum
  int        fd;       // file descriptor
  bool       ismmap;   // true if the file is memory-mapped
};

// build_init initializes a Build structure
void build_init(Build*,
  Mem                    mem,
  SymPool*               syms,
  Pkg*                   pkg,
  DiagHandler* nullable  diagh,
  void* nullable         userdata);

// build_dispose frees up internal resources used by Build
void build_dispose(Build*);

// build_emit_diag invokes b->diagh. d must have been allocated in b->mem.
static void build_emit_diag(Build* b, Diagnostic* d);

// build_mkdiag creates a new Diagnostic struct but does not call b->diagh.
// After making a Diagnostic you should initialize its members and call build_emit_diag.
// If you are using diag_free the diagnostic's message must be allocated in b->mem.
Diagnostic* build_mkdiag(Build*);

// build_diag invokes b->diagh with message (the message's bytes are copied into b->mem)
void build_diag(Build*, DiagLevel, PosSpan, const char* message);

// build_diagv formats a diagnostic message invokes b->diagh
void build_diagv(Build*, DiagLevel, PosSpan, const char* format, va_list);

// build_diagf formats a diagnostic message invokes b->diagh
void build_diagf(Build*, DiagLevel, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 4, 5);

// build_errf calls build_diagf with DiagError
void build_errf(Build*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// build_warnf calls build_diagf with DiagWarn
void build_warnf(Build*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// build_warnf calls build_diagf with DiagNote
void build_notef(Build*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);


// diag_fmt appends to s a ready-to-print representation of a Diagnostic message.
Str diag_fmt(Str s, const Diagnostic*);

// diag_free frees a diagnostics object.
// It is useful when a build's mem is a shared allocator.
// Normally you'd just dipose an entire build mem arena instead of calling this function.
// Co never calls this itself but a user's diagh function may.
void diag_free(Diagnostic*);

// DiagLevelName returns a printable string like "error".
const char* DiagLevelName(DiagLevel);

// build_get_source returns the source file corresponding to Pos.
// Returns NULL if Pos does not name a source in the build (e.g. for generated code.)
static const Source* nullable build_get_source(const Build*, Pos);

#if R_TESTING_ENABLED
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


// -----------------------------------------------------------------------------------------------
// implementations

inline static const Source* nullable build_get_source(const Build* b, Pos pos) {
  return (Source*)pos_source(&b->posmap, pos);
}

inline static void build_emit_diag(Build* b, Diagnostic* d) {
  if (d->level <= b->diaglevel && b->diagh)
    b->diagh(d, b->userdata);
}

ASSUME_NONNULL_END
