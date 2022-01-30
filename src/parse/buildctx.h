// build context
#pragma once
#include "../array.h"
#include "../sym.h"
#include "type.h"
#include "pos.h"
#include "ast.h"
ASSUME_NONNULL_BEGIN

typedef struct BuildCtx   BuildCtx;
typedef struct Diagnostic Diagnostic; // diagnostic message
typedef u8                DiagLevel;  // diagnostic level (Error, Warn ...)

// DiagHandler callback type.
// msg is a preformatted error message and is only valid until this function returns.
typedef void(DiagHandler)(Diagnostic* d, void* userdata);

DEF_TYPED_ARRAY(DiagnosticArray, Diagnostic*)

enum DiagLevel {
  DiagError,
  DiagWarn,
  DiagNote,
  DiagMAX = DiagNote,
} END_TYPED_ENUM(DiagLevel)

struct Diagnostic {
  BuildCtx*   build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
};

struct BuildCtx {
  bool     opt;       // optimize
  bool     debug;     // include debug information
  bool     safe;      // enable boundary checks and memory ref checks
  TypeCode sint_type; // concrete type of "int"
  TypeCode uint_type; // concrete type of "uint"

  Mem             mem;       // memory allocator
  SymPool*        syms;      // symbol pool
  DiagnosticArray diagarray; // all diagnostic messages produced. Stored in mem.
  PosMap          posmap;    // maps Source <-> Pos

  // interned types
  struct {
    SymMap       types;
    SymMapBucket types_st[8];
  };

  // build state
  Pkg* pkg; // top-level package for which we are building

  // diagnostics
  DiagHandler* nullable diagh;     // diagnostics handler
  void* nullable        userdata;  // custom user data passed to error handler
  DiagLevel             diaglevel; // diagnostics filter (some > diaglevel is ignored)
  u32                   errcount;  // total number of errors since last call to build_init
};

// BuildCtxInit initializes a BuildCtx structure
void BuildCtxInit(BuildCtx*,
  Mem                   mem,
  SymPool*              syms,
  Pkg*                  pkg,
  DiagHandler* nullable diagh,
  void* nullable        userdata // passed along to diagh
);

// BuildCtxDispose frees up internal resources.
// BuildCtx can be reused with BuildCtxInit after this call.
void BuildCtxDispose(BuildCtx*);

// b_diag invokes b->diagh with message (the message's bytes are copied into b->mem)
void b_diag(BuildCtx*, DiagLevel, PosSpan, const char* message);

// b_diagv formats a diagnostic message and invokes ctx->diagh
void b_diagv(BuildCtx*, DiagLevel, PosSpan, const char* format, va_list);

// b_diagf formats a diagnostic message invokes b->diagh
void b_diagf(BuildCtx*, DiagLevel, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 4, 5);

// b_errf calls b_diagf with DiagError
void b_errf(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// b_warnf calls b_diagf with DiagWarn
void b_warnf(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// b_warnf calls b_diagf with DiagNote
void b_notef(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// b_typeid_assign computes the type id of t, adds it to b->syms and assigns it to t->tid
Sym b_typeid_assign(BuildCtx* b, Type* t);

// b_typeid returns the type symbol identifying the type t.
// Mutates t and b->syms if t->tid is NULL.
inline static Sym b_typeid(BuildCtx* b, Type* t) {
  return t->tid ? t->tid : b_typeid_assign(b, t); }

// bctx_typeeq returns true if x & y are equivalent types
bool _b_typeeq(BuildCtx*, Type* x, Type* y);
inline static bool b_typeeq(BuildCtx* b, Type* x, Type* y) {
  return x == y || _b_typeeq(b, x, y); }

// ----

// diag_fmt appends to dst a ready-to-print representation of a diagnostic message
Str diag_fmt(const Diagnostic*, Str dst);

// diag_free frees a diagnostics object.
// It is useful when a ctx's mem is a shared allocator.
// Normally you'd just dipose an entire ctx mem arena instead of calling this function.
// Co never calls this itself but a user's diagh function may.
void diag_free(Diagnostic*);

// DiagLevelName returns a printable string like "error"
const char* DiagLevelName(DiagLevel);


ASSUME_NONNULL_END
