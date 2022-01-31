// language syntax tokens
#pragma once
ASSUME_NONNULL_BEGIN

typedef u16 Tok;

// Tok definitions
#define DEF_TOKENS(_)   \
  _( TNone  , "TNone" ) \
  _( TComma , ",")      \
  _( TSemi  , ";")      \
  _( TColon , ":")      \
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
// end DEF_TOKENS
#define DEF_TOKENS_KEYWORD(_) \
  _( TAs,       as )          \
  _( TAuto,     auto )        \
  _( TBreak,    break )       \
  _( TContinue, continue )    \
  _( TDefer,    defer )       \
  _( TElse,     else )        \
  _( TEnum,     enum )        \
  _( TFor,      for )         \
  _( TFun,      fun )         \
  _( TIf,       if )          \
  _( TImport,   import )      \
  _( TIn,       in )          \
  _( TNil,      nil )         \
  _( TReturn,   return )      \
  _( TStruct,   struct )      \
  _( TSwitch,   switch )      \
  _( TType,     type )        \
  _( TConst,    const )       \
  _( TMut,      mut )         \
  _( TVar,      var )         \
// end DEF_TOKENS_KEYWORD
// Limited to a total of 31 keywords. See parse_scan.c

enum Tok {
  #define I_ENUM(name, _str) name,

  DEF_TOKENS(I_ENUM)

  // TKeywordsStart is used for 0-based keyword indexing.
  // Its explicit value is used by sym.c to avoid having to regenerate keyword symbols
  // whenever a non-keyword token is added. I.e. this number can be changed freely but will
  // require regeneration of the code in sym.c.
  TKeywordsStart = 0x100,
  DEF_TOKENS_KEYWORD(I_ENUM)
  TKeywordsEnd,
  Tok_MAX = TKeywordsEnd,

  #undef I_ENUM
} END_TYPED_ENUM(Tok)
// We only have 5 bits to encode tokens in Sym. Additionally, the value 0 is reserved
// for "not a keyword", leaving the max number of values at 31 (i.e. 2^5=32-1).
static_assert(TKeywordsEnd - TKeywordsStart < 32, "too many keywords");


// TokName returns a printable name for a token (second part in TOKENS definition)
const char* TokName(Tok);

// langtok returns the Tok representing this sym in the language syntax.
// Either returns a keyword token or TId if sym is not a keyword.
inline static Tok langtok(Sym s) {
  // Bits [4-8) represents offset into Tok enum when s is a language keyword.
  u8 kwindex = symflags(s);
  return kwindex == 0 ? TId : TKeywordsStart + kwindex;
}

ASSUME_NONNULL_END
