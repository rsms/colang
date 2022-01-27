#pragma once
#include "../coimpl.h"
#include "../mem.h"
#include "../array.h"
#include "../str.h"
#include "../sym.h"

#include "token.h"  // Tok { T* }
#include "type.h"   // TypeCode { TC_* }, TypeFlags { TF_*}
#include "source.h" // Source, Pkg
#include "pos.h"    // Pos, PosMap, PosSpan
#include "ast.h"    // Scope, Node types, NodeKind { N* }, NodeFlags { NF_* }
#include "universe.h"

ASSUME_NONNULL_BEGIN

typedef struct BuildCtx   BuildCtx;
typedef struct Parser     Parser;     // parser state (includes Scanner)
typedef u8                ParseFlags; // flags for changing parser behavior
typedef struct Scanner    Scanner;    // lexical scanner state
typedef struct Diagnostic Diagnostic; // diagnostic message
typedef u8                DiagLevel;  // diagnostic level (Error, Warn ...)
typedef struct Comment    Comment;    // source comment
typedef struct Indent     Indent;     // source indentation

enum DiagLevel {
  DiagError,
  DiagWarn,
  DiagNote,
  DiagMAX = DiagNote,
} END_TYPED_ENUM(DiagLevel)

enum ParseFlags {
  ParseFlagsDefault = 0,
  ParseComments     = 1 << 1, // parse comments, populating S.comments_{head,tail}
  ParseOpt          = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
} END_TYPED_ENUM(ParseFlags)

// DiagHandler callback type.
// msg is a preformatted error message and is only valid until this function returns.
typedef void(DiagHandler)(Diagnostic* d, void* userdata);

struct Diagnostic {
  BuildCtx*   build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
};

DEF_TYPED_ARRAY(DiagnosticArray, Diagnostic*)

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

// Comment is a scanned comment
struct Comment {
  struct Comment* next; // next comment in linked list
  Source*         src;  // source
  const u8*       ptr;  // ptr into source
  u32             len;  // byte length
};

// Indent tracks source indetation
struct Indent {
  bool isblock; // true if this indent is a block
  u32  n;       // number of whitespace chars
};

// Scanner reads source code and produces tokens
struct Scanner {
  BuildCtx*  build;        // build context (memory allocator, sympool, pkg, etc.)
  Source*    src;          // input source
  u32        srcposorigin;
  ParseFlags flags;
  bool       insertSemi;   // insert a semicolon before next newline
  const u8*  inp;          // input buffer current pointer
  const u8*  inend;        // input buffer end

  // indentation
  Indent indent;           // current level
  Indent indentDst;        // unwind to level
  struct { // previous indentation levels (Indent elements)
    Indent* v;
    u32     len;
    u32     cap;
    Indent  storage[16];
  } indentStack;

  // token
  Tok        tok;           // current token
  const u8*  tokstart;      // start of current token
  const u8*  tokend;        // end of current token
  const u8*  prevtokend;    // end of previous token
  Sym        name;          // Current name (valid for TId and keywords)

  u32        lineno;        // source position line
  const u8*  linestart;     // source position line start pointer (for column)

  Comment*   comments_head; // linked list head of comments scanned so far
  Comment*   comments_tail; // linked list tail of comments scanned so far
};

// Parser holds state used during parsing
struct Parser {
  Scanner   s;        // parser is based on a scanner
  BuildCtx* build;    // build context
  Scope*    pkgscope; // package-level scope
  Node*     expr;     // most recently parsed expression
  u32       fnest;    // function nesting level

  // set when parsing named type e.g. "type Foo ..."
  Sym nullable typename;

  // ctxtype is non-null when the parser is confident about the type context
  Type* nullable ctxtype;

  // scopestack is used for tracking identifiers during parsing.
  // This is a simple stack which we do a linear search on when looking up identifiers.
  // It is faster than using chained hash maps in most cases because of cache locality
  // and the fact that...
  // 1. Most identifiers reference an identifier defined nearby. For example:
  //      x = 3
  //      A = x + 5
  //      B = x - 5
  // 2. Most bindings are short-lived and temporary ("locals") which means we can
  //    simply change a single index pointer to "unwind" an entire scope of bindings and
  //    then reuse that memory for the next binding scope.
  //
  // base is the offset in ptr to the current scope's base. Loading ptr[base] yields a uintptr
  // that is the next scope's base index.
  // keys (Sym) and values (Node) are interleaved in ptr together with saved base pointers.
  struct {
    uintptr cap;          // capacity of ptr (count, not bytes)
    uintptr len;          // current length (use) of ptr
    uintptr base;         // current scope's base index into ptr
    void**  ptr;          // entries
    void*   storage[256]; // initial storage in parser's memory
  } scopestack;
};

// ======================================================================================

// buildctx_init initializes a BuildCtx structure
void buildctx_init(BuildCtx*,
  Mem                   mem,
  SymPool*              syms,
  Pkg*                  pkg,
  DiagHandler* nullable diagh,
  void* nullable        userdata // passed along to diagh
);

// buildctx_dispose frees up internal resources. BuildCtx can be reused with buildctx_init after this call.
void buildctx_dispose(BuildCtx*);

// buildctx_diag invokes b->diagh with message (the message's bytes are copied into b->mem)
void buildctx_diag(BuildCtx*, DiagLevel, PosSpan, const char* message);

// buildctx_diagv formats a diagnostic message and invokes ctx->diagh
void buildctx_diagv(BuildCtx*, DiagLevel, PosSpan, const char* format, va_list);

// buildctx_diagf formats a diagnostic message invokes b->diagh
void buildctx_diagf(BuildCtx*, DiagLevel, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 4, 5);

// buildctx_errf calls buildctx_diagf with DiagError
void buildctx_errf(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// buildctx_warnf calls buildctx_diagf with DiagWarn
void buildctx_warnf(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// buildctx_warnf calls buildctx_diagf with DiagNote
void buildctx_notef(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

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

// ----

// scan_init initializes a scanner. Returns false if SourceOpenBody fails.
error scan_init(Scanner*, BuildCtx*, Source*, ParseFlags);

// scan_dispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as scan_init calls SourceOpenBody.
void scan_dispose(Scanner*);

// scan_next scans the next token
Tok scan_next(Scanner*);

// scan_pos returns the source position of s->tok (current token)
Pos scan_pos(const Scanner* s);

// scan_tokstr returns a token's string value and length, which is a pointer
// into the source's body.
inline static const u8* scan_tokstr(const Scanner* s, usize* len_out) {
  *len_out = (usize)(s->tokend - s->tokstart);
  return s->tokstart;
}

// scan_comment_pop removes and returns the least recently scanned comment.
// The caller takes ownership of the comment and should free it using memfree(s->mem,comment).
Comment* nullable scan_comment_pop(Scanner* s);

// parse a translation unit and return AST or NULL on error (reported to diagh)
// Expects p to be zero-initialized on first call. Can reuse p after return.
Node* nullable parse(Parser* p, BuildCtx*, Source*, ParseFlags, Scope* pkgscope);


static bool TypeEquals(BuildCtx* ctx, Type* x, Type* y); // true if x is same type as y
bool _TypeEquals(BuildCtx* ctx, Type* x, Type* y); // impl parse_type.c
inline static bool TypeEquals(BuildCtx* ctx, Type* x, Type* y) {
  return x == y || _TypeEquals(ctx, x, y);
}


ASSUME_NONNULL_END
