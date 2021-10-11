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


static Str str_append_NodeArray(Str s, const NodeArray* na, const char* glue, u32 gluelen) {
  bool isFirst = true;
  for (u32 i = 0; i < na->len; i++) {
    Node* n = na->v[i];
    if (isFirst) {
      isFirst = false;
    } else {
      s = str_append(s, glue, gluelen);
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
    return str_append(s, n->id.name, symlen(n->id.name));

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

  case NArray: // [one two 3]
    s = str_appendc(s, '[');
    s = str_append_NodeArray(s, &n->array.a, " ", 1);
    return str_appendc(s, ']');

  case NTuple: // (one two 3)
    s = str_appendc(s, '(');
    s = str_append_NodeArray(s, &n->array.a, " ", 1);
    return str_appendc(s, ')');

  case NPkg: // pkg
    s = str_appendcstr(s, "pkg \"");
    s = str_append(s, n->cunit.name, strlen(n->cunit.name));
    return str_appendc(s, '"');

  case NFile: // file
    s = str_appendcstr(s, "file \"");
    s = str_append(s, n->cunit.name, strlen(n->cunit.name));
    return str_appendc(s, '"');

  case NVar: // var x | param x
    return str_appendfmt(s, "%s %s",
      (n->var.isconst ? "const" :
       NodeIsParam(n) ? "param" :
                        "var"),
      n->var.name);

  case NRef: // &x, mut&x
    if (NodeIsConst(n)) {
      s = str_appendc(s, '&');
    } else {
      s = str_appendcstr(s, "mut&");
    }
    return NodeStr(s, n->ref.target);

  case NFun: // fun foo
    s = str_appendcstr(s, "function");
    if (n->fun.name) {
      s = str_appendc(s, ' ');
      s = str_appendcstr(s, n->fun.name);
    }
    return s;

  case NMacro: // macro foo
    s = str_appendcstr(s, "macro");
    if (n->macro.name) {
      s = str_appendc(s, ' ');
      s = str_appendcstr(s, n->macro.name);
    }
    return s;

  case NTypeCast: // typecast<int16>
    s = str_appendcstr(s, "typecast<");
    s = NodeStr(s, n->call.receiver);
    return str_appendc(s, '>');

  case NCall: // call foo
    s = str_appendcstr(s, "call ");
    return NodeStr(s, n->call.receiver);

  case NIf: // if
    return str_appendcstr(s, "if");

  case NSelector: // expr.name | expr.selector
    s = NodeStr(s, n->sel.operand);
    s = str_appendc(s, '.');
    return str_append(s, n->sel.member, symlen(n->sel.member));

  case NIndex: // expr[index]
    return str_appendcstr(s, "subscript");
    // s = NodeStr(s, n->index.operand);
    // s = str_appendc(s, '[');
    // s = NodeStr(s, n->index.index);
    // return str_appendc(s, ']');

  case NSlice: // expr[start?:end?]
    return str_appendcstr(s, "slice");
    // s = NodeStr(s, n->slice.operand);
    // s = str_appendc(s, '[');
    // if (n->slice.start)
    //   s = NodeStr(s, n->slice.start);
    // s = str_appendc(s, ':');
    // if (n->slice.end)
    //   s = NodeStr(s, n->slice.end);
    // return str_appendc(s, ']');

  case NBasicType: // int
    if (n == Type_ideal)
      return str_appendcstr(s, "ideal");
    return str_append(s, n->t.basic.name, symlen(n->t.basic.name));

  case NRefType: // &T, mut&T
    if (NodeIsConst(n)) {
      s = str_appendc(s, '&');
    } else {
      s = str_appendcstr(s, "mut&");
    }
    return NodeStr(s, n->t.ref);

  case NField:
    s = str_appendcstr(s, "field ");
    s = str_append(s, n->field.name, symlen(n->field.name));
    s = str_appendc(s, ' ');
    return NodeStr(s, n->type);

  case NNamedVal:
    s = str_append(s, n->namedval.name, symlen(n->namedval.name));
    s = str_appendc(s, '=');
    return NodeStr(s, n->namedval.value);

  case NFunType: // (int int)->bool
    if (n->t.fun.params == NULL) {
      s = str_appendcstr(s, "()");
    } else {
      // TODO: include names
      s = NodeStr(s, n->t.fun.params->type);
    }
    s = str_appendcstr(s, "->");
    return NodeStr(s, n->t.fun.result); // ok if NULL

  case NTupleType: // (int bool Foo)
    s = str_appendc(s, '(');
    s = str_append_NodeArray(s, &n->t.tuple.a, " ", 1);
    return str_appendc(s, ')');

  case NArrayType: // [int 4], [int]
    s = str_appendc(s, '[');
    s = NodeStr(s, n->t.array.subtype);
    if (n->t.array.size > 0) {
      s = str_appendc(s, ' ');
      s = str_appendu64(s, n->t.array.size, 10);
    }
    return str_appendc(s, ']');

  case NStructType: { // "struct Name" or "struct {foo float; y bool}"
    s = str_appendcstr(s, "struct ");
    if (n->t.struc.name)
      return str_append(s, n->t.struc.name, symlen(n->t.struc.name));
    s = str_appendc(s, '{');
    bool isFirst = true;
    for (u32 i = 0; i < n->t.struc.a.len; i++) {
      Node* cn = n->t.struc.a.v[i];
      if (isFirst) {
        isFirst = false;
      } else {
        s = str_appendcstr(s, "; ");
      }
      if (cn->kind == NField) {
        s = str_append(s, cn->field.name, symlen(cn->field.name));
        s = str_appendc(s, ' ');
        s = NodeStr(s, cn->type);
      } else {
        s = NodeStr(s, cn);
      }
    }
    return str_appendc(s, '}');
  }

  case NTypeType: // "type ..."
    s = str_appendcstr(s, "type ");
    return NodeStr(s, n->t.type);

  // The remaining types are not expected to appear. Use their kind if they do.
  case NBad:
  case NNone:
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
  // const char* lbrack;   // [
  // const char* rbrack;   // ]
  const char* langle;   // <
  const char* rangle;   // >

  const char* delim_open;   // ( or <
  const char* delim_close;  // ) or >
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
    // c.lbrack = "[";
    // c.rbrack = "]";
    c.langle = "<";
    c.rangle = ">";
  } else {
    c.lparen = "\x1b[2m(\x1b[22;1m";
    c.rparen = "\x1b[2m)\x1b[22;1m";
    // c.lbrack = "\x1b[2m[\x1b[22;1m";
    // c.rbrack = "\x1b[2m]\x1b[22;1m";
    c.langle = "\x1b[2m<\x1b[22;1m";
    c.rangle = "\x1b[2m>\x1b[22;1m";
  }

  c.delim_open = c.lparen;
  c.delim_close = c.rparen;

  NodeVisit(assertnotnull_debug(n), &c, l_visit);

  c.s = style_pop(&c.style, c.s);
  asserteq(c.style.stack.len, 0); // style_push/style_pop calls should be balanced

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


static bool l_is_compact(Node* n) {
  return n == NULL ||
         (NodeKindClass(n->kind) & NodeClassLit) ||
         NodeIsPrimitiveConst(n);
}


static bool l_collapse_field(LReprCtx* c, NodeList* nl) {
  if (!nl->parent)
    return false;

  // don't collapse nodes which are likely to print many lines
  if (nl->parent->n->kind != NTypeType && nl->n->kind == NStructType &&
    PtrMapGet(&c->seenmap, nl->n) == NULL)
  {
    switch (nl->n->kind) {
      case NStructType:
      case NFun:
      case NTuple:
        return false;
      default:
        break;
    }
  }

  switch (nl->parent->n->kind) {

    case NField:
      return l_is_compact(nl->parent->n->type) && l_is_compact(nl->parent->n->field.init);
    case NVar:
      return (
        NodeIsParam(nl->parent->n) ||
        (l_is_compact(nl->parent->n->type) && l_is_compact(nl->parent->n->var.init))
      );

    case NBoolLit:
    case NFloatLit:
    case NId:
    case NRef:
    case NIntLit:
    case NReturn:
    case NStrLit:
    case NNamedVal:
    case NTypeType:
      return true;

    case NStructType:
      return false;

    default:
      if (NodeIsType(nl->parent->n))
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
    case NCall:
    case NIndex:
    case NPostfixOp:
    case NPrefixOp:
    case NSelector:
    case NTypeCast:
    case NVar:
      return false;

    case NFun:
      return !NodeIsType(nl->n);

    default:
      if (NodeIsType(nl->n))
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
      if (nl->parent && nl->parent->n && nl->parent->n->kind == NVar)
        return NodeKindName(n->kind);
      return "";
    case NTupleType:
      return "";
      // if (nl->parent && NodeIsType(nl->parent->n))
      //   return "";
      // return "TupleType ";
    case NStructType:
      return "struct";
    case NTypeType:
      return "type";
    case NFunType:
      return "fun";
    case NVar:
      return (
        n->var.isconst ? "const" :
        NodeIsParam(n) ? "param" :
                         "var");
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


static Str append_open_delim(LReprCtx* c, Str s) {
  s = append_delim(c, s, c->delim_open);
  c->delim_close = c->delim_open == c->lparen ? c->rparen : c->rangle;
  c->delim_open = c->lparen;
  return s;
}


static Str append_close_delim(LReprCtx* c, Str s) {
  s = append_delim(c, s, c->delim_close);
  c->delim_close = c->rparen;
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


static Str l_maybe_append_special(LReprCtx* c, NodeList* nl, Str s) {
  // basic constants and types are simply shown as names e.g. "int", "nil", "true"
  auto n = nl->n;

  if (n == Const_nil) {
    s = str_append(s, sym_nil, symlen(sym_nil));
  } else if (n == Const_true) {
    s = str_append(s, sym_true, symlen(sym_true));
  } else if (n == Const_false) {
    s = str_append(s, sym_false, symlen(sym_false));
  } else if (n->kind == NBasicType) {
    s = append_delim(c, s, c->langle);
    s = str_append(s, n->t.basic.name, symlen(n->t.basic.name));
    s = append_delim(c, s, c->rangle);
  }

  return s;
}


static bool l_visit(NodeList* nl, void* cp) {
  auto c = (LReprCtx*)cp;
  Str s = c->s;
  auto n = assertnotnull_debug(nl->n);
  u32 addedIndent = 0;
  u32 numExtraEndParens = 0;

  // do resizing of the string buffer up front
  s = str_makeroom(s, 64 + c->ind);

  // type as value?
  if (c->typenest == 0 && NodeIsType(n))
    s = style_push(&c->style, s, type_color);
  c->typenest += (u32)NodeIsType(n);

  // indentation and fieldname
  if (c->ind > 0) {
    if (l_is_first_tuple_item(nl)) {
      if (c->typenest == 0)
        s = str_appendc(s, ' ');
      addedIndent += INDENT_DEPTH;
    } else {
      bool collapseField = l_collapse_field(c, nl);
      if (collapseField && l_curr_line_len(c, s) < c->maxline) {
        // just a space as separator
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
          // if (n->kind == NTuple)
          //   addedIndent += 1;
        }
        addedIndent += INDENT_DEPTH;
      }
    }
  } else {
    addedIndent = INDENT_DEPTH;
  }

  // macro template var (except in macro "params")
  if (NodeIsMacroParam(n) &&
      ( !nl->parent || !nl->parent->parent ||
        nl->parent->n->kind != NTuple ||
        nl->parent->parent->n->kind != NMacro) )
  {
    asserteq_debug(n->kind, NVar);
    s = style_push(&c->style, s, typeval_color);
    s = str_append(s, n->var.name, symlen(n->var.name));
    s = style_pop(&c->style, s);
    addedIndent = 0;
    goto end;
  }

  // specials
  u32 len1 = str_len(s);
  s = l_maybe_append_special(c, nl, s);
  if (str_len(s) != len1) {
    addedIndent = 0;
    goto end;
  }

  c->ind += addedIndent;
  bool descend = true;

  // header, e.g. "(NodeKind"
  // bool startType = c->typenest == 1;
  // const char* delim_start = startType ? c->langle : c->lparen;
  // const char* delim_end   = startType ? c->rangle : c->rparen;
  s = append_open_delim(c, s);
  const char* delim_close = c->delim_close;
  s = str_appendcstr(s, l_listname(nl));

  // record current line so that we can later detect line breaks
  u32 linestart = c->linestart;

  switch (n->kind) {

  // functions can reference themselves
  case NFun: {
    if (n->fun.name) {
      s = str_appendc(s, ' ');
      s = style_push(&c->style, s, id_color);
      s = str_append(s, n->fun.name, symlen(n->fun.name));
      s = style_pop(&c->style, s);
    }
    // Include a function identifier which we can use to map references in the output.
    bool newfound = false;
    auto id = l_seen_id(c, n, &newfound);
    if (!newfound && nl->parent && nl->parent->n->kind != NFile) {
      // this function has been seen before and we have printed it as it was
      // not defined in the file scope.
      descend = false;
    }
    if (c->fl & NodeReprRefs) {
      s = style_push(&c->style, s, ref_color);
      s = str_appendfmt(s, " #%zu", (size_t)id);
      s = style_pop(&c->style, s);
    }
    break;
  }

  case NMacro: {
    bool newfound = false;
    auto id = l_seen_id(c, n, &newfound);
    if (!newfound && nl->parent && nl->parent->n->kind != NFile) {
      // see comments in case NFun
      descend = false;
    }
    if (n->macro.name) {
      s = str_appendc(s, ' ');
      s = style_push(&c->style, s, id_color);
      s = str_append(s, n->macro.name, symlen(n->macro.name));
      s = style_pop(&c->style, s);
    }
    if (c->fl & NodeReprRefs) {
      s = style_push(&c->style, s, ref_color);
      s = str_appendfmt(s, " #%zu", (size_t)id);
      s = style_pop(&c->style, s);
    }
    break;
  }

  case NVar: {
    bool newfound = false;
    auto id = l_seen_id(c, n, &newfound);
    if (!newfound && nl->parent && nl->parent->n->kind != NFile) {
      // see comments in case NFun
      descend = false;
    }

    s = str_appendc(s, ' ');
    s = style_push(&c->style, s, NodeIsMacroParam(n) ? typeval_color : id_color);
    s = str_append(s, n->var.name, symlen(n->var.name));
    s = style_pop(&c->style, s);

    if (c->fl & NodeReprRefs) {
      s = style_push(&c->style, s, ref_color);
      s = str_appendfmt(s, " #%zu", (size_t)id);
      s = style_pop(&c->style, s);
    }
    break;
  }

  case NNamedVal: {
    s = str_appendc(s, ' ');
    s = style_push(&c->style, s, id_color);
    s = str_append(s, n->namedval.name, symlen(n->namedval.name));
    s = style_pop(&c->style, s);
    break;
  }

  case NPkg:
  case NFile: {
    if (n->cunit.name) {
      s = str_appendc(s, ' ');
      s = style_push(&c->style, s, id_color);
      s = str_append(s, n->cunit.name, strlen(n->cunit.name));
      s = style_pop(&c->style, s);
    }
    if (n->kind == NFile) {
      // allocate function ids up front to avoid expanding a referenced function inside a body
      // when the definition trails the use, syntactically.
      for (u32 i = 0; i < n->cunit.a.len; i++) {
        const Node* cn = n->cunit.a.v[i];
        while (cn->kind == NVar && cn->var.init) {
          l_seen_id(c, cn, NULL);
          cn = cn->var.init;
        }
        if (cn->kind == NFun)
          l_seen_id(c, cn, NULL);
      }
    }
    break;
  }

  case NSelector:
    s = str_appendc(s, ' ');
    s = style_push(&c->style, s, id_color);
    s = str_append(s, n->sel.member, symlen(n->sel.member));
    s = style_pop(&c->style, s);
    break;

  case NStructType: {
    bool newfound = false;
    auto id = l_seen_id(c, n, &newfound);
    if (!newfound)
      descend = false;
    if (n->t.struc.name) {
      s = str_appendc(s, ' ');
      s = style_push(&c->style, s, id_color);
      s = str_append(s, n->t.struc.name, symlen(n->t.struc.name));
      s = style_pop(&c->style, s);
    }
    if (c->fl & NodeReprRefs) {
      s = style_push(&c->style, s, ref_color);
      s = str_appendfmt(s, " #%zu", (size_t)id);
      s = style_pop(&c->style, s);
    }
    break;
  }

  default:
    break;
  } // switch(n->kind)

  // attributes
  if (c->fl & NodeReprAttrs) {
    if (n->flags) {
      s = style_push(&c->style, s, attr_color);

      if (NodeIsUnresolved(n))
        s = str_appendcstr(s, " @unres");

      if (NodeIsMacroParam(n)) {
        s = str_appendcstr(s, " @typeparam");
      } else if (NodeIsConst(n)) {
        s = str_appendcstr(s, " @const");
      }

      if ((n->flags & NodeFlagUnused))
        s = str_appendcstr(s, " @unused");

      if ((n->flags & NodeFlagPublic))
        s = str_appendcstr(s, " @pub");

      // if ((n->flags & NodeFlagHasConstVar))
      //   s = str_appendcstr(s, " @hascvar");

      s = style_pop(&c->style, s);
    }
    // pointer attr
    #ifdef DEBUG_INCLUDE_POINTERS
      s = style_push(&c->style, s, attr_color);
      s = str_appendfmt(s, " @ptr(%p)", n);
      s = style_pop(&c->style, s);
    #endif
  }

  // include fields and children
  if (descend) {
    c->s = s; // store s

    // fields
    l_append_fields(n, c);

    // visit children
    NodeVisitChildren(nl, cp, l_visit);

    s = c->s; // load s
  }

  // type
  if ((c->fl & NodeReprTypes) &&
      !NodeIsType(n) && n->kind != NTypeType && n->kind != NFile && n->kind != NPkg)
  {
    c->delim_open = c->langle;
    NodeList tnl = { .n = n->type, .parent = nl, .fieldname = "type" };
    s = style_push(&c->style, s, type_color);

    if (n->type && (!nl->parent || nl->parent->n->type != n->type)) {
      // print this type since it differs from the parent type
      c->s = s; // store s
      l_visit(&tnl, c);
      s = c->s; // load s
    } else {
      if (linestart != c->linestart) {
        s = l_new_line(c, s);
        s = append_indent(s, c->ind);
      } else {
        s = str_appendc(s, ' ');
      }
      s = append_open_delim(c, s);
      if (n->type) {
        // Type is same as parent type. (checked earlier)
        // To avoid repeat printing of type trees, print something short and symbolic.
        if (NodeIsMacroParam(n->type)) {
          asserteq_debug(n->type->kind, NVar);
          s = style_push(&c->style, s, typeval_color);
          s = str_append(s, n->type->var.name, symlen(n->type->var.name));
          s = style_pop(&c->style, s);
        } else {
          //s = str_appendcstr(s, l_listname(&tnl));
          s = str_appendcstr(s, "•••");
        }
      } else {
        // missing type "<?>"
        s = str_appendcstr(s, "?");
      }
      s = append_close_delim(c, s);
    }

    s = style_pop(&c->style, s);
  }

  // end list
  s = append_delim(c, s, delim_close);

end:
  while (numExtraEndParens) {
    s = append_delim(c, s, c->rparen);
    numExtraEndParens--;
  }

  c->typenest -= (u32)NodeIsType(n);
  c->delim_open = c->lparen;

  // end type color
  if (c->typenest == 0 && NodeIsType(n))
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
    s = str_append(s, n->id.name, symlen(n->id.name));
    s = style_pop(&c->style, s);
    break;

  case NVar:
    if (c->fl & NodeReprUseCount) {
      s = style_push(&c->style, s, ref_color);
      s = str_appendfmt(s, "(%u refs)", n->var.nrefs);
      s = style_pop(&c->style, s);
    }
    break;

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

  case NArrayType:
    if (n->t.array.size > 0) {
      s = str_appendu64(s, n->t.array.size, 10);
      break;
    }
    FALLTHROUGH;

  default:
    str_setlen(s, str_len(s) - 1); // undo ' '
    break;
  }
  c->s = s; // store s
  return;
}
