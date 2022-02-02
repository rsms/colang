#include "../coimpl.h"
#include "../tstyle.h"
#include "../array.h"
#include "../str.h"
#include "../sbuf.h"
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


static Str NodeStrArray(Str s, const NodeArray* na) {
  for (u32 i = 0; i < na->len; i++) {
    if (i)
      s = str_appendc(s, ' ');
    s = _NodeStr(s, na->v[i]);
  }
  return s;
}

// appends a short representation of an AST node to s, suitable for use in error messages.
Str _NodeStr(Str s, const Node* nullable n) {
  // Note: Do not include type information.
  // Instead, in use sites, call fmtnode individually for n->type when needed.

  #define NODE(s,n)      _NodeStr(s, (Node*)n)
  #define NODEARRAY(s,a) NodeStrArray(s, as_NodeArray(a))
  #define SPACE(s)       str_appendc(s, ' ')
  #define STR(s,cstr)    str_appendcstr(s, cstr)
  #define SYM(s,sym)     str_appendn(s, sym, symlen(sym))

  if (n == NULL)
    return STR(s, "<null>");

  switch ((enum NodeKind)n->kind) {

  case NBad: // nil
    return STR(s, "bad");
  case NPkg: // package "foo"
    s = STR(STR(s, "package \""), ((PkgNode*)n)->name);
    return str_appendc(s, '"');
  case NFile: // file "foo"
    s = STR(STR(s, "file \""), ((FileNode*)n)->name);
    return str_appendc(s, '"');
  case NField: // field foo T
    s = SYM(STR(s, "field"), ((FieldNode*)n)->name);
    if (((FieldNode*)n)->type)
      NodeStr(SPACE(s), ((FieldNode*)n)->type);
    return s;

  case NNil: // nil
    return STR(s, "nil");
  case NBoolLit: // true | false
    return STR(s, ((BoolLitNode*)n)->ival ? "true" : "false");
  case NIntLit: // 123
    return str_appendu64(s, ((IntLitNode*)n)->ival, 10);
  case NFloatLit: // 12.3
    return str_appendf64(s, ((FloatLitNode*)n)->fval, -1);
  case NStrLit: // "lolcat"
    s = str_appendc(s, '"');
    s = str_appendrepr(s, ((StrLitNode*)n)->sp, ((StrLitNode*)n)->len);
    return str_appendc(s, '"');
  case NId: // foo
    return SYM(s, ((IdNode*)n)->name);
  case NBinOp: // foo + bar
    s = NODE(s, ((BinOpNode*)n)->left);
    s = STR(SPACE(s), TokName(((BinOpNode*)n)->op));
    return NODE(SPACE(s), ((BinOpNode*)n)->right);
  case NPostfixOp: // foo++
    s = NODE(s, ((PostfixOpNode*)n)->expr);
    return STR(s, TokName(((PostfixOpNode*)n)->op));
  case NPrefixOp: // -foo
    s = STR(s, TokName(((PrefixOpNode*)n)->op));
    return NODE(s, ((PrefixOpNode*)n)->expr);
  case NAssign: // foo=
    s = NODE(s, ((AssignNode*)n)->dst);
    return str_appendc(s, '=');
  case NNamedArg: // name=value
    s = SYM(s, ((NamedArgNode*)n)->name);
    s = str_appendc(s, '=');
    return NodeStr(s, ((NamedArgNode*)n)->value);
  case NReturn: // return foo
    s = STR(s, "return ");
    return NODE(s, ((ReturnNode*)n)->expr);
  case NBlock: // block
    return STR(s, "block");
  case NArray: // array [one two 3]
    s = NODEARRAY(STR(s, "array ["), &((ArrayNode*)n)->a);
    return str_appendc(s, ']');
  case NTuple: // tuple (one two 3)
    s = NODEARRAY(STR(s, "tuple ("), &((TupleNode*)n)->a);
    return str_appendc(s, ')');
  case NVar: // var x
    s = STR(s, ((VarNode*)n)->isconst ? "const" : NodeIsParam(n) ? "param" : "var");
    return SYM(SPACE(s), ((VarNode*)n)->name);
  case NRef: // &x, mut&x
    s = STR(s, NodeIsConst(n) ? "&" : "mut&");
    return NODE(s, ((RefNode*)n)->target);
  case NFun: // function foo
    s = STR(s, "function");
    if (((FunNode*)n)->name)
      s = SYM(SPACE(s), ((FunNode*)n)->name);
    return s;
  case NMacro: // macro foo
    s = STR(s, "macro");
    if (((MacroNode*)n)->name)
      s = SYM(SPACE(s), ((MacroNode*)n)->name);
    return s;
  case NTypeCast: // typecast<int16>
    s = NODE(STR(s, "typecast<"), ((TypeCastNode*)n)->expr);
    return str_appendc(s, '>');
  case NCall: // call foo
    return NODE(STR(s, "call "), ((CallNode*)n)->receiver);
  case NIf: // if
    return STR(s, "if");
  case NSelector: // expr.name | expr.selector
    s = str_appendc(NODE(s, ((SelectorNode*)n)->operand), '.');
    return SYM(s, ((SelectorNode*)n)->member);
  case NIndex: // foo[index]
    s = NODE(s, ((IndexNode*)n)->operand);
    s = str_appendc(s, '[');
    s = NODE(s, ((IndexNode*)n)->indexexpr);
    return str_appendc(s, ']');
  case NSlice: { // [start?:end?]
    auto slice = (SliceNode*)n;
    s = NODE(s, slice->operand);
    s = str_appendc(s, '[');
    if (slice->start)
      s = NODE(s, slice->start);
    s = str_appendc(s, ':');
    if (slice->end)
      s = NODE(s, slice->end);
    return str_appendc(s, ']');
  }

  case NBasicType: // int
    return SYM(s, ((BasicTypeNode*)n)->name);
  case NRefType: // &T, mut&T
    s = STR(s, NodeIsConst(n) ? "&" : "mut&");
    return NODE(s, ((RefTypeNode*)n)->elem);
  case NTypeType: // type
    return STR(s, "type");
  case NNamedType: // foo
    return SYM(s, ((NamedTypeNode*)n)->name);
  case NAliasType: // foo (alias of bar)
    s = SYM(s, ((AliasTypeNode*)n)->name);
    s = NODE(STR(s, " (alias of "), ((AliasTypeNode*)n)->type);
    return str_appendc(s, ')');
  case NFunType: // (int int)->bool
    if (((FunTypeNode*)n)->params == NULL) {
      s = STR(s, "()");
    } else {
      // TODO: include names?
      s = NODE(s, ((FunTypeNode*)n)->params->type);
    }
    return NODE(STR(s, "->"), ((FunTypeNode*)n)->result); // ok if NULL
  case NTupleType: // (int bool Foo)
    s = str_appendc(s, '(');
    s = NODEARRAY(s, &((TupleTypeNode*)n)->a);
    return str_appendc(s, ')');
  case NArrayType: // [int 4]
    s = str_appendc(s, '[');
    s = NODE(s, ((ArrayTypeNode*)n)->elem);
    if (((ArrayTypeNode*)n)->size > 0)
      s = str_appendu64(SPACE(s), ((ArrayTypeNode*)n)->size, 10);
    return str_appendc(s, ']');
  case NStructType: { // "struct Name" or "struct {foo float; y bool}"
    auto st = (StructTypeNode*)n;
    s = STR(s, "struct ");
    if (st->name)
      return SYM(s, st->name);
    s = str_appendc(s, '{');
    for (u32 i = 0; i < st->fields.len; i++) {
      auto field = st->fields.v[i];
      if (i)
        s = STR(s, "; ");
      s = SYM(s, field->name);
      if (field->type)
        NodeStr(SPACE(s), field->type);
    }
    return str_appendc(s, '}');
  }

  case NComment:
    assertf(0, "unexpected node %s", nodename(n));
    break;
  }

  #undef STR
  #undef SYM
  #undef SPACE
  #undef NODEARRAY
  #undef NODE

  return str_appendcstr(s, "INVALID");
}



// ---------------------

typedef struct { ASTVisitor; SBuf buf; } ReprVisitor;

static void _visit(ASTVisitor* v, const Node* nullable n) {
  SBuf* buf = &((ReprVisitor*)v)->buf;
  sbuf_appendc(buf, '(');
  if (n == NULL) {
    sbuf_appendstr(buf, "NULL)");
    return;
  }
  sbuf_appendstr(buf, NodeKindName(n->kind));
  sbuf_appendc(buf, ' ');
  char* p = buf->p;
  ASTVisit(v, n);
  if (buf->p == p) {
    // nothing printed after '(name', so replace ' ' with ')'
    *(p-1) = ')';
  } else {
    sbuf_appendc(buf, ')');
  }
}

static int visit_file(ASTVisitor* v, const FileNode* file) {
  SBuf* buf = &((ReprVisitor*)v)->buf;
  sbuf_appendc(buf, '"');
  sbuf_appendrepr(buf, file->name, strlen(file->name));
  sbuf_appendc(buf, '"');
  return 0;
}


Str nullable _NodeRepr(Str s, const Node* nullable n, NodeReprFlags fl) {
  Str s2 = str_makeroom(s, 512);
  if (s2)
    s = s2;

  static const ASTVisitorFuns vfn = { .File = visit_file };
  ReprVisitor v = {0};
  sbuf_init(&v.buf, s->p, s->cap);
  ASTVisitorInit((ASTVisitor*)&v, &vfn);
  _visit((ASTVisitor*)&v, n);

  sbuf_terminate(&v.buf);
  s->len = MIN(s->cap, v.buf.len);

  // TODO: if s is too small, grow it and retry

  return s;
}

// ---------------------


const char* _fmtnode(const Node* n) {
  Str* sp = str_tmp();
  *sp = _NodeStr(*sp, n);
  return (*sp)->p;
}

const char* _fmtast(const Node* n) {
  Str* sp = str_tmp();
  *sp = _NodeRepr(*sp, n, 0);
  return (*sp)->p;
}



