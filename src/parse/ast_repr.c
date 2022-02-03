#include "../coimpl.h"
#include "../tstyle.h"
#include "../array.h"
#include "../str.h"
#include "../sbuf.h"
#include "../unicode.h"
#include "../test.h"
#include "ast.h"
#include "universe.h"

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

typedef struct Repr Repr;

struct Repr {
  ASTVisitor;

  SBuf  buf;
  int   indent;
  usize lnstart; // relative to buf.len
  usize wrapcol;
  usize stylelen; // buf.len-stylelen = nbytes written to buf excluding ANSI codes

  TStyles     styles;
  TStyleStack stylestack;
  const char* lparen;
  const char* rparen;
};

#define STYLE_STR  TS_LIGHTGREEN
#define STYLE_OP   TS_LIGHTORANGE

// -- repr output writers

static void write_push_style(Repr* r, TStyle style) {
  if (TStylesIsNone(r->styles))
    return;
  const char* s = tstyle_str(r->styles, tstyle_push(&r->stylestack, style));
  usize len = strlen(s);
  r->stylelen += len;
  sbuf_append(&r->buf, s, len);
}

static void write_pop_style(Repr* r) {
  if (TStylesIsNone(r->styles))
    return;
  const char* s = tstyle_str(r->styles, tstyle_pop(&r->stylestack));
  usize len = strlen(s);
  r->stylelen += len;
  sbuf_append(&r->buf, s, len);
}

static void write_paren_start(Repr* r) {
  sbuf_appendstr(&r->buf, r->lparen);
  if (r->lparen[1])
    r->stylelen += strlen(r->lparen) - 1;
}

static void write_paren_end(Repr* r) {
  sbuf_appendstr(&r->buf, r->rparen);
  if (r->rparen[1])
    r->stylelen += strlen(r->rparen) - 1;
}

static usize printable_len(Repr* r) {
  // nbytes written to buf excluding ANSI codes
  return r->buf.len - r->stylelen;
}

#define write_node(r,n) _write_node((r),as_Node(n))
static void _write_node(Repr* r, const Node* nullable n) {
  SBuf* buf = &r->buf;
  if (buf->len && !sbuf_endswith(buf, r->lparen, strlen(r->lparen))) // "((" vs "( ("
    sbuf_appendc(buf, ' ');
  write_paren_start(r);
  if (n != NULL) {
    sbuf_appendstr(buf, NodeKindName(n->kind));
    ASTVisit((ASTVisitor*)r, n);
  }
  write_paren_end(r);
  if (printable_len(r) - r->lnstart > r->wrapcol) {
    sbuf_appendc(buf, '\n');
    r->lnstart = printable_len(r);
  }
}

static void write_array(Repr* r, const NodeArray* a) {
  SBuf* buf = &r->buf;
  sbuf_appendc(buf, ' ');
  write_paren_start(r);
  for (u32 i = 0; i < a->len; i++)
    write_node(r, a->v[i]);
  write_paren_end(r);
}

static void write_str(Repr* r, const char* s) {
  sbuf_appendc(&r->buf, ' ');
  sbuf_appendstr(&r->buf, s);
}

static void write_qstr(Repr* r, const char* s, usize len) {
  SBuf* buf = &r->buf;
  write_push_style(r, STYLE_STR);
  sbuf_appendstr(buf, " \"");
  sbuf_appendrepr(buf, s, len);
  sbuf_appendc(buf, '"');
  write_pop_style(r);
}

static void write_qsym(Repr* r, Sym s) {
  write_qstr(r, s, symlen(s));
}

// -- visitor functions

static void visit_Field(Repr* r, const FieldNode* n) {}
static void visit_Pkg(Repr* r, const PkgNode* n) {}

static void visit_File(Repr* r, const FileNode* file) {
  write_qstr(r, file->name, strlen(file->name));
  if (file->a.len > 0)
    write_array(r, as_NodeArray(&file->a));
}

static void visit_Comment(Repr* r, const CommentNode* n) {}
static void visit_Nil(Repr* r, const NilNode* n) {}
static void visit_BoolLit(Repr* r, const BoolLitNode* n) {}
static void visit_IntLit(Repr* r, const IntLitNode* n) {}
static void visit_FloatLit(Repr* r, const FloatLitNode* n) {}
static void visit_StrLit(Repr* r, const StrLitNode* n) {}
static void visit_Id(Repr* r, const IdNode* n) {}

static void visit_BinOp(Repr* r, const BinOpNode* n) {
  write_push_style(r, STYLE_OP);
  write_str(r, TokName(n->op));
  write_pop_style(r);
  write_node(r, n->left);
  write_node(r, n->right);
}

static void visit_PrefixOp(Repr* r, const PrefixOpNode* n) {}
static void visit_PostfixOp(Repr* r, const PostfixOpNode* n) {}
static void visit_Return(Repr* r, const ReturnNode* n) {}
static void visit_Assign(Repr* r, const AssignNode* n) {}
static void visit_Tuple(Repr* r, const TupleNode* n) {}
static void visit_Array(Repr* r, const ArrayNode* n) {}
static void visit_Block(Repr* r, const BlockNode* n) {}

static void visit_Fun(Repr* r, const FunNode* n) {
  write_qsym(r, n->name ? n->name : kSym__);
  write_node(r, n->params);
  write_node(r, n->result);
  write_node(r, n->body);
}

static void visit_Macro(Repr* r, const MacroNode* n) {}
static void visit_Call(Repr* r, const CallNode* n) {}
static void visit_TypeCast(Repr* r, const TypeCastNode* n) {}
static void visit_Var(Repr* r, const VarNode* n) {}
static void visit_Ref(Repr* r, const RefNode* n) {}
static void visit_NamedArg(Repr* r, const NamedArgNode* n) {}
static void visit_Selector(Repr* r, const SelectorNode* n) {}
static void visit_Index(Repr* r, const IndexNode* n) {}
static void visit_Slice(Repr* r, const SliceNode* n) {}
static void visit_If(Repr* r, const IfNode* n) {}
static void visit_TypeType(Repr* r, const TypeTypeNode* n) {}

static void visit_NamedType(Repr* r, const NamedTypeNode* n) {
  write_qsym(r, n->name);
}

static void visit_AliasType(Repr* r, const AliasTypeNode* n) {}
static void visit_RefType(Repr* r, const RefTypeNode* n) {}
static void visit_BasicType(Repr* r, const BasicTypeNode* n) {}
static void visit_ArrayType(Repr* r, const ArrayTypeNode* n) {}
static void visit_TupleType(Repr* r, const TupleTypeNode* n) {}
static void visit_StructType(Repr* r, const StructTypeNode* n) {}
static void visit_FunType(Repr* r, const FunTypeNode* n) {}


// -- NodeRepr entry

Str nullable _NodeRepr(Str s, const Node* nullable n, NodeReprFlags fl) {
  Str s2 = str_makeroom(s, 512);
  if (s2)
    s = s2;

  Repr r = {
    .wrapcol = 30,
  };
  sbuf_init(&r.buf, s->p, s->cap);
  r.styles = TStylesForStderr();
  if (TStylesIsNone(r.styles)) {
    r.lparen = "(";
    r.rparen = ")";
  } else {
    r.lparen = "\x1b[2m(\x1b[22m";
    r.rparen = "\x1b[2m)\x1b[22m";
  }

  static const ASTVisitorFuns vfn = {
    .Field = (error(*nullable)(ASTVisitor*,const FieldNode*))visit_Field,
    .Pkg = (error(*nullable)(ASTVisitor*,const PkgNode*))visit_Pkg,
    .File = (error(*nullable)(ASTVisitor*,const FileNode*))visit_File,
    .Comment = (error(*nullable)(ASTVisitor*,const CommentNode*))visit_Comment,
    .Nil = (error(*nullable)(ASTVisitor*,const NilNode*))visit_Nil,
    .BoolLit = (error(*nullable)(ASTVisitor*,const BoolLitNode*))visit_BoolLit,
    .IntLit = (error(*nullable)(ASTVisitor*,const IntLitNode*))visit_IntLit,
    .FloatLit = (error(*nullable)(ASTVisitor*,const FloatLitNode*))visit_FloatLit,
    .StrLit = (error(*nullable)(ASTVisitor*,const StrLitNode*))visit_StrLit,
    .Id = (error(*nullable)(ASTVisitor*,const IdNode*))visit_Id,
    .BinOp = (error(*nullable)(ASTVisitor*,const BinOpNode*))visit_BinOp,
    .PrefixOp = (error(*nullable)(ASTVisitor*,const PrefixOpNode*))visit_PrefixOp,
    .PostfixOp = (error(*nullable)(ASTVisitor*,const PostfixOpNode*))visit_PostfixOp,
    .Return = (error(*nullable)(ASTVisitor*,const ReturnNode*))visit_Return,
    .Assign = (error(*nullable)(ASTVisitor*,const AssignNode*))visit_Assign,
    .Tuple = (error(*nullable)(ASTVisitor*,const TupleNode*))visit_Tuple,
    .Array = (error(*nullable)(ASTVisitor*,const ArrayNode*))visit_Array,
    .Block = (error(*nullable)(ASTVisitor*,const BlockNode*))visit_Block,
    .Fun = (error(*nullable)(ASTVisitor*,const FunNode*))visit_Fun,
    .Macro = (error(*nullable)(ASTVisitor*,const MacroNode*))visit_Macro,
    .Call = (error(*nullable)(ASTVisitor*,const CallNode*))visit_Call,
    .TypeCast = (error(*nullable)(ASTVisitor*,const TypeCastNode*))visit_TypeCast,
    .Var = (error(*nullable)(ASTVisitor*,const VarNode*))visit_Var,
    .Ref = (error(*nullable)(ASTVisitor*,const RefNode*))visit_Ref,
    .NamedArg = (error(*nullable)(ASTVisitor*,const NamedArgNode*))visit_NamedArg,
    .Selector = (error(*nullable)(ASTVisitor*,const SelectorNode*))visit_Selector,
    .Index = (error(*nullable)(ASTVisitor*,const IndexNode*))visit_Index,
    .Slice = (error(*nullable)(ASTVisitor*,const SliceNode*))visit_Slice,
    .If = (error(*nullable)(ASTVisitor*,const IfNode*))visit_If,
    .TypeType = (error(*nullable)(ASTVisitor*,const TypeTypeNode*))visit_TypeType,
    .NamedType = (error(*nullable)(ASTVisitor*,const NamedTypeNode*))visit_NamedType,
    .AliasType = (error(*nullable)(ASTVisitor*,const AliasTypeNode*))visit_AliasType,
    .RefType = (error(*nullable)(ASTVisitor*,const RefTypeNode*))visit_RefType,
    .BasicType = (error(*nullable)(ASTVisitor*,const BasicTypeNode*))visit_BasicType,
    .ArrayType = (error(*nullable)(ASTVisitor*,const ArrayTypeNode*))visit_ArrayType,
    .TupleType = (error(*nullable)(ASTVisitor*,const TupleTypeNode*))visit_TupleType,
    .StructType = (error(*nullable)(ASTVisitor*,const StructTypeNode*))visit_StructType,
    .FunType = (error(*nullable)(ASTVisitor*,const FunTypeNode*))visit_FunType,
  };
  ASTVisitorInit((ASTVisitor*)&r, &vfn);
  write_node(&r, n);

  sbuf_terminate(&r.buf);
  s->len = MIN(s->cap, r.buf.len);

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



