#pragma once
#include "../mem.c"
#include "../array.h"
#include "../str.h"
#include "../sym.c"
#include "token.h"    // Tok { T* }
#include "type.h"     // TypeCode { TC_* }, TypeFlags { TF_*}
#include "source.h"   // Source, Pkg
#include "pos.h"      // Pos, PosMap, PosSpan
#include "ast.h"      // Scope, Node types, NodeKind { N* }, NodeFlags { NF_* }
#include "buildctx.h" // BuildCtx, Diagnostic, DiagLevel
#include "universe.h"
ASSUME_NONNULL_BEGIN


typedef struct Parser  Parser;     // parser state (includes Scanner)
typedef u8             ParseFlags; // flags for changing parser behavior
typedef struct Scanner Scanner;    // lexical scanner state
typedef struct Comment Comment;    // source comment
typedef struct Indent  Indent;     // source indentation


enum ParseFlags {
  ParseComments = 1 << 1, // parse comments, populating S.comments_{head,tail}
  ParseOpt      = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
} END_ENUM(ParseFlags)


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
  Scanner; // parser is based on a scanner

  Scope* pkgscope; // package-level scope
  u32    fnest;    // function nesting level
  error  err;      // !0 if a fatal error occurred (e.g. memory allocation failed)

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

// Scanner must be the head of the Parser struct to support parser scan state save & restore
static_assert(offsetof(Parser,build) == offsetof(Scanner,build),
  "Scanner is not the head of the Parser struct");


// ScannerInit initializes a scanner. Returns false if SourceOpenBody fails.
error ScannerInit(Scanner*, BuildCtx*, Source*, ParseFlags) WARN_UNUSED_RESULT;

// ScannerDispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as ScannerInit calls SourceOpenBody.
void ScannerDispose(Scanner*);

// ScannerNext scans the next token
Tok ScannerNext(Scanner*);

// ScannerPos returns the source position of s->tok (current token)
Pos ScannerPos(const Scanner* s);

// ScannerTokStr returns a token's string value and length, which is a pointer
// into the source's body.
inline static const u8* ScannerTokStr(const Scanner* s, usize* len_out) {
  *len_out = (usize)(s->tokend - s->tokstart);
  return s->tokstart;
}

// ScannerCommentPop removes and returns the least recently scanned comment.
// The caller takes ownership of the comment and should free it using memfree(s->mem,comment).
Comment* nullable ScannerCommentPop(Scanner* s);

// parse_cunit parses a translation unit, producing AST at *result.
// Expects p to be zero-initialized on first call. Can reuse p after return.
error parse_tu(Parser* p, BuildCtx*, Source*, ParseFlags, Scope* pkgscope, FileNode** result)
  WARN_UNUSED_RESULT;


ASSUME_NONNULL_END
