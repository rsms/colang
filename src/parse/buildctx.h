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
  OptNone     = '0', // no optimizations
  OptMinimal  = '1', // compile quickly with a few optimizations
  OptBalanced = '2', // balance between performance, compile time and code size
  OptPerf     = '3', // maximize performance at cost of compile time and code size
  OptSize     = 's', // small code size and good performance at cost of compile time
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

  SymMap         types;     // interned types
  BasicTypeNode* sint_type; // concrete type of "int"
  BasicTypeNode* uint_type; // concrete type of "uint"

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

  // temporary buffers
  char       tmpbuf[2][512];
  Node_union tmpnode;
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

// b_err_nomem calls b_diagf with a "out of memory" message and returns a pointer
// to b->tmpnode that is safe to mutate (but effects of use are UD, of course.)
Node* b_err_nomem(BuildCtx*, PosSpan);


// b_add_source adds src to b->srclist
void b_add_source(BuildCtx*, Source* src);
error b_add_source_file(BuildCtx*, const char* filename);
error b_add_source_dir(BuildCtx*, const char* filename); // add all *.co files in dir

// All b_mknode* functions report memory-allocation failures via b_err_nomem.

// b_mknode allocates and initializes a AST node (e.g. b_mknode(b, Id, NoPos) => IdNode*)
// Returns b->tmpnode on memory-allocation failure.
#define b_mknode(b, KIND, pos) \
  ( (KIND##Node*)_b_mknode((b), N##KIND, (pos), _PTRCOUNT(sizeof(KIND##Node))) )

// b_mknode_union allocates a union of nodes, initializing it as the lowest NodeKind.
// Returns b->tmpnode on memory-allocation failure.
#define b_mknode_union(b, KIND, pos) \
  ( (KIND##Node*)_b_mknode((b), N##KIND##_BEG, (pos), _PTRCOUNT(sizeof(KIND##Node_union))) )

// b_mknodev allocates a node with plain array tail,
// e.g. struct{int field[]}
#define b_mknodev(b, KIND, pos, ARRAY_FIELD, count) \
  ( (KIND##Node* nullable)_b_mknodev( \
      (b), N##KIND, (pos), _PTRCOUNT(STRUCT_SIZE((KIND##Node*)0, ARRAY_FIELD, (count))) ) )

// b_mknode_array allocates a node with an Array(T) field, initialized with tail storage,
// e.g. struct{Array(int) field}
#define b_mknode_array(b, KIND, pos, ARRAY_FIELD, count) ( \
  (KIND##Node* nullable)_b_mknode_array( \
    (b), N##KIND, (pos), \
    ALIGN2(sizeof(KIND##Node), sizeof(void*)), \
    offsetof(KIND##Node, ARRAY_FIELD), \
    sizeof(((KIND##Node*)0)->ARRAY_FIELD.v[0]), \
    (count) ) \
)

#define _PTRCOUNT(size) ALIGN2(((usize)(size)),sizeof(void*))/sizeof(void*)

Node* _b_mknode(BuildCtx* b, NodeKind kind, Pos pos, usize nptrs);
Node* nullable _b_mknodev(BuildCtx* b, NodeKind kind, Pos pos, usize nptrs);
Node* nullable _b_mknode_array(
  BuildCtx* b, NodeKind kind, Pos pos,
  usize structsize, uintptr array_offs, usize elemsize, u32 cap);

// b_free_node returns a node to b's nodeslab free list
#define b_free_node(b, n, KIND) _b_free_node((b),as_Node(n),_PTRCOUNT(sizeof(KIND##Node)) )
void _b_free_node(BuildCtx* b, Node* n, usize nptrs);

#define b_copy_node(b, src) ( (__typeof__(*(src))*)_b_copy_node((b),as_const_Node(src)) )
Node* _b_copy_node(BuildCtx* b, const Node* src);

// b_mkpkgnode creates a package node for b, setting
//   pkg->name = b->pkgid
//   pkg->scope = pkgscope
// Note: returns b->tmpnode on memory-allocation failure.
PkgNode* b_mkpkgnode(BuildCtx* b, Scope* pkgscope);


// b_typeid_assign computes the type id of t, adds it to b->syms and assigns it to t->tid
Sym b_typeid_assign(BuildCtx* b, Type* t);

// b_typeid returns the type symbol identifying the type t.
// Mutates t and b->syms if t->tid is NULL.
static Sym b_typeid(BuildCtx* b, Type* t);

// bctx_typeeq returns true if x & y are equivalent types
static bool b_typeeq(BuildCtx* b, Type* x, Type* y);

// b_typelteq returns true if src can be downgraded to dst; if dst <= src.
// (e.g. true if src can be assigned to a field or local of type dst.)
static bool b_typelteq(BuildCtx* b, Type* dst, Type* src);

// b_intern_type registers t in b->types.
// If an existing equivalent (same id) type exists, *tp is updated to that other pointer.
// Returns true if *tp was updated with a different but equivalent type.
#define b_intern_type(b, tp) ( assert_is_Type(*(tp)), _b_intern_type((b),(Type**)(tp)) )
bool _b_intern_type(BuildCtx* b, Type** tp);


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

// dst <= src -- true if src can be downgraded to dst.
// e.g. true if src can be assigned to a field or local of type dst.
bool _b_typelteq(BuildCtx* b, Type* dst, Type* src);
inline static bool b_typelteq(BuildCtx* b, Type* dst, Type* src) {
  return dst == src || _b_typelteq(b, dst, src);
}


END_INTERFACE
