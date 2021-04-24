#pragma once
#include "../source/source.h"
#include "sym.h"
#include "types.h"
#include "symmap.h"   // SymMap

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
  _( TIdent         , "identifier") \
  _( TIntLit        , "int")        \
  _( TFloatLit      , "float")      \
  _( TComment       , "comment")    \
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

// TokName returns a printable name for a token
const char* TokName(Tok);


// ErrorHandler callback type
typedef void(ErrorHandler)(const Source*, SrcPos, const Str msg, void* userdata);

// ParseFlags are flags for parser and scanner
typedef enum {
  ParseFlagsDefault = 0,
  ParseComments     = 1 << 1, // parse comments, populating S.comments
  ParseOpt          = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
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
  Mem        mem;
  Source*    src;          // input source
  SymPool*   syms;         // symbol pool
  const u8*  inp;          // input buffer current pointer
  const u8*  inp0;         // input buffer previous pointer
  const u8*  inend;        // input buffer end
  ParseFlags flags;

  Tok       tok;           // current token
  const u8* tokstart;      // start of current token
  const u8* tokend;        // end of current token
  Sym       name;          // Current name (valid for TIdent and keywords)

  bool      insertSemi;    // insert a semicolon before next newline

  Comment*  comments;      // linked list head of comments scanned so far
  Comment*  comments_tail; // linked list tail of comments scanned so far

  u32       lineno;     // source position line
  const u8* linestart;  // source position line start pointer (for column)

  ErrorHandler* errh;
  void*         userdata;
} Scanner;

// ScannerInit initializes a scanner. Returns false on failure.
bool ScannerInit(Scanner*, Mem nullable mem, Source*, ParseFlags, ErrorHandler*, void* userdata);

// ScannerNext scans the next token
Tok ScannerNext(Scanner*);

// ScannerSrcPos returns the source position of s->tok (current token)
SrcPos ScannerSrcPos(Scanner* s);


ASSUME_NONNULL_END

#include "universe.h"
#include "ast.h"
