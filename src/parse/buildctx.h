// information for an entire build session
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef struct BuildCtx   BuildCtx;
typedef struct Diagnostic Diagnostic; // diagnostic message
typedef u8                DiagLevel;  // diagnostic level (Error, Warn ...)
typedef u8                OptLevel;   // optimization level

// DiagHandler callback type.
// msg is a preformatted error message and is only valid until this function returns.
typedef void(DiagHandler)(Diagnostic* d, void* userdata);

enum DiagLevel {
  DiagError,
  DiagWarn,
  DiagNote,
  DiagMAX = DiagNote,
} END_ENUM(DiagLevel)

enum OptLevel {
  OptNone,  // like cc -O0
  OptSpeed, // like cc -O3
  OptSize,  // like cc -Os
} END_ENUM(OptLevel)

struct Diagnostic {
  BuildCtx*   build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
};

typedef Array(Diagnostic*) DiagnosticArray;

typedef struct NodeSlab NodeSlab;
struct NodeSlab {
  NodeSlab* nullable next; // next slab in the free list
  u32                len;  // number of used entries at data
  void*              data[4096/sizeof(void*) - 2];
};

struct BuildCtx {
  OptLevel opt;       // optimization level
  bool     debug;     // include debug information
  bool     safe;      // enable boundary checks and memory ref checks
  Mem      mem;       // memory allocator

  SymMap   types; // interned types
  TypeCode sint_type; // concrete type of "int"
  TypeCode uint_type; // concrete type of "uint"

  SymPool         syms;      // symbol pool
  DiagnosticArray diagarray; // all diagnostic messages produced. Stored in mem.
  PosMap          posmap;    // maps Source <-> Pos

  PkgNode pkg;
  Sym     pkgid;     // e.g. "bar/cat"
  Scope   pkgscope;

  NodeSlab  nodeslab_head; // first slab + list of additional data slabs
  NodeSlab* nodeslab_curr; // current slab

  Source* nullable srclist; // list of sources (linked via Source.next)

  // diagnostics
  DiagHandler* nullable diagh;     // diagnostics handler
  void* nullable        userdata;  // custom user data passed to error handler
  DiagLevel             diaglevel; // diagnostics filter (some > diaglevel is ignored)
  u32                   errcount;  // total number of errors since last call to build_init

  // temporary buffers for eg string formatting
  char tmpbuf[2][256];
};

// BuildCtxInit initializes a BuildCtx structure.
// userdata is passed along to DiagHandler.
error BuildCtxInit(
  BuildCtx*, Mem, const char* pkgid, DiagHandler* nullable, void* nullable userdata);

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


// b_mknode allocates and initializes a AST node (e.g. b_mknode(b, Id, NoPos) => IdNode*)
// b_mknodez allocates and initializes a AST node of particular byte size and kind.
#define b_mknode(b, KIND, pos) \
  ( (KIND##Node* nullable) b_mknodez((b), N##KIND, sizeof(KIND##Node), (pos)) )

#define b_mknode_union(b, KIND, pos) \
  ( (KIND##Node* nullable)b_mknodez((b), N##KIND##_BEG, sizeof(KIND##Node_union), (pos)) )

#define b_mknodez(b, kind, size, pos) \
  ( _b_mknode((b), (kind), (pos), ALIGN2((size),sizeof(void*))/sizeof(void*)) )

Node* nullable _b_mknode(BuildCtx* b, NodeKind kind, Pos pos, usize nptrs);

// b_clonenode allocates and initializes a AST node that is a copy of another node
#define b_clonenode(b, src) ((__typeof__(src) nullable)_b_clonenode((b),as_Node(src)))
Node* nullable _b_clonenode(BuildCtx* b, const Node* src);

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
bool diag_fmt(const Diagnostic* d, Str* dst);

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


END_INTERFACE
