// information for an entire build session
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PARSE_BUILDCTX_IMPLEMENTATION
#endif
#include "array.c"
#include "sym.c"

#include "type.c"
#include "pos.c"
#include "ast.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

typedef struct BuildCtx   BuildCtx;
typedef struct Diagnostic Diagnostic; // diagnostic message
typedef u8                DiagLevel;  // diagnostic level (Error, Warn ...)

// DiagHandler callback type.
// msg is a preformatted error message and is only valid until this function returns.
typedef void(DiagHandler)(Diagnostic* d, void* userdata);

enum DiagLevel {
  DiagError,
  DiagWarn,
  DiagNote,
  DiagMAX = DiagNote,
} END_ENUM(DiagLevel)

struct Diagnostic {
  BuildCtx*   build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
};

typedef Array(Diagnostic*) DiagnosticArray;

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
  Sym             pkgid;     // e.g. "bar/cat"
  Source*         srclist;   // list of sources (linked via Source.next)

  // interned types
  SymMap types;

  // diagnostics
  DiagHandler* nullable diagh;     // diagnostics handler
  void* nullable        userdata;  // custom user data passed to error handler
  DiagLevel             diaglevel; // diagnostics filter (some > diaglevel is ignored)
  u32                   errcount;  // total number of errors since last call to build_init

  // temporary buffers for eg string formatting
  char tmpbuf[2][256];
};

// BuildCtxInit initializes a BuildCtx structure
void BuildCtxInit(BuildCtx*,
  Mem                   mem,
  SymPool*              syms,
  const char*           pkgid,
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


// b_add_source adds src to b->srclist
void b_add_source(BuildCtx*, Source* src);
error b_add_source_file(BuildCtx*, const char* filename);
error b_add_source_dir(BuildCtx*, const char* filename); // add all *.co files in dir


// b_mknode allocates and initializes a AST node, e.g. b_mknode(b,Id) => IdNode*
#define b_mknode(b, KIND) ((KIND##Node* nullable)b_mknodex((b),N##KIND))

// b_mknodex is like b_mknode but not typed
Node* nullable b_mknodex(BuildCtx* b, NodeKind kind);

// b_mkpkgnode creates a package node for b, setting
// pkg->name = b->pkgid
// pkg->scope = pkgscope
PkgNode* nullable b_mkpkgnode(BuildCtx* b, Scope* pkgscope);


// b_typeid_assign computes the type id of t, adds it to b->syms and assigns it to t->tid
Sym b_typeid_assign(BuildCtx* b, Type* t);

// b_typeid returns the type symbol identifying the type t.
// Mutates t and b->syms if t->tid is NULL.
static Sym b_typeid(BuildCtx* b, Type* t);

// bctx_typeeq returns true if x & y are equivalent types
static bool b_typeeq(BuildCtx* b, Type* x, Type* y);

// ----

// diag_fmt appends to dst a ready-to-print representation of a diagnostic message
bool diag_fmt(const Diagnostic* d, Str* s);

// diag_free frees a diagnostics object.
// It is useful when a ctx's mem is a shared allocator.
// Normally you'd just dipose an entire ctx mem arena instead of calling this function.
// Co never calls this itself but a user's diagh function may.
void diag_free(Diagnostic*);

// DiagLevelName returns a printable string like "error"
const char* DiagLevelName(DiagLevel);

//———————————————————————————————————————————————————————————————————————————————————————
// internal

inline static Sym b_typeid(BuildCtx* b, Type* t) {
  return t->tid ? t->tid : b_typeid_assign(b, t);
}

bool _b_typeeq(BuildCtx*, Type* x, Type* y);
inline static bool b_typeeq(BuildCtx* b, Type* x, Type* y) {
  return x == y || _b_typeeq(b, x, y);
}

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PARSE_BUILDCTX_IMPLEMENTATION

#include "sys.c"

#include "universe.c"
#include "typeid.c"

#ifndef CO_NO_LIBC
  #include <stdio.h> // vsnprintf
#endif

void BuildCtxInit(
  BuildCtx*             ctx,
  Mem                   mem,
  SymPool*              syms,
  const char*           pkgid,
  DiagHandler* nullable diagh,
  void*                 userdata)
{
  assert(strlen(pkgid) > 0);
  memset(ctx, 0, sizeof(BuildCtx));
  ctx->mem       = mem;
  ctx->safe      = true;
  ctx->syms      = syms;
  ctx->pkgid     = symget(syms, pkgid, strlen(pkgid));
  ctx->diagh     = diagh;
  ctx->userdata  = userdata;
  ctx->diaglevel = DiagMAX;
  ctx->sint_type = sizeof(long) > 4 ? TC_i64 : TC_i32; // default to host size
  ctx->uint_type = sizeof(long) > 4 ? TC_u64 : TC_u32;
  array_init(&ctx->diagarray, NULL, 0);
  symmap_init(&ctx->types, mem, 1);
  posmap_init(&ctx->posmap);
}

void BuildCtxDispose(BuildCtx* ctx) {
  array_free(&ctx->diagarray);
  symmap_free(&ctx->types);
  posmap_dispose(&ctx->posmap);
  #if DEBUG
  memset(ctx, 0, sizeof(BuildCtx));
  #endif
}

static Diagnostic* b_mkdiag(BuildCtx* ctx) {
  Diagnostic* d = memalloct(Diagnostic);
  d->build = ctx;
  array_push(&ctx->diagarray, d);
  return d;
}

void b_diag(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* message) {
  if (level <= DiagError)
    ctx->errcount++;
  if (level > ctx->diaglevel || ctx->diagh == NULL)
    return;
  Diagnostic* d = b_mkdiag(ctx);
  d->level = level;
  d->pos = pos;
  d->message = mem_strdup(ctx->mem, message);
  ctx->diagh(d, ctx->userdata);
}

void b_diagv(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* fmt, va_list ap) {
  if (level > ctx->diaglevel || ctx->diagh == NULL) {
    if (level <= DiagError)
      ctx->errcount++;
    return;
  }
  #ifdef CO_NO_LIBC
    #warning TODO implement b_diagv for non-libc (need vsnprintf)
    // Note: maybe we can implement str_appendfmtv for non-libc and use that?
  #else
    char buf[256];
    va_list ap1;
    va_copy(ap1, ap);
    isize n = vsnprintf(buf, sizeof(buf), fmt, ap1);
    if LIKELY(n < (isize)sizeof(buf))
      return b_diag(ctx, level, pos, buf);
    // buf too small; heap allocate
    char* buf2 = (char*)mem_alloc(ctx->mem, n + 1);
    n = vsnprintf(buf2, n + 1, fmt, ap);
    b_diag(ctx, level, pos, buf2);
    mem_free(ctx->mem, buf2, n + 1);
  #endif
}

void b_diagf(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, level, pos, fmt, ap);
  va_end(ap);
}

void b_errf(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, DiagError, pos, fmt, ap);
  va_end(ap);
}

void b_warnf(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, DiagWarn, pos, fmt, ap);
  va_end(ap);
}

void b_notef(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, DiagNote, pos, fmt, ap);
  va_end(ap);
}


void b_add_source(BuildCtx* b, Source* src) {
  if (b->srclist)
    src->next = b->srclist;
  b->srclist = src;
}

error b_add_file(BuildCtx* b, const char* filename) {
  Source* src = mem_alloczt(b->mem, Source);
  error err = source_open_file(src, filename);
  if (err) {
    mem_free(b->mem, src, sizeof(Source));
    return err;
  }
  b_add_source(b, src);
  return 0;
}

error b_add_dir(BuildCtx* b, const char* filename) {
  FSDir d;
  error err = sys_dir_open(filename, &d);
  if (err)
    return err;

  FSDirEnt e;
  error err1;
  while ((err = sys_dir_read(d, &e)) > 0) {
    switch (e.type) {
      case FSDirEnt_REG:
      case FSDirEnt_LNK:
      case FSDirEnt_UNKNOWN:
        if (e.namlen < 4 || e.name[0] == '.' || strcmp(&e.name[e.namlen-2], ".co") != 0)
          break; // skip file
        if ((err = b_add_file(b, e.name)))
          goto end;
        break;
      default:
        break;
    }
  }
end:
  err1 = sys_dir_close(d);
  return err < 0 ? err : err1;
}


Node* nullable b_mknodex(BuildCtx* b, NodeKind kind) {
  Node* n = NodeAlloc(b->mem);
  if (LIKELY(n != NULL)) {
    memset(n, 0, sizeof(union NodeUnion));
    NodeInit(n, kind);
  }
  return n;
}


PkgNode* nullable b_mkpkgnode(BuildCtx* b, Scope* pkgscope) {
  auto pkg = b_mknode(b, Pkg);
  if (LIKELY(pkg != NULL)) {
    pkg->name = b->pkgid;
    pkg->scope = pkgscope;
  }
  return pkg;
}


Sym b_typeid_assign(BuildCtx* b, Type* t) {
  // Note: built-in types have predefined type ids (defined in universe)
  char buf[128];
  usize len = typeid_make(buf, sizeof(buf), t);
  if (LIKELY( len < sizeof(buf) ))
    return t->tid = symget(b->syms, buf, len);
  // didn't fit in stack buffer; resort to heap allocation
  len++;
  char* s = mem_alloc(b->mem, len);
  if (!s) {
    b_errf(b, (PosSpan){0}, "out of memory");
    return kSym__;
  }
  len = typeid_make(s, len, t);
  t->tid = symget(b->syms, s, len);
  mem_free(b->mem, s, len);
  return t->tid;
}

bool _b_typeeq(BuildCtx* b, Type* x, Type* y) {
  // invariant: x != y
  assertnotnull(x);
  assertnotnull(y);
  if (x->kind != y->kind)
    return false;
  if (is_BasicTypeNode(x)) // all BasicTypeNodes have precomputed type id
    return x->tid == y->tid;
  return b_typeid(b, x) == b_typeid(b, y);
}

#if 0
// TODO Type* InternASTType(Build* b, Type* t) {
  if (t->kind == NBasicType)
    return t;
  auto tid = GetTypeID(b, t);
  auto t2 = SymMapGet(&b->types, tid);
  if (t2)
    return t2;
  SymMapSet(&b->types, tid, t);
  return t;
}
#endif


// --- functions to aid unit tests

#if defined(CO_TEST) && !defined(CO_NO_LIBC)

BuildCtx* b_testctx_new() {
  Mem mem = mem_libc_allocator();

  SymPool* syms = memalloct(mem, SymPool);
  sympool_init(syms, universe_syms(), mem, NULL);

  Pkg* pkg = memalloct(mem, Pkg);
  pkg->dir = ".";

  BuildCtx* b = memalloct(mem, BuildCtx);
  b_init(b, mem, syms, pkg, NULL, NULL);

  return b;
}

void b_testctx_free(BuildCtx* b) {
  auto mem = b->mem;
  sympool_dispose(b->syms);
  memfree(mem, b->pkg);
  memfree(mem, b->syms);
  b_dispose(b);
  // MemLinearFree(mem);
}

#endif // defined(CO_TEST) && !defined(CO_NO_LIBC)

#endif // PARSE_BUILDCTX_IMPLEMENTATION
