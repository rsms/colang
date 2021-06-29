#pragma once
ASSUME_NONNULL_BEGIN

typedef struct SExpr SExpr;
struct SExpr {
  SExpr* nullable next;
  enum { SExprList, SExprAtom } type;
  union {
    struct {
      u8              kind; // '(' or '[' or '{'
      SExpr* nullable head;
    } list;
    struct {
      const char* name;
      u32         namelen;
    } atom;
  };
};

typedef enum {
  SExprFmtDefault = 0,          // separate values with spaces
  SExprFmtPretty  = 1 << 0, // separate values with linebreaks and indentation
} SExprFmtFlags;

SExpr* sexpr_parse(const u8* src, u32 srclen, Mem mem);
void sexpr_free(SExpr* n, Mem mem); // mem must be same as used with sexpr_parse
Str sexpr_fmt(const SExpr* n, Str s, SExprFmtFlags);
Str sexpr_prettyprint(Str dst, const char* src, u32 srclen); // returns dst with fmt appended

ASSUME_NONNULL_END
