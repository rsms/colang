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


// appends a short representation of an AST node to s, suitable for use in error messages.
Str _NodeStr(const Node* nullable n, Str s) {
  // Note: Do not include type information.
  // Instead, in use sites, call fmtnode individually for n->type when needed.

  if (n == NULL)
    return str_appendcstr(s, "<null>");

  switch ((enum NodeKind)n->kind) {

  // uses no extra data
  case NNil: // nil
    return str_appendcstr(s, "nil");

  case NBoolLit: // true | false
    return str_appendcstr(s, ((BoolLitNode*)n)->ival ? "true" : "false");

  case NIntLit: // 123
    return str_appendu64(s, ((IntLitNode*)n)->ival, 10);

  case NFloatLit: // 12.3
    return str_appendf64(s, ((FloatLitNode*)n)->fval, -1);

  case NId: // foo
    return str_appendn(s, ((IdNode*)n)->name, symlen(((IdNode*)n)->name));

  case NPkg:
    s = str_appendcstr(s, "package \"");
    s = str_appendn(s, ((PkgNode*)n)->name, strlen(((PkgNode*)n)->name));
    return str_appendc(s, '"');

  case NFile:
    s = str_appendcstr(s, "file \"");
    s = str_appendn(s, ((FileNode*)n)->name, strlen(((FileNode*)n)->name));
    return str_appendc(s, '"');


  // case NBinOp: // foo + bar
  //   s = NodeStr(s, n->op.left);
  //   s = str_appendc(s, ' ');
  //   s = str_appendcstr(s, TokName(n->op.op));
  //   s = str_appendc(s, ' ');
  //   return NodeStr(s, n->op.right);

  // case NPostfixOp: // foo++
  //   s = NodeStr(s, n->op.left);
  //   return str_appendcstr(s, TokName(n->op.op));

  // case NPrefixOp: // -foo
  //   s = str_appendcstr(s, TokName(n->op.op));
  //   return NodeStr(s, n->op.left); // note: prefix op uses left, not right.

  // case NAssign: // thing=
  //   s = NodeStr(s, n->op.left);
  //   return str_appendc(s, '=');

  // case NReturn: // return thing
  //   s = str_appendcstr(s, "return ");
  //   return NodeStr(s, n->op.left);

  // case NBlock: // {int}
  //   return str_appendcstr(s, "block");

  // case NArray: // [one two 3]
  //   s = str_appendc(s, '[');
  //   s = str_append_NodeArray(s, &n->array.a, " ", 1);
  //   return str_appendc(s, ']');

  // case NTuple: // (one two 3)
  //   s = str_appendc(s, '(');
  //   s = str_append_NodeArray(s, &n->array.a, " ", 1);
  //   return str_appendc(s, ')');

  // case NVar: // var x | param x
  //   return str_appendfmt(s, "%s %s",
  //     (n->var.isconst ? "const" :
  //      NodeIsParam(n) ? "param" :
  //                       "var"),
  //     n->var.name);

  // case NRef: // &x, mut&x
  //   if (NodeIsConst(n)) {
  //     s = str_appendc(s, '&');
  //   } else {
  //     s = str_appendcstr(s, "mut&");
  //   }
  //   return NodeStr(s, n->ref.target);

  // case NFun: // fun foo
  //   s = str_appendcstr(s, "function");
  //   if (n->fun.name) {
  //     s = str_appendc(s, ' ');
  //     s = str_appendcstr(s, n->fun.name);
  //   }
  //   return s;

  // case NMacro: // macro foo
  //   s = str_appendcstr(s, "macro");
  //   if (n->macro.name) {
  //     s = str_appendc(s, ' ');
  //     s = str_appendcstr(s, n->macro.name);
  //   }
  //   return s;

  // case NTypeCast: // typecast<int16>
  //   s = str_appendcstr(s, "typecast<");
  //   s = NodeStr(s, n->call.receiver);
  //   return str_appendc(s, '>');

  // case NCall: // call foo
  //   s = str_appendcstr(s, "call ");
  //   return NodeStr(s, n->call.receiver);

  // case NIf: // if
  //   return str_appendcstr(s, "if");

  // case NSelector: // expr.name | expr.selector
  //   s = NodeStr(s, n->sel.operand);
  //   s = str_appendc(s, '.');
  //   return str_append(s, n->sel.member, symlen(n->sel.member));

  // case NIndex: // [index]
  //   s = str_appendc(s, '[');
  //   s = NodeStr(s, n->index.indexexpr);
  //   return str_appendc(s, ']');

  // case NSlice: // [start?:end?]
  //   // s = NodeStr(s, n->slice.operand);
  //   s = str_appendc(s, '[');
  //   if (n->slice.start)
  //     s = NodeStr(s, n->slice.start);
  //   s = str_appendc(s, ':');
  //   if (n->slice.end)
  //     s = NodeStr(s, n->slice.end);
  //   return str_appendc(s, ']');

  // case NBasicType: // int
  //   if (n == Type_ideal)
  //     return str_appendcstr(s, "ideal");
  //   return str_append(s, n->t.basic.name, symlen(n->t.basic.name));

  // case NRefType: // &T, mut&T
  //   if (NodeIsConst(n)) {
  //     s = str_appendc(s, '&');
  //   } else {
  //     s = str_appendcstr(s, "mut&");
  //   }
  //   return NodeStr(s, n->t.ref);

  // case NField:
  //   s = str_appendcstr(s, "field ");
  //   s = str_append(s, n->field.name, symlen(n->field.name));
  //   s = str_appendc(s, ' ');
  //   return NodeStr(s, n->type);

  // case NNamedVal:
  //   s = str_append(s, n->namedval.name, symlen(n->namedval.name));
  //   s = str_appendc(s, '=');
  //   return NodeStr(s, n->namedval.value);

  // case NFunType: // (int int)->bool
  //   if (n->t.fun.params == NULL) {
  //     s = str_appendcstr(s, "()");
  //   } else {
  //     // TODO: include names
  //     s = NodeStr(s, n->t.fun.params->type);
  //   }
  //   s = str_appendcstr(s, "->");
  //   return NodeStr(s, n->t.fun.result); // ok if NULL

  // case NTupleType: // (int bool Foo)
  //   s = str_appendc(s, '(');
  //   s = str_append_NodeArray(s, &n->t.tuple.a, " ", 1);
  //   return str_appendc(s, ')');

  // case NArrayType: // [int 4], [int]
  //   s = str_appendc(s, '[');
  //   s = NodeStr(s, n->t.array.subtype);
  //   if (n->t.array.size > 0) {
  //     s = str_appendc(s, ' ');
  //     s = str_appendu64(s, n->t.array.size, 10);
  //   }
  //   return str_appendc(s, ']');

  // case NStructType: { // "struct Name" or "struct {foo float; y bool}"
  //   s = str_appendcstr(s, "struct ");
  //   if (n->t.struc.name)
  //     return str_append(s, n->t.struc.name, symlen(n->t.struc.name));
  //   s = str_appendc(s, '{');
  //   bool isFirst = true;
  //   for (u32 i = 0; i < n->t.struc.a.len; i++) {
  //     Node* cn = n->t.struc.a.v[i];
  //     if (isFirst) {
  //       isFirst = false;
  //     } else {
  //       s = str_appendcstr(s, "; ");
  //     }
  //     if (cn->kind == NField) {
  //       s = str_append(s, cn->field.name, symlen(cn->field.name));
  //       s = str_appendc(s, ' ');
  //       s = NodeStr(s, cn->type);
  //     } else {
  //       s = NodeStr(s, cn);
  //     }
  //   }
  //   return str_appendc(s, '}');
  // }

  // case NTypeType: // "type ..."
  //   s = str_appendcstr(s, "type ");
  //   return NodeStr(s, n->t.type);

  // // The remaining types are not expected to appear. Use their kind if they do.
  // case NBad:
  // case NNone:
  //   return str_appendcstr(s, NodeKindName(n->kind));

  // // TODO
  // case NStrLit:
  //   panic("TODO %s", NodeKindName(n->kind));

  // case _NodeKindMax:
  //   break;

  default:
    panic("TODO %s", NodeKindName(n->kind));
  }

  return str_appendcstr(s, "INVALID");
}



// ---------------------


const char* _fmtnode(const Node* n) {
  Str* sp = str_tmp();
  *sp = _NodeStr(n, *sp);
  return (*sp)->p;
}

const char* _fmtast(const Node* n) {
  Str* sp = str_tmp();
  // *sp = _NodeRepr(n, *sp, 0); // TODO
  *sp = _NodeStr(n, *sp); // XXX TMP
  return (*sp)->p;
}


Str _NodeRepr(const Node* nullable n, Str s, NodeReprFlags fl) {
  s = str_append(s, "[TODO _NodeRepr]");
  return s;
}
