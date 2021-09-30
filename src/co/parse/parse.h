#pragma once
#include "../build.h"
#include "../types.h"
#include "../util/array.h"

ASSUME_NONNULL_BEGIN

typedef struct Node Node;
typedef Node Type;

// scanner tokens
#define TOKENS(_)  \
  _( TNone  , "TNone" ) \
  _( TComma , ",")      \
  _( TSemi  , ";")      \
  \
  _( T_PRIM_OPS_START , "") \
  /* primary "intrinsic" operator tokens, most of them mapping directly to IR ops */ \
  _( TPlus          , "+")  \
  _( TMinus         , "-")  \
  _( TStar          , "*")  \
  _( TSlash         , "/")  \
  _( TPercent       , "%")  \
  _( TShl           , "<<") \
  _( TShr           , ">>") \
  _( TAnd           , "&")  \
  _( TPipe          , "|")  \
  _( THat           , "^")  \
  _( TTilde         , "~")  \
  _( TExcalm        , "!")  \
  /* binary comparison ops (IR builder assume these are packed!) */ \
  _( TEq            , "==") /* must be first */ \
  _( TNEq           , "!=") \
  _( TLt            , "<")  \
  _( TLEq           , "<=") \
  _( TGt            , ">")  \
  _( TGEq           , ">=") /* must be last */ \
  /* unary ops */ \
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
  _( TDot           , ".")  \
  _( TId            , "identifier")  \
  _( TIntLit        , "int") \
  _( TFloatLit      , "float") \
/*END TOKENS*/
#define TOKEN_KEYWORDS(_) \
  _( as,          TAs)          \
  _( auto,        TAuto)        \
  _( break,       TBreak)       \
  _( continue,    TContinue)    \
  _( defer,       TDefer)       \
  _( else,        TElse)        \
  _( enum,        TEnum)        \
  _( for,         TFor)         \
  _( fun,         TFun)         \
  _( if,          TIf)          \
  _( import,      TImport)      \
  _( in,          TIn)          \
  _( nil,         TNil)         \
  _( return,      TReturn)      \
  _( struct,      TStruct)      \
  _( switch,      TSwitch)      \
  _( type,        TType)        \
  _( var,         TVar)         \
  _( const,       TConst)       \
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
static_assert(TKeywordsEnd - TKeywordsStart < 32, "too many keywords");

// TokName returns a printable name for a token (second part in TOKENS definition)
const char* TokName(Tok);

// ParseFlags are flags for parser and scanner
typedef enum {
  ParseFlagsDefault = 0,
  ParseComments     = 1 << 1, // parse comments, populating S.comments_{head,tail}
  ParseOpt          = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
} ParseFlags;

// Comment is a scanned comment
typedef struct Comment {
  struct Comment* next; // next comment in linked list
  Source*         src;  // source
  const u8*       ptr;  // ptr into source
  size_t          len;  // byte length
} Comment;

// Indent is a scanned comment
typedef struct Indent {
  bool isblock; // true if this indent is a block
  u32  n;       // number of whitespace chars
} Indent;

// Scanner reads source code and produces tokens
typedef struct Scanner {
  Build*     build;        // build context (memory allocator, sympool, pkg, etc.)
  Source*    src;          // input source
  u32        srcposorigin;
  ParseFlags flags;
  const u8*  inp;          // input buffer current pointer
  const u8*  inend;        // input buffer end
  bool       insertSemi;   // insert a semicolon before next newline

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
} Scanner;

// ScannerInit initializes a scanner. Returns false if SourceOpenBody fails.
bool ScannerInit(Scanner*, Build*, Source*, ParseFlags);

// ScannerDispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as ScannerInit calls SourceOpenBody.
void ScannerDispose(Scanner*);

// ScannerNext scans the next token
Tok ScannerNext(Scanner*);

// ScannerPos returns the source position of s->tok (current token)
static Pos ScannerPos(const Scanner* s);

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


// Parser holds state used during parsing
typedef struct Parser {
  Scanner s;          // parser is based on a scanner
  Build*  build;      // compilation context
  Scope*  pkgscope;   // package-level scope
  Node*   expr;       // most recently parsed expression
  u32     fnest;      // function nesting level

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
    uintptr_t cap;          // capacity of ptr (count, not bytes)
    uintptr_t len;          // current length (use) of ptr
    uintptr_t base;         // current scope's base index into ptr
    void**    ptr;          // entries
    void*     storage[256]; // initial storage in parser's memory
  } scopestack;
} Parser;

// CreatePkgAST creates a new AST node for a package
Node* CreatePkgAST(Build*, Scope* pkgscope);

// Parse parses a translation unit and returns an AST
// Returns NULL on error.
Node* nullable Parse(Parser*, Build*, Source*, ParseFlags, Scope* pkgscope);

// ResolveSym resolves unresolved symbols in an AST. May return a new version of n.
// For top-level AST, scope should be pkgscope.
Node* ResolveSym(Build*, ParseFlags, Node*, Scope*);

// ResolveType resolves unresolved types in an AST. May return a new version of n.
Node* ResolveType(Build* b, Node* n);

// GetTypeID retrieves the TypeID for the type node n.
// This function may mutate n by computing and storing id to n.t.id.
// This function may add symbols to b->syms
Sym GetTypeID(Build* b, Type* n);

// InternASTType uses GetTypeID(t) to intern the type.
// It returns t if newfound, or an existing type node equivalent to t.
// The returned type is valid until the next call to build_dispose(b).
Type* InternASTType(Build* b, Type* t);

// TypeEquals returns true if x and y are equivalent types (i.e. identical).
// This function may call GetTypeID which may update b->syms, may mutate x and y.
static bool TypeEquals(Build* b, Type* x, Type* y);

// // TypeConv describes the effect of converting one type to another
// typedef enum TypeConv {
//   TypeConvLossless = 0,  // conversion is "perfect". e.g. int32 -> int64
//   TypeConvLossy,         // conversion may be lossy. e.g. int32 -> float32
//   TypeConvImpossible,    // conversion is not possible. e.g. (int,int) -> bool
// } TypeConv;

// // TypeConversion returns the effect of converting fromType -> toType.
// // intsize is the size in bytes of the "int" and "uint" types. E.g. 4 for 32-bit.
// TypeConv CheckTypeConversion(Node* fromType, Node* toType, u32 intsize);

typedef enum ConvlitFlags {
  ConvlitImplicit    = 0,
  ConvlitExplicit    = 1 << 0, // explicit conversion; allows for a greater range of conversions
  ConvlitRelaxedType = 1 << 1, // if a node is already typed, do nothing and return it.
} ConvlitFlags;

// convlit converts an expression to type t.
// If n is already of type t, n is simply returned.
// n is assumed to have no unresolved refs (expected to have gone through resolve_sym)
// Build is used for error reporting.
// This function may call GetTypeID which may update b->syms and mutate n.
Node* convlit(Build*, Node* n, Type* t, ConvlitFlags fl);

// NodeEval attempts to evaluate expr. Returns NULL on failure or the resulting value on success.
// If targetType is provided, the result is implicitly converted to that type.
// In that case it is an error if the result can't be converted to targetType.
Node* nullable NodeEval(Build* b, Node* expr, Type* nullable targetType);


// ---------------------------------------------------------------------------------
// implementations

inline static const u8* ScannerTokStr(const Scanner* s, size_t* len_out) {
  *len_out = (size_t)(s->tokend - s->tokstart);
  return s->tokstart;
}

inline static Pos ScannerPos(const Scanner* s) {
  // assert(s->tokend >= s->tokstart);
  u32 col = 1 + (u32)((uintptr_t)s->tokstart - (uintptr_t)s->linestart);
  u32 span = s->tokend - s->tokstart;
  return pos_make(s->srcposorigin, s->lineno, col, span);
}

bool _TypeEquals(Build* b, Type* x, Type* y); // impl typeid.c

inline static bool TypeEquals(Build* b, Type* x, Type* y) {
  return x == y || _TypeEquals(b, x, y);
}

ASSUME_NONNULL_END
