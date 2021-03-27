#include <rbase/rbase.h>
#include "token.h"

const char* TokName(Tok t) {
  switch (t) {
    #define I_ENUM(name, str) case name: return str;
    TOKENS(I_ENUM)
    #undef I_ENUM

    case TKeywordsStart: return "TKeywordsStart";

    #define I_ENUM(str, name) case name: return "keyword " #str;
    TOKEN_KEYWORDS(I_ENUM)
    #undef I_ENUM

    case TKeywordsEnd: return "TKeywordsEnd";

    case TMax: return "TMax";
  }
}
