#pragma once
#include "../source/source.h"
#include "sym.h"
#include "types.h"

ASSUME_NONNULL_BEGIN

typedef struct Node Node;

// scanner tokens
#define TOKENS(_)  \
  _( TNone  , "TNone" ) \
  _( TComma , ",")      \
  _( TSemi  , ";")      \
  \
  _( T_PRIM_OPS_START , "") \
  /* primary "intrinsic" operator tokens, most of them mapping directly to IR ops */ \
  _( TStar          , "*")  \
  _( TSlash         , "/")  \
  _( TPercent       , "%")  \
  _( TShl           , "<<") \
  _( TShr           , ">>") \
  _( TAnd           , "&")  \
  _( TPlus          , "+")  \
  _( TMinus         , "-")  \
  _( TPipe          , "|")  \
  _( THat           , "^")  \
  _( TTilde         , "~")  \
  _( TExcalm        , "!")  \
  _( TEq            , "==") \
  _( TNEq           , "!=") \
  _( TLt            , "<")  \
  _( TLEq           , "<=") \
  _( TGt            , ">")  \
  _( TGEq           , ">=") \
  _( TPlusPlus      , "++") \
  _( TMinusMinus    , "--") \
  \
  _( T_PRIM_OPS_END , "") /* end of operator tokens */ \
  \
  _( TAssign        , "=")   \
  _( TShlAssign     , "<<=") \
  _( TShrAssign     , ">>=") \
  _( TPlusAssign    , "+=")  \
  _( TMinusAssign   , "-=")  \
  _( TStarAssign    , "*=")  \
  _( TSlashAssign   , "/=")  \
  _( TPercentAssign , "%=")  \
  _( TAndAssign     , "&=")  \
  _( TPipeAssign    , "|=")  \
  _( TTildeAssign   , "~=")  \
  _( THatAssign     , "^=")  \
  _( TLParen        , "(")   \
  _( TRParen        , ")")   \
  _( TLBrace        , "{")   \
  _( TRBrace        , "}")   \
  _( TLBrack        , "[")   \
  _( TRBrack        , "]")   \
  _( TAndAnd        , "&&")  \
  _( TPipePipe      , "||")  \
  _( TRArr          , "->")  \
  _( TId            , "id")  \
  _( TIntLit        , "int") \
  _( TFloatLit      , "float") \
  _( TIndent        , "indent") /* only produced when using flag ParseIndent */ \
/*END TOKENS*/
#define TOKEN_KEYWORDS(_) \
  _( as,          TAs)          \
  _( break,       TBreak)       \
  _( case,        TCase)        \
  _( continue,    TContinue)    \
  _( default,     TDefault)     \
  _( defer,       TDefer)       \
  _( else,        TElse)        \
  _( enum,        TEnum)        \
  _( for,         TFor)         \
  _( fun,         TFun)         \
  _( if,          TIf)          \
  _( import,      TImport)      \
  _( in,          TIn)          \
  _( interface,   TInterface)   \
  _( is,          TIs)          \
  _( mutable,     TMutable)     \
  _( nil,         TNil)         \
  _( return,      TReturn)      \
  _( select,      TSelect)      \
  _( struct,      TStruct)      \
  _( switch,      TSwitch)      \
  _( symbol,      TSymbol)      \
  _( type,        TType)        \
  _( while,       TWhile)       \
// Limited to a total of 31 keywords. See scan.c
//END TOKEN_KEYWORDS

// Tok enum
typedef enum {
  #define I_ENUM(name, str) name,
  TOKENS(I_ENUM)
  #undef I_ENUM

  // TKeywordsStart is used for 0-based keyword indexing.
  // Its explicit value is used by sym.c to avoid having to regenerate keyword symbols
  // whenever a non-keyword token is added. I.e. this number can be changed freely but will
  // require regeneration of the code in sym.c.
  TKeywordsStart = 0x100,
  #define I_ENUM(_str, name) name,
  TOKEN_KEYWORDS(I_ENUM)
  #undef I_ENUM
  TKeywordsEnd,

  TMax
} Tok;
// We only have 5 bits to encode tokens in Sym. Additionally, the value 0 is reserved
// for "not a keyword", leaving the max number of values at 31 (i.e. 2^5=32-1).
static_assert(TKeywordsEnd - TKeywordsStart <= 32, "too many keywords");

// TokName returns a printable name for a token (second part in TOKENS definition)
const char* TokName(Tok);


// ErrorHandler callback type
// msg is a preformatted error message and is only valid until this function returns
typedef void(ErrorHandler)(SrcPos pos, const Str msg, void* userdata);

// BuildCtx holds shared resources used to scan, parse and compile things
typedef struct BuildCtx {
  Mem nullable           mem;      // for allocations during parsing (e.g. ASTNode, Comment)
  SymPool*               syms;     // symbol pool
  Pkg*                   pkg;      // top-level package for which we are building
  ErrorHandler* nullable errh;     // error handler
  void* nullable         userdata; // custom user data passed to error handler
  Str                    tmpbuf;   // tmpbuf is a generic string buffer for temporary work
} BuildCtx;

void build_init(BuildCtx*,
  Mem nullable           mem,
  SymPool*               syms,
  Pkg*                   pkg,
  ErrorHandler* nullable errh,
  void* nullable         userdata);

void build_dispose(BuildCtx*);

// build_errf formats a message including source position and invokes ctx->errh
void build_errf(const BuildCtx* ctx, SrcPos, const char* format, ...);

#if R_UNIT_TEST_ENABLED
// test_build_new creates a new BuildCtx in a new isolated Mem space with new pkg and syms
BuildCtx* test_build_new();
void      test_build_free(BuildCtx*);
#endif

// ParseFlags are flags for parser and scanner
typedef enum {
  ParseFlagsDefault = 0,
  ParseComments     = 1 << 1, // parse comments, populating S.comments_{head,tail}
  ParseOpt          = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
  ParseIndent       = 1 << 3, // parse indentation, producing TIndent tokens
} ParseFlags;

// Comment is a scanned comment
typedef struct Comment {
  struct Comment* next; // next comment in linked list
  Source*         src;  // source
  const u8*       ptr;  // ptr into source
  size_t          len;  // byte length
} Comment;

// Scanner reads source code and produces tokens
typedef struct Scanner {
  BuildCtx*  ctx;          // memory to use for allocations
  Source*    src;          // input source
  ParseFlags flags;
  const u8*  inp;          // input buffer current pointer
  const u8*  inp0;         // input buffer previous pointer
  const u8*  inend;        // input buffer end
  bool       insertSemi;    // insert a semicolon before next newline

  Tok        tok;           // current token
  const u8*  tokstart;      // start of current token
  const u8*  tokend;        // end of current token
  Sym        name;          // Current name (valid for TId and keywords)

  Comment*   comments_head; // linked list head of comments scanned so far
  Comment*   comments_tail; // linked list tail of comments scanned so far

  u32        lineno;        // source position line
  const u8*  linestart;     // source position line start pointer (for column)
} Scanner;

// ScannerInit initializes a scanner. Returns false if SourceOpenBody fails.
bool ScannerInit(Scanner*, BuildCtx*, Source*, ParseFlags);

// ScannerDispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as ScannerInit calls SourceOpenBody.
void ScannerDispose(Scanner*);

// ScannerNext scans the next token
Tok ScannerNext(Scanner*);

// ScannerSrcPos returns the source position of s->tok (current token)
static SrcPos ScannerSrcPos(const Scanner* s);

// ScannerTokStr returns a token's string value and length, which is a pointer
// into the source's body.
static const u8* ScannerTokStr(const Scanner* s, size_t* len_out);

// ScannerCommentPop removes and returns the least recently scanned comment.
// The caller takes ownership of the comment and should free it using memfree(s->mem,comment).
Comment* nullable ScannerCommentPop(Scanner* s);


ASSUME_NONNULL_END
#include "universe.h"
#include "ast.h"
ASSUME_NONNULL_BEGIN


// Parser is the state used to parse
typedef struct Parser {
  Scanner   s;          // parser is based on a scanner
  BuildCtx* build;      // compilation context
  Scope*    scope;      // current scope
  u32       fnest;      // function nesting level (for error handling)
  u32       unresolved; // number of unresolved identifiers
} Parser;

// Parse parses a translation unit and returns an AST
Node* Parse(Parser*, BuildCtx*, Source*, ParseFlags, Scope* pkgscope);

// ResolveSym resolves unresolved symbols in an AST.
// For top-level AST, scope should be pkgscope.
Node* ResolveSym(BuildCtx*, ParseFlags, Node*, Scope*);

// ResolveType resolves unresolved types in an AST
void ResolveType(BuildCtx*, Node*);

// GetTypeID retrieves the TypeID for the type node n.
// This function may mutate n by computing and storing id to n.t.id.
// This function may add symbols to b->syms
Sym GetTypeID(BuildCtx* b, Node* n);

// TypeEquals returns true if x and y are equivalent types (i.e. identical).
// This function may call GetTypeID which may mutate b->syms, x and y.
bool TypeEquals(BuildCtx* b, Node* x, Node* y);

// TypeConv describes the effect of converting one type to another
typedef enum TypeConv {
  TypeConvLossless = 0,  // conversion is "perfect". e.g. int32 -> int64
  TypeConvLossy,         // conversion may be lossy. e.g. int32 -> float32
  TypeConvImpossible,    // conversion is not possible. e.g. (int,int) -> bool
} TypeConv;

// // TypeConversion returns the effect of converting fromType -> toType.
// // intsize is the size in bytes of the "int" and "uint" types. E.g. 4 for 32-bit.
// TypeConv CheckTypeConversion(Node* fromType, Node* toType, u32 intsize);


// convlit converts an expression to type t.
// If n is already of type t, n is simply returned.
// BuildCtx is used for error reporting.
// This function may call GetTypeID which may mutate b->syms and n.
Node* convlit(BuildCtx*, Node* n, Node* t, bool explicit);
// inline static Node* convlit(BuildCtx* ctx, Node* n, Node* t, bool explicit) {
//   return n; // FIXME
// }

// For explicit conversions, which allows a greater range of conversions.
static Node* ConvlitExplicit(BuildCtx*, Node* n, Node* t);

// For implicit conversions (e.g. operands)
static Node* ConvlitImplicit(BuildCtx*, Node* n, Node* t);


// ---------------------------------------------------------------------------------
// implementations

inline static const u8* ScannerTokStr(const Scanner* s, size_t* len_out) {
  *len_out = (size_t)(s->tokend - s->tokstart);
  return s->tokstart;
}

// ScannerSrcPos returns the source position of s->tok (current token)
inline static SrcPos ScannerSrcPos(const Scanner* s) {
  // assert(s->tokstart >= s->src->body);
  // assert(s->tokstart < (s->src->body + s->src->len));
  // assert(s->tokend >= s->tokstart);
  // assert(s->tokend <= (s->src->body + s->src->len));
  size_t offs = (size_t)(s->tokstart - s->src->body);
  size_t span = (size_t)(s->tokend - s->tokstart);
  return (SrcPos){ s->src, offs, span };
}

inline static Node* ConvlitExplicit(BuildCtx* ctx, Node* n, Node* t) {
  return convlit(ctx, n, t, /*explicit*/ true);
}
inline static Node* ConvlitImplicit(BuildCtx* ctx, Node* n, Node* t) {
  return convlit(ctx, n, t, /*explicit*/ false);
}

ASSUME_NONNULL_END
