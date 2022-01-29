#include "../coimpl.h"
#include "../tstyle.h"
#include "../array.h"
#include "../str.h"
#include "ast.h"

// DEBUG_INCLUDE_POINTERS: define to include node memory addresses in output
//#define DEBUG_INCLUDE_POINTERS

// INDENT_DEPTH is the number of spaces used for indentation
#define INDENT_DEPTH 2

// static TStyle id_color      = TStyle_lightyellow;
// static TStyle type_color    = TStyle_blue;
// static TStyle typeval_color = TStyle_lightblue; // type used as a value
// static TStyle field_color   = TStyle_pink;
// static TStyle ref_color     = TStyle_red;
// static TStyle attr_color    = TStyle_orange;
// static TStyle lit_color     = TStyle_lightpurple;
// static TStyle op_color      = TStyle_lightgreen;


const char* _fmtnode(const Node* n) {
  Str* sp = str_tmp();
  *sp = _NodeStr(n, *sp);
  return (*sp)->p;
}

const char* _fmtast(const Node* n) {
  Str* sp = str_tmp();
  *sp = _NodeRepr(n, *sp, 0);
  return (*sp)->p;
}


Str _NodeRepr(const Node* nullable n, Str s, NodeReprFlags fl) {
  s = str_append(s, "[TODO _NodeRepr]");
  return s;
}

Str _NodeStr(const Node* nullable n, Str s) {
  s = str_append(s, "[TODO _NodeStr]");
  return s;
}
