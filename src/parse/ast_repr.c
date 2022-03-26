#include "../coimpl.h"
#include "../tstyle.h"
#include "../array.h"
#include "../str.h"
#include "../sbuf.h"
#include "../unicode.c"
#include "../test.c"
#include "ast.h"
#include "universe.h"

// DEBUG_INCLUDE_POINTERS: define to include node memory addresses in output
//#define DEBUG_INCLUDE_POINTERS

// INDENT_DEPTH is the number of spaces used for indentation
#define INDENT_DEPTH 2


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
  case NConst: // const x
    s = STR(s, "const");
    return SYM(SPACE(s), ((LocalNode*)n)->name);
  case NVar: // var x
    s = STR(s, "var");
    return SYM(SPACE(s), ((LocalNode*)n)->name);
  case NParam: // param x
    s = STR(s, "param");
    return SYM(SPACE(s), ((LocalNode*)n)->name);
  case NMacroParam: // macroparam x
    s = STR(s, "macroparam");
    return SYM(SPACE(s), ((LocalNode*)n)->name);
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
  SBuf  buf;
  usize indent;
  usize lnstart; // relative to buf.len
  usize wrapcol;
  usize stylelen; // buf.len-stylelen = nbytes written to buf excluding ANSI codes

  TStyles     styles;
  TStyleStack stylestack;
  const char* lparen;
  const char* rparen;
};

#define STYLE_NODE  TS_BOLD        // node name
#define STYLE_LIT   TS_LIGHTGREEN
#define STYLE_NAME  TS_LIGHTBLUE   // symbolic names like Id, NamedType, etc.
#define STYLE_OP    TS_LIGHTORANGE
#define STYLE_TYPE  TS_DARKGREY_BG
#define STYLE_META  TS_DIM

// -- repr output writers

static usize printable_len(Repr* r) {
  // nbytes written to buf excluding ANSI codes
  return r->buf.len - r->stylelen;
}

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

static void write_newline(Repr* r) {
  SBuf* buf = &r->buf;
  sbuf_appendc(buf, '\n');
  r->lnstart = printable_len(r);
  sbuf_appendfill(buf, ' ', r->indent);
}

static void write_push_indent(Repr* r) {
  r->indent += INDENT_DEPTH;
  write_newline(r);
}

static void write_pop_indent(Repr* r) {
  assertf(r->indent > 1, "write_pop_indent without matching write_push_indent");
  r->indent -= 2;
}

typedef struct Meta Meta;
struct Meta {
  Repr* r;
  usize startlen;
};
static Meta meta_begin(Repr* r) {
  return (Meta){ .r = r, .startlen = r->buf.len };
}
static void _meta_start(Meta* mp) {
  if (mp->startlen == mp->r->buf.len) {
    sbuf_appendc(&mp->r->buf, ' ');
    write_push_style(mp->r, STYLE_META);
    sbuf_appendc(&mp->r->buf, '[');
  } else {
    sbuf_appendc(&mp->r->buf, ' ');
  }
}
#define meta_write_entry_start(m) _meta_start(&(m))
#define meta_write_entry(m,s) ({ _meta_start(&(m)); sbuf_appendstr(&(m).r->buf, (s)); })
#define meta_end(m) _meta_end(&(m))
static void _meta_end(Meta* m) {
  if (m->startlen < m->r->buf.len) {
    sbuf_appendc(&m->r->buf, ']');
    write_pop_style(m->r);
  }
}


static void write_node_fields(Repr* r, const Node* n);
static void _write_node1(Repr* r, const Node* nullable n);
static void write_node(Repr* r, const Node* nullable n) {
  SBuf* buf = &r->buf;

  // bool is_long_line = printable_len(r) - r->lnstart > r->wrapcol;
  char lastch = *(buf->p - MIN(buf->len, 1));
  bool indent = buf->len > 0 && lastch != '<';

  if (indent) write_push_indent(r);

  if (n == NULL || n == (Node*)kExpr_nil) {
    // "nil"
    write_push_style(r, STYLE_LIT);
    sbuf_appendstr(buf, "nil");
    write_pop_style(r);
  } else {
    if (is_Type(n)) write_push_style(r, STYLE_TYPE);

    if (is_BasicTypeNode(n)) {
      // "name" for BasicType (e.g. "int")
      write_push_style(r, STYLE_NODE);
      Sym name = ((BasicTypeNode*)n)->name;
      sbuf_append(buf, name, symlen(name));
      write_pop_style(r);
    } else {
      // "(Node field1 field2 ...fieldN)" for everything else
      write_paren_start(r);
      _write_node1(r, n);
      write_paren_end(r);
    }

    if (is_Type(n)) write_pop_style(r);
  }

  if (indent) write_pop_indent(r);
}

#define write_node(r,n) write_node((r),as_Node(n))

static void _write_node1(Repr* r, const Node* n) {
  SBuf* buf = &r->buf;

  // "<type>" of expressions
  if (is_Expr(n)) {
    auto typ = ((Expr*)n)->type;
    write_push_style(r, STYLE_TYPE);
    sbuf_appendc(&r->buf, '<');
    if (typ == NULL) {
      sbuf_appendstr(&r->buf, "?");
    } else {
      write_node(r, typ);
    }
    sbuf_appendc(&r->buf, '>');
    write_pop_style(r);
    sbuf_appendc(&r->buf, ' ');
  }

  // "NodeName"
  write_push_style(r, STYLE_NODE);
  sbuf_appendstr(buf, NodeKindName(n->kind));
  write_pop_style(r);

  // "[meta]"
  auto m = meta_begin(r);
  NodeFlags fl = n->flags;
  if (fl & NF_Unresolved)  meta_write_entry(m, "unres");
  if (fl & NF_Const)       meta_write_entry(m, "const");
  if (fl & NF_Base)        meta_write_entry(m, "base");
  if (fl & NF_RValue)      meta_write_entry(m, "rval");
  if (fl & NF_Unused)      meta_write_entry(m, "unused");
  if (fl & NF_Public)      meta_write_entry(m, "pub");
  if (fl & NF_Named)       meta_write_entry(m, "named");
  if (fl & NF_PartialType) meta_write_entry(m, "partialtype");
  if (fl & NF_CustomInit)  meta_write_entry(m, "custominit");
  meta_end(m);

  write_node_fields(r, n);
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
  write_push_style(r, STYLE_LIT);
  sbuf_appendstr(buf, " \"");
  sbuf_appendrepr(buf, s, len);
  sbuf_appendc(buf, '"');
  write_pop_style(r);
}

static void write_name(Repr* r, Sym s) {
  sbuf_appendc(&r->buf, ' ');
  write_push_style(r, STYLE_NAME);
  sbuf_append(&r->buf, s, symlen(s));
  write_pop_style(r);
}

#define write_TODO(r) _write_TODO(r, __FILE__, __LINE__)
static void _write_TODO(Repr* r, const char* file, u32 line) {
  sbuf_appendc(&r->buf, ' ');
  write_push_style(r, TS_RED);
  sbuf_appendstr(&r->buf, "[TODO ");
  sbuf_appendstr(&r->buf, file);
  sbuf_appendc(&r->buf, ':');
  sbuf_appendu32(&r->buf, line, 10);
  sbuf_appendc(&r->buf, ']');
  write_pop_style(r);
}

// -- visitor functions

static void write_node_fields(Repr* r, const Node* np) {
  SBuf* buf = &r->buf;

  #define _(NAME)             \
    return; } case N##NAME: { \
      UNUSED auto n = (const NAME##Node*)np;

  #define _G(NAME)                                  \
    return; } case N##NAME##_BEG ... N##NAME##_END: { \
      UNUSED auto n = (const NAME##Node*)np;

  switch (np->kind) {
  case NBad: {
  _(Field) write_TODO(r);

  _(Pkg)
    write_qstr(r, n->name, strlen(n->name));
    if (n->a.len > 0)
      write_array(r, as_NodeArray(&n->a));

  _(File)
    write_qstr(r, n->name, strlen(n->name));
    if (n->a.len > 0)
      write_array(r, as_NodeArray(&n->a));

  _(Comment) write_TODO(r);

  // expressions
  _(Nil) UNREACHABLE; // handled by _write_node

  _(BoolLit)
    sbuf_appendc(buf, ' ');
    write_push_style(r, STYLE_LIT);
    sbuf_appendstr(buf, n->ival ? "true" : "false");
    write_pop_style(r);

  _(IntLit)
    sbuf_appendc(buf, ' ');
    write_push_style(r, STYLE_LIT);
    sbuf_appendu64(buf, n->ival, 10);
    write_pop_style(r);

  _(FloatLit)
    sbuf_appendc(buf, ' ');
    write_push_style(r, STYLE_LIT);
    sbuf_appendf64(buf, n->fval, 10);
    write_pop_style(r);

  _(StrLit)
    write_qstr(r, n->sp, n->len);

  _(Id)
    write_name(r, n->name);
    write_node(r, n->target);

  _(BinOp)
    write_push_style(r, STYLE_OP);
    write_str(r, TokName(n->op));
    write_pop_style(r);
    write_node(r, n->left);
    write_node(r, n->right);

  _(PrefixOp) write_TODO(r);
  _(PostfixOp) write_TODO(r);
  _(Return) write_TODO(r);

  _(Assign)
    write_node(r, n->dst);
    write_node(r, n->val);

  _(Tuple) write_array(r, as_NodeArray(&n->a));
  _(Array) write_array(r, as_NodeArray(&n->a));

  _(Block)
    if (n->a.len > 0)
      write_array(r, as_NodeArray(&n->a));

  _(Fun)
    write_name(r, n->name ? n->name : kSym__);
    write_node(r, n->params);
    write_node(r, n->result);
    write_node(r, n->body);

  _G(Local)
    write_name(r, n->name);
    if (LocalInitField(n))
      write_node(r, LocalInitField(n));

  _(Macro)    write_TODO(r);
  _(Call)     write_TODO(r);
  _(TypeCast) write_TODO(r);
  _(Ref)      write_TODO(r);
  _(NamedArg) write_TODO(r);
  _(Selector) write_TODO(r);
  _(Index)    write_TODO(r);
  _(Slice)    write_TODO(r);
  _(If)       write_TODO(r);

  // types
  _(BasicType) UNREACHABLE; // handled by _write_node
  _(TypeType)  write_TODO(r);
  _(NamedType) write_name(r, n->name);
  _(RefType)   write_node(r, n->elem);

  _(AliasType)
    write_name(r, n->name);
    write_node(r, n->type);

  _(ArrayType)  write_TODO(r);
  _(TupleType)  write_TODO(r);
  _(StructType) write_TODO(r);
  _(FunType)    write_TODO(r);
  }}
  #undef _
}


// -- NodeRepr entry

Str nullable _NodeRepr(Str s, const Node* nullable n, NodeReprFlags fl) {
  Repr r = {
    .wrapcol = 30,
  };
  r.styles = TStylesForStderr();
  if (TStylesIsNone(r.styles)) {
    r.lparen = "(";
    r.rparen = ")";
  } else {
    r.lparen = "\x1b[2m(\x1b[22m";
    r.rparen = "\x1b[2m)\x1b[22m";
  }

  Str s2 = str_makeroom(s, 4096);
  if (s2)
    s = s2;

  while (1) {
    sbuf_init(&r.buf, &s->p[s->len], str_avail(s));
    write_node(&r, n);
    sbuf_terminate(&r.buf);
    if (r.buf.len < str_avail(s)) {
      s->len += r.buf.len;
      return s;
    }
    if (!(s2 = str_makeroom(s, r.buf.len)))
      return s; // memalloc failure
    s = s2;
  }
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



