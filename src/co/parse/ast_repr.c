#include "../common.h"
#include "../util/tstyle.h"
#include "../util/array.h"
#include "../util/tmpstr.h"
#include "../util/ptrmap.h"
#include "parse.h"

// DEBUG_INCLUDE_POINTERS: define to include node memory addresses in output
//#define DEBUG_INCLUDE_POINTERS

// INDENT_DEPTH is the number of spaces used for indentation
#define INDENT_DEPTH 2


static TStyle id_color      = TStyle_lightyellow;
static TStyle type_color    = TStyle_blue;
static TStyle typeval_color = TStyle_lightblue; // type used as a value
static TStyle field_color   = TStyle_pink;
static TStyle ref_color     = TStyle_red;
static TStyle attr_color    = TStyle_orange;
static TStyle lit_color     = TStyle_lightpurple;
static TStyle op_color      = TStyle_lightgreen;


ConstStr fmtnode(const Node* n) {
  Str* sp = tmpstr_get();
  *sp = NodeStr(*sp, n);
  return *sp;
}

ConstStr fmtast(const Node* n) {
  Str* sp = tmpstr_get();
  *sp = NodeRepr(n, *sp, NodeReprDefault);
  return *sp;
}


Str NValFmt(Str s, const NVal v) {
  switch (v.ct) {

  case CType_int:
    if (v.i > 0x7fffffffffffffff) {
      return str_appendfmt(s, FMT_U64, v.i);
    } else {
      return str_appendfmt(s, FMT_S64, (i64)v.i);
    }

  case CType_rune:
  case CType_float:
  case CType_str:
    panic("TODO NValFmt");
    break;

  case CType_bool:
    return str_appendcstr(s, v.i == 0 ? "false" : "true");

  case CType_nil:
    str_appendcstr(s, "nil");
    break;

  case CType_INVALID:
    assert(0 && "unexpected CType");
    break;
  }
  return str_appendcstr(s, "?");
}


// ==============================================================================================
// shared helpers


inline static Str style_push(StyleStack* st, Str s, TStyle style) {
  if (st->styles == TStyleNone)
    return s;
  return StyleStackPush(st, s, style);
}

inline static Str style_pop(StyleStack* st, Str s) {
  if (st->styles == TStyleNone)
    return s;
  return StyleStackPop(st, s);
}

static Str styleStackInitRepr(StyleStack* sstack, Str s, NodeReprFlags fl) {
  TStyleTable styles = TStyleNone;
  if ((fl&NodeReprNoColor) == 0 && ((fl&NodeReprColor) || TSTyleStderrIsTTY())) {
    styles = TSTyleForTerm();
  }
  StyleStackInit(sstack, styles);
  return style_push(sstack, s, TStyle_bold);
}


// ==============================================================================================
// NodeStr


static Str str_append_NodeArray(Str s, const NodeArray* na) {
  bool isFirst = true;
  for (u32 i = 0; i < na->len; i++) {
    Node* n = na->v[i];
    if (isFirst) {
      isFirst = false;
    } else {
      s = str_appendc(s, ' ');
    }
    s = NodeStr(s, n);
  }
  return s;
}


// appends a short representation of an AST node to s, suitable for use in error messages.
Str NodeStr(Str s, const Node* n) {
  // Note: Do not include type information.
  // Instead, in use sites, call fmtnode individually for n->type when needed.

  if (n == NULL)
    return str_appendcstr(s, "<null>");

  switch (n->kind) {

  // uses no extra data
  case NNil: // nil
    return str_appendcstr(s, "nil");

  case NBoolLit: // true | false
    return str_appendcstr(s, n->val.i == 0 ? "false" : "true");

  case NIntLit: // 123
    return str_appendfmt(s, FMT_U64, n->val.i);

  case NFloatLit: // 12.3
    return str_appendfmt(s, "%f", n->val.f);

  case NId: // foo
    return str_append(s, n->ref.name, symlen(n->ref.name));

  case NBinOp: // foo + bar
    s = NodeStr(s, n->op.left);
    s = str_appendc(s, ' ');
    s = str_appendcstr(s, TokName(n->op.op));
    s = str_appendc(s, ' ');
    return NodeStr(s, n->op.right);

  case NPostfixOp: // foo++
    s = NodeStr(s, n->op.left);
    return str_appendcstr(s, TokName(n->op.op));

  case NPrefixOp: // -foo
    s = str_appendcstr(s, TokName(n->op.op));
    return NodeStr(s, n->op.left); // note: prefix op uses left, not right.

  case NAssign: // thing=
    s = NodeStr(s, n->op.left);
    return str_appendc(s, '=');

  case NReturn: // return thing
    s = str_appendcstr(s, "return ");
    return NodeStr(s, n->op.left);

  case NBlock: // {int}
    return str_appendcstr(s, "block");

  case NTuple: // (one two 3)
    s = str_appendc(s, '(');
    s = str_append_NodeArray(s, &n->array.a);
    return str_appendc(s, ')');

  case NPkg: // pkg
    return str_appendcstr(s, "pkg");

  case NFile: // file
    return str_appendcstr(s, "file");

  case NLet: // let
    return str_appendfmt(s, "%s %s", n->let.ismut ? "var" : "let", n->let.name);

  case NArg: // foo
    return str_append(s, n->field.name, symlen(n->field.name));

  case NFun: // fun foo
    s = str_appendcstr(s, "function");
    if (n->fun.name) {
      s = str_appendc(s, ' ');
      s = str_appendcstr(s, n->fun.name);
    }
    return s;

  case NTypeCast: // typecast<int16>
    s = str_appendcstr(s, "typecast<");
    s = NodeStr(s, n->call.receiver);
    return str_appendc(s, '>');

  case NCall: // call foo
    return str_appendcstr(s, "call");

  case NIf: // if
    return str_appendcstr(s, "if");

  case NBasicType: // int
    if (n == Type_ideal)
      return str_appendcstr(s, "ideal");
    return str_append(s, n->t.basic.name, symlen(n->t.basic.name));

  case NFunType: // (int int)->bool
    if (n->t.fun.params == NULL) {
      s = str_appendcstr(s, "()");
    } else {
      s = NodeStr(s, n->t.fun.params);
    }
    s = str_appendcstr(s, "->");
    return NodeStr(s, n->t.fun.result); // ok if NULL

  case NTupleType: // (int bool Foo)
    s = str_appendc(s, '(');
    s = str_append_NodeArray(s, &n->t.list.a);
    return str_appendc(s, ')');

  case NArrayType: // [4]int, []int
    return str_appendcstr(s, n->t.array.sizeExpr ? "array" : "slice");

  // The remaining types are not expected to appear. Use their kind if they do.
  case NBad:
  case NNone:
  case NField: // field is not yet implemented by parser
    return str_appendcstr(s, NodeKindName(n->kind));

  // TODO
  case NStrLit:
    panic("TODO %s", NodeKindName(n->kind));

  case _NodeKindMax:
    break;
  }

  return str_appendcstr(s, "INVALID");
}


// ==============================================================================================
// NodeRepr

typedef struct LReprCtx {
  NodeReprFlags fl;        // flags
  Str           s;         // output buffer
  u32           ind;       // indentation depth
  PtrMap        seenmap;   // nodes we've already printed
  StyleStack    style;     // ANSI terminal styling
  u32           linestart; // offset into s of the current line's start (index of last '\n' byte)
  u32           styleoffs; // number of bytes of style at the beginning of linestart
  u32           maxline;   // limits the maximum length of output lines
  u32           typenest;  // >0 when visiting types

  // pre-styled chunks
  const char* lparen;   // (
  const char* rparen;   // )
  const char* lbrack;   // [
  const char* rbrack;   // ]
} LReprCtx;

static bool l_visit(NodeList* nl, void* cp);
static void l_append_fields(const Node* n, LReprCtx* c);

Str NodeRepr(const Node* n, Str s, NodeReprFlags fl) {
  LReprCtx c = {
    .fl = fl,
    .s = s,
    .maxline = 80,
  };
  c.s = styleStackInitRepr(&c.style, c.s, fl);
  PtrMapInit(&c.seenmap, 64, MemHeap);

  if (c.style.styles == TStyleNone) {
    c.lparen = "(";
    c.rparen = ")";
    c.lbrack = "[";
    c.rbrack = "]";
  } else {
    c.lparen = "\x1b[2m(\x1b[22;1m";
    c.rparen = "\x1b[2m)\x1b[22;1m";
    c.lbrack = "\x1b[2m[\x1b[22;1m";
    c.rbrack = "\x1b[2m]\x1b[22;1m";
  }

  NodeVisit(n, &c, l_visit);

  PtrMapDispose(&c.seenmap);
  StyleStackDispose(&c.style);
  return c.s;
}

static Str append_indent(Str s, u32 nspaces) {
  const char spaces[] = "                                ";
  const u32 maxspaces = (u32)strlen(spaces);
  char* p = str_reserve(&s, nspaces);
  while (nspaces) {
    u32 z = MIN(maxspaces, nspaces);
    memcpy(p, spaces, z);
    p += z;
    nspaces -= z;
  }
  return s;
}

// length of current line
static u32 l_curr_line_len(LReprCtx* c, Str s) {
  u32 currcol = str_len(s);
  // subtract bytes in s used for ANSI styling
  u32 stylebytes = c->style.nbyteswritten - c->styleoffs;
  assert_debug(currcol >= c->linestart);
  assert_debug(stylebytes <= currcol - c->linestart);
  return currcol - stylebytes - c->linestart;
}

static Str l_new_line(LReprCtx* c, Str s) {
  c->linestart = str_len(s);
  c->styleoffs = c->style.nbyteswritten;
  return str_appendc(s, '\n');
}


static uintptr_t l_seen_id(LReprCtx* c, const Node* n, bool* newfound) {
  // Using stable counter instead of address/pointer makes the output deterministic
  uintptr_t id = (uintptr_t)PtrMapGet(&c->seenmap, n);
  if (id == 0) {
    if (newfound)
      *newfound = true;
    id = (uintptr_t)PtrMapLen(&c->seenmap) + 1;
    PtrMapSet(&c->seenmap, n, (void*)id);
  }
  return id;
}


static bool l_collapse_field(NodeList* nl) {
  if (!nl->parent)
    return false;
  switch (nl->parent->n->kind) {

  case NId:
  case NLet:
  case NArg:
  case NField:
  case NTuple:
  case NReturn:
  case NBoolLit:
  case NFloatLit:
  case NIntLit:
  case NStrLit:
    return true;

  default:
    if (NodeKindIsType(nl->parent->n->kind))
      return true;
    return false;
  }
}

// only called if l_collapse_field(nl)==false
static bool l_show_field(NodeList* nl) {
  if (!nl->parent)
    return true;
  switch (nl->parent->n->kind) {
    case NBinOp:
    case NPostfixOp:
    case NPrefixOp:
    case NCall:
      return false;
    case NFun:
      return !NodeKindIsType(nl->n->kind);
    default:
      if (NodeKindIsType(nl->n->kind))
        return false;
      return true;
  }
}


static const char* l_listname(NodeList* nl) {
  const Node* n = nl->n;
  switch (n->kind) {
    case NBasicType:
      return n->t.basic.name;
    case NTuple:
    case NTupleType:
      return "";
    default:
      return NodeKindName(n->kind);
  }
}


static Str append_delim(LReprCtx* c, Str s, const char* chunk) {
  s = str_appendcstr(s, chunk);
  if (c->style.styles != TStyleNone)
    c->style.nbyteswritten += strlen(chunk) - 1;
  return s;
}


static bool l_is_first_tuple_item(NodeList* nl) {
  return (
    nl->parent &&
    (nl->parent->n->kind == NTupleType || nl->parent->n->kind == NTuple) &&
    nl->index == 0 &&
    nl->fieldname == NULL
  );
}


static Str l_maybe_append_special(NodeList* nl, Str s) {
  // basic constants and types are simply shown as names e.g. "int", "nil", "true"
  auto n = nl->n;
  if (n == Const_nil || n == Const_true || n == Const_false)
    n = n->type;
  if (n->kind == NBasicType)
    return str_append(s, n->t.basic.name, symlen(n->t.basic.name));
  return s;
}


static bool l_visit(NodeList* nl, void* cp) {
  auto c = (LReprCtx*)cp;
  Str s = c->s;
  auto n = nl->n;
  u32 addedIndent = 0;
  u32 numExtraEndParens = 0;

  // do resizing of the string buffer up front
  s = str_makeroom(s, 64 + c->ind);

  // type as value?
  if (c->typenest == 0 && NodeKindIsType(nl->n->kind))
    s = style_push(&c->style, s, typeval_color);

  // indentation and fieldname
  if (c->ind > 0) {
    bool collapseField = l_collapse_field(nl);
    if (collapseField && l_curr_line_len(c, s) < c->maxline) {
      // just a space as separator
      if (!l_is_first_tuple_item(nl))
        s = str_appendc(s, ' ');
    } else {
      // new line
      s = l_new_line(c, s);
      s = append_indent(s, c->ind);
      // maybe include fieldname
      if (nl->fieldname && !collapseField && l_show_field(nl)) {
        u32 fieldname_len = (u32)strlen(nl->fieldname);
        s = append_delim(c, s, c->lparen);
        numExtraEndParens++;
        s = style_push(&c->style, s, field_color);
        s = str_append(s, nl->fieldname, fieldname_len);
        s = style_pop(&c->style, s);
        s = str_appendc(s, ' ');
        addedIndent = fieldname_len + INDENT_DEPTH;
        if (n->kind == NTuple)
          addedIndent += 1;
      }
      addedIndent += INDENT_DEPTH;
    }
  } else {
    addedIndent = INDENT_DEPTH;
  }

  // specials
  if (c->typenest != 1) {
    // Note: (typenest != 1) ensures that when we begin printing a type it is always
    // surrounded by '[' and ']', even for e.g. "int", "nil" etc.
    u32 len1 = str_len(s);
    s = l_maybe_append_special(nl, s);
    if (str_len(s) != len1) {
      addedIndent = 0;
      goto end;
    }
  }

  c->ind += addedIndent;
  bool descend = true;

  // header, e.g. "(NodeKind" or "[" for types
  s = append_delim(c, s, c->typenest ? c->lbrack : c->lparen);
  s = str_appendcstr(s, l_listname(nl));

  switch (n->kind) {

  // functions can reference themselves
  case NFun: {
    if (n->fun.name) {
      s = str_appendc(s, ' ');
      s = str_append(s, n->fun.name, symlen(n->fun.name));
    }
    // Include a function identifier which we can use to map references in the output.
    bool newfound = false;
    auto id = l_seen_id(c, n, &newfound);
    if (!newfound && nl->parent && nl->parent->n->kind != NFile) {
      // this function has been seen before and we have printed it as it was
      // not defined in the file scope.
      descend = false;
    }
    s = style_push(&c->style, s, ref_color);
    s = str_appendfmt(s, " #%zu", (size_t)id);
    s = style_pop(&c->style, s);
    break;
  }
  case NFile: {
    // allocate function ids up front to avoid expanding a referenced function inside a body
    // when the definition trails the use, syntactically.
    for (u32 i = 0; i < n->array.a.len; i++) {
      const Node* cn = n->array.a.v[i];
      if (cn->kind == NFun)
        l_seen_id(c, cn, NULL);
    }
    break;
  }
  case NLet: {
    // Let
    bool newfound = false;
    auto id = l_seen_id(c, n, &newfound);
    s = style_push(&c->style, s, ref_color);
    s = str_appendfmt(s, " #%zu", (size_t)id);
    s = style_pop(&c->style, s);
    descend = newfound;
    break;
  }
  default:
    break;
  } // switch(n->kind)

  // attributes
  if (NodeIsUnresolved(n)) {
    s = style_push(&c->style, s, attr_color);
    s = str_appendcstr(s, " @unres");
    s = style_pop(&c->style, s);
  }
  #ifdef DEBUG_INCLUDE_POINTERS
    s = style_push(&c->style, s, attr_color);
    s = str_appendfmt(s, " @ptr(%p)", n);
    s = style_pop(&c->style, s);
  #endif

  // include fields and children
  if (descend) {
    c->s = s; // store s

    // fields
    l_append_fields(n, c);

    // visit children
    NodeVisitChildren(nl, cp, l_visit);

    s = c->s; // load s
  }

  // include type
  const Type* typ = n->type;
  if (c->typenest == 0 && (c->fl & NodeReprTypes) && typ && typ != n) {
    s = style_push(&c->style, s, type_color);
    NodeList tnl = { .n = typ, .parent = nl, .fieldname = "type" };
    if (nl->parent == NULL || nl->parent->n->type != typ) {
      // okay, let's print this type as it differs from the parent type
      c->s = s; // store s
      c->typenest++;
      l_visit(&tnl, c);
      c->typenest--;
      s = c->s; // load s
    } else {
      // Type is same as parent type.
      // To avoid repeat printing of type trees, print something short and symbolic.
      // s = str_appendcstr(s, " •");
      s = str_appendc(s, ' ');
      s = str_appendcstr(s, l_listname(&tnl));
    }
    s = style_pop(&c->style, s);
  }

  // end list
  s = append_delim(c, s, c->typenest ? c->rbrack : c->rparen);

end:
  while (numExtraEndParens) {
    s = append_delim(c, s, c->rparen);
    numExtraEndParens--;
  }

  // end type color
  if (c->typenest == 0 && NodeKindIsType(nl->n->kind))
    s = style_pop(&c->style, s);

  c->ind -= addedIndent;
  c->s = s; // store s
  return true;
}


static void l_append_fields(const Node* n, LReprCtx* c) {
  Str s = c->s; // load s
  s = str_appendc(s, ' ');
  switch (n->kind) {

  case NId:
    s = style_push(&c->style, s, id_color);
    s = str_append(s, n->ref.name, symlen(n->ref.name));
    s = style_pop(&c->style, s);
    break;

  case NLet:
    s = style_push(&c->style, s, id_color);
    s = str_append(s, n->let.name, symlen(n->let.name));
    s = style_pop(&c->style, s);
    if (n->let.ismut) {
      s = style_push(&c->style, s, attr_color);
      s = str_appendcstr(s, " @mutable");
      s = style_pop(&c->style, s);
    }
    if (c->fl & NodeReprLetRefs) {
      s = style_push(&c->style, s, ref_color);
      s = str_appendfmt(s, " (uses %u)", n->let.nrefs);
      s = style_pop(&c->style, s);
    }
    break;

  case NArg:
  case NField:
    s = style_push(&c->style, s, id_color);
    s = str_append(s, n->field.name, symlen(n->field.name));
    s = style_pop(&c->style, s);
    break;

  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
    s = style_push(&c->style, s, op_color);
    s = str_appendcstr(s, TokName(n->op.op));
    s = style_pop(&c->style, s);
    break;

  case NIntLit:
  case NBoolLit:
  case NFloatLit:
    s = style_push(&c->style, s, lit_color);
    s = NValFmt(s, n->val);
    s = style_pop(&c->style, s);
    break;

  default:
    str_setlen(s, str_len(s) - 1); // undo ' '
    break;
  }
  c->s = s; // store s
  return;
}
