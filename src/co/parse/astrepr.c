#include "../common.h"
#include "../util/tstyle.h"
#include "../util/array.h"
#include "parse.h"

// DEBUG_INCLUDE_SCOPE: define to include "[scope ADDR]" in output
#define DEBUG_INCLUDE_SCOPE


typedef struct ReprCtx {
  int         ind;  // indentation level
  int         maxdepth;
  int         typenest; // >0 when formatting a type
  TStyleTable style;
  bool        pretty;
  bool        includeTypes;
  Array       funscope; // cycle guard
  Node*       funscope_storage[4];

  // style
  Array stylestack;
  Str   stylestack_storage[4];
  Str   nobg_color;
  Str   nofg_color;
  Str   id_color;
  Str   ptr_color;
  Str   type_color;
  Str   unimportant_color;
  Str   unresolved_color;
} ReprCtx;

static Str nodeRepr(const Node* n, Str s, ReprCtx* ctx, int depth);

Str NodeRepr(const Node* n, Str s) {
  auto mem = MemHeap;
  TStyleTable style = TStyle16;
  ReprCtx ctx = {
    .maxdepth     = 48,
    .pretty       = true,
    .includeTypes = true,
    .style        = style,
  };

  ArrayInitWithStorage(&ctx.funscope, ctx.funscope_storage, countof(ctx.funscope_storage));
  ArrayInitWithStorage(&ctx.stylestack, ctx.stylestack_storage, countof(ctx.stylestack_storage));

  auto bold_style = str_cpycstr(style[TStyle_bold]);
  if (style != TStyleNone) {
    ctx.nobg_color = str_cpycstr(style[TStyle_defaultbg]);
    ctx.nofg_color = str_cpycstr(style[TStyle_defaultfg]);
    ctx.id_color   = str_cpycstr(style[TStyle_orange]);
    ctx.ptr_color  = str_cpycstr(style[TStyle_red]);
    ctx.type_color = str_cpycstr(style[TStyle_blue]);
    // ctx.type_color = str_cpycstr("\x1b[44m");
    ctx.unimportant_color = str_cpycstr(style[TStyle_grey]);
    ctx.unresolved_color = str_cpycstr(style[TStyle_green]);
    ArrayPush(&ctx.stylestack, (void*)bold_style, mem);
  }

  s = nodeRepr(n, s, &ctx, /*depth*/ 1);

  ArrayFree(&ctx.funscope, mem);
  ArrayFree(&ctx.stylestack, mem);
  str_free(bold_style);
  str_free(ctx.nobg_color);
  str_free(ctx.nofg_color);
  str_free(ctx.id_color);
  str_free(ctx.ptr_color);
  str_free(ctx.type_color);
  str_free(ctx.unimportant_color);
  str_free(ctx.unresolved_color);
  return s;
}

static Str style_apply(ReprCtx* ctx, Str s) {
  if (ctx->stylestack.len == 0)
    return str_appendcstr(s, ctx->style[TStyle_none]);
  bool nofg = true;
  bool nobg = true;
  s = str_makeroom(s, ctx->stylestack.len * 8);
  for (u32 i = 0; i < ctx->stylestack.len; i++) {
    Str style = ctx->stylestack.v[i];
    s = str_append(s, style, str_len(style));
    if (style[2] == '3' || style[2] == '9') {
      nofg = false;
    } else if (style[2] == '4') {
      nobg = false;
    }
  }
  if (nofg)
    s = str_append(s, ctx->nofg_color, str_len(ctx->nofg_color));
  if (nobg)
    s = str_append(s, ctx->nobg_color, str_len(ctx->nobg_color));
  return s;
}

static Str style_push(ReprCtx* ctx, Str s, Str style) {
  if (ctx->style == TStyleNone)
    return s;
  ArrayPush(&ctx->stylestack, (void*)style, MemHeap);
  return style_apply(ctx, s);
}

static Str style_pop(ReprCtx* ctx, Str s) {
  ArrayPop(&ctx->stylestack);
  return style_apply(ctx, s);
}


// funscope_push adds n to ctx->funscope. Returns true if added.
static bool funscope_push(ReprCtx* ctx, const Node* n) {
  assert(n->kind == NFun);
  //dlog("funscope_push %s", fmtnode(n));
  if (ArrayIndexOf(&ctx->funscope, (void*)n) > -1)
    return false;
  ArrayPush(&ctx->funscope, (void*)n, MemHeap);
  return true;
}

// funscope_pop removes n from ctx->funscope
static void funscope_pop(ReprCtx* ctx) {
  ArrayPop(&ctx->funscope);
}


static Str indent(Str s, const ReprCtx* ctx) {
  if (ctx->ind > 0 && ctx->typenest == 0) {
    if (ctx->pretty) {
      s = str_append(s, "\n", 1);
      s = str_appendfill(s, ctx->ind, ' ');
    } else {
      s = str_append(s, " ", 1);
    }
  }
  return s;
}


static Str reprEmpty(Str s, const ReprCtx* ctx) {
  s = indent(s, ctx);
  return str_append(s, "()", 2);
}


#ifdef DEBUG_INCLUDE_SCOPE
static Scope* getScope(const Node* n) {
  switch (n->kind) {
    case NFile:
    case NPkg:
      return n->array.scope;
    default:
      return NULL;
  }
}
#endif /*DEBUG_INCLUDE_SCOPE*/


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
    dlog("TODO NValFmt");
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


static Str nodeRepr(const Node* n, Str s, ReprCtx* ctx, int depth) {
  if (n == NULL)
    return str_append(s, "(null)", 6);

  // dlog("nodeRepr %s", NodeKindName(n->kind));
  // if (n->kind == NId) {
  //   dlog("  addr:   %p", n);
  //   dlog("  name:   %s", n->ref.name);
  //   if (n->ref.target == NULL) {
  //     dlog("  target: <null>");
  //   } else {
  //     dlog("  target:");
  //     dlog("    addr:   %p", n->ref.target);
  //     dlog("    kind:   %s", NodeKindName(n->ref.target->kind));
  //   }
  // }

  // s = str_appendfmt(s, "[%p] ", n);

  if (depth > ctx->maxdepth) {
    s = style_push(ctx, s, ctx->unimportant_color);
    s = str_appendcstr(s, "...");
    s = style_pop(ctx, s);
    return s;
  }

  bool isType = NodeKindIsType(n->kind);
  if (!isType) {
    s = indent(s, ctx);

    // add type prefix
    if (n->kind != NPkg && n->kind != NFile && ctx->includeTypes && ctx->typenest == 0) {
      s = style_push(ctx, s, ctx->type_color);
      if (n->type) {
        s = nodeRepr(n->type, s, ctx, depth + 1);
        s = str_appendcstr(s, ":");
      } else {
        s = str_appendcstr(s, "?:");
      }
      s = style_pop(ctx, s);
    }

    s = str_appendfmt(s, "(%s ", NodeKindName(n->kind));
  }

  if (NodeIsUnresolved(n)) {
    s = style_push(ctx, s, ctx->unresolved_color);
    s = str_appendcstr(s, "\xE2\x8B\xAF "); // "⋯" U+22EF
    s = style_pop(ctx, s);
  }

  ctx->ind += 2;

  #ifdef DEBUG_INCLUDE_SCOPE
  auto scope = getScope(n);
  if (scope)
    s = str_appendfmt(s, "[scope %p] ", scope);
  #endif /*DEBUG_INCLUDE_SCOPE*/

  switch (n->kind) {

  // does not use u
  case NBad:
  case NNone:
  case NNil:
  case NZeroInit:
    str_setlen(s, str_len(s) - 1); // trim away trailing " " from s
    break;

  // uses u.integer
  case NIntLit:
    s = str_appendfmt(s, FMT_U64, n->val.i);
    break;

  case NBoolLit:
    if (n->val.i == 0) {
      s = str_appendcstr(s, "false");
    } else {
      s = str_appendcstr(s, "true");
    }
    break;

  // uses u.real
  case NFloatLit:
    s = str_appendfmt(s, "%f", n->val.f);
    break;

  // uses u.str
  case NComment:
    s = str_appendrepr(s, (const char*)n->str.ptr, n->str.len);
    break;

  // uses u.ref
  case NId:
    assert(n->ref.name != NULL);
    if (isType && n->ref.target) {
      s = nodeRepr(n->ref.target, s, ctx, depth);
      break;
    }
    s = style_push(ctx, s, ctx->id_color);
    s = str_append(s, n->ref.name, symlen(n->ref.name));
    s = style_pop(ctx, s);
    if (n->ref.target) {
      if (n->ref.target->kind == NFun) {
        auto f = n->ref.target;
        s = str_appendfmt(s, " @(Fun %s %s%p%s)",
          f->fun.name ? f->fun.name : "_", ctx->ptr_color, f, ctx->nofg_color);
      } else {
        s = str_appendfmt(s, " @%s", NodeKindName(n->ref.target->kind));
      }
      // s = nodeRepr(n->ref.target, s, ctx, depth + 1);
    }
    break;

  // uses u.op
  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
  case NAssign:
  case NReturn:
    if (n->op.op != TNone) {
      s = str_appendcstr(s, TokName(n->op.op));
      s = str_append(s, " ", 1);
    }
    s = nodeRepr(n->op.left, s, ctx, depth + 1);
    if (n->op.right) {
      s = nodeRepr(n->op.right, s, ctx, depth + 1);
    }
    break;

  // uses u.array
  case NBlock:
  case NTuple:
  case NFile:
  case NPkg:
  {
    // sdssetlen(s, str_len(s)-1); // trim away trailing " " from s
    for (u32 i = 0; i < n->array.a.len; i++) {
      s = nodeRepr(n->array.a.v[i], s, ctx, depth + 1);
      // TODO: detect if line breaks were added.
      // I.e. (Tuple int int) is currently printed as "(Tuple intint)" (missing space).
    }
    break;
  }

  // uses u.field
  case NLet:
  case NArg:
  case NField:
  {
    if (n->kind == NArg) {
      s = str_appendfmt(s, "#%u ", n->field.index);
    }
    if (n->field.name) {
      s = style_push(ctx, s, ctx->id_color);
      s = str_append(s, n->field.name, symlen(n->field.name));
      s = style_pop(ctx, s);
    } else {
      s = str_appendc(s, '_');
    }
    if (n->field.init) {
      if (NodeKindIsType(n->field.init->kind))
        s = str_appendc(s, ' ');
      s = nodeRepr(n->field.init, s, ctx, depth + 1);
    }
    break;
  }

  // uses u.fun
  case NFun: {
    // cycle guard for functions which may refer to themselves.
    // TODO: include structs and complex types here in the future.
    if (!funscope_push(ctx, n)) {
      s = str_appendfmt(s, "[cycle %s %s%p%s]",
        NodeKindName(n->kind), ctx->ptr_color, n, ctx->nofg_color);
      return s;
    }
    auto f = &n->fun;

    if (f->name) {
      s = style_push(ctx, s, ctx->id_color);
      s = str_append(s, f->name, symlen(f->name));
      s = style_pop(ctx, s);
    } else {
      s = str_append(s, "_", 1);
    }

    // include address
    s = str_appendfmt(s, " %s%p%s", ctx->ptr_color, n, ctx->nofg_color);

    if (f->params) {
      s = nodeRepr(f->params, s, ctx, depth + 1);
    } else {
      s = reprEmpty(s, ctx);
    }

    s = str_appendcstr(s, " -> ");

    if (f->result) {
      s = nodeRepr(f->result, s, ctx, depth + 1);
    } else {
      s = reprEmpty(s, ctx);
    }

    if (f->body) {
      s = nodeRepr(f->body, s, ctx, depth + 1);
    }

    funscope_pop(ctx);
    break;
  }

  // uses u.call
  case NTypeCast: // TODO
  case NCall: {
    auto recv = n->call.receiver;

    // const Node* funTarget = (
    //   recv->kind == NFun ? recv :
    //   (recv->kind == NId && recv->ref.target != NULL && recv->ref.target->kind == NFun) ?
    //     recv->ref.target :
    //   NULL
    // );

    s = nodeRepr(recv, s, ctx, depth + 1);
    // if (funTarget) {
    //   //
    // } else if (recv->kind == NId && recv->ref.target == NULL) {
    //   // when the receiver is an ident without a resolved target, print its name
    //   s = str_append(s, recv->ref.name, symlen(recv->ref.name));
    // } else {
    //   s = nodeRepr(recv, s, ctx, depth + 1);
    // }

    s = nodeRepr(n->call.args, s, ctx, depth + 1);
    break;
  }

  // uses u.cond
  case NIf:
    s = nodeRepr(n->cond.cond, s, ctx, depth + 1);
    s = nodeRepr(n->cond.thenb, s, ctx, depth + 1);
    if (n->cond.elseb) {
      s = nodeRepr(n->cond.elseb, s, ctx, depth + 1);
    }
    break;

  case NBasicType:
    ctx->typenest++;
    s = style_push(ctx, s, ctx->type_color);
    if (n == Type_ideal) {
      s = str_appendc(s, '*');
    } else {
      s = str_append(s, n->t.basic.name, symlen(n->t.basic.name));
    }
    s = style_pop(ctx, s);
    ctx->typenest--;
    break;

  case NFunType:
    ctx->typenest++;
    if (n->t.fun.params) {
      s = nodeRepr(n->t.fun.params, s, ctx, depth + 1);
    } else {
      s = str_appendcstr(s, "()");
    }
    s = str_appendcstr(s, "->");
    if (n->t.fun.result) {
      s = nodeRepr(n->t.fun.result, s, ctx, depth + 1);
    } else {
      s = str_appendcstr(s, "()");
    }
    ctx->typenest--;
    break;

  // uses u.t.list
  case NTupleType: {
    ctx->typenest++;
    s = str_appendc(s, '(');
    bool first = true;
    for (u32 i = 0; i < n->t.list.a.len; i++) {
      Node* cn = n->t.list.a.v[i];
      if (first) {
        first = false;
      } else {
        s = str_appendc(s, ' ');
      }
      s = nodeRepr(cn, s, ctx, depth + 1);
    }
    s = str_appendc(s, ')');
    ctx->typenest--;
    break;
  }

  // uses u.t.array
  case NArrayType: {
    ctx->typenest++;
    if (n->t.array.sizeExpr == NULL) {
      s = str_appendcstr(s, "[]");
    } else {
      if (n->t.array.size != 0) {
        // size is known
        s = str_appendfmt(s, "[" FMT_U64 "]", n->t.array.size);
      } else {
        s = str_appendc(s, '[');
        s = nodeRepr(n->t.array.sizeExpr, s, ctx, depth + 1);
        s = str_appendc(s, ']');
      }
    }
    s = nodeRepr(n->t.array.subtype, s, ctx, depth + 1);
    ctx->typenest--;
    break;
  }

  // TODO
  case NStrLit:
    panic("TODO %s", NodeKindName(n->kind));

  case _NodeKindMax: break;
  // Note: No default case, so that the compiler warns us about missing cases.

  }

  ctx->ind -= 2;

  if (!isType) {
    s = str_append(s, ")", 1);
  }
  return s;
}


static Str str_append_NodeArray(Str s, const NodeArray* na) {
  bool isFirst = true;
  for (u32 i = 0; i < na->len; i++) {
    Node* n = na->v[i];
    if (isFirst) {
      isFirst = false;
    } else {
      s = str_appendc(s, ' ');
    }
    s = str_append_astnode(s, n);
  }
  return s;
}


// appends a short representation of an AST node to s, suitable for use in error messages.
Str str_append_astnode(Str s, const Node* n) {
  // Note: Do not include type information.
  // Instead, in use sites, call fmtnode individually for n->type when needed.

  if (n == NULL)
    return str_appendcstr(s, "<null>");

  switch (n->kind) {

  // uses no extra data
  case NNil: // nil
    return str_appendcstr(s, "nil");

  case NZeroInit: // init
    return str_appendcstr(s, "init");

  case NBoolLit: // true | false
    return str_appendcstr(s, n->val.i == 0 ? "false" : "true");

  case NIntLit: // 123
    return str_appendfmt(s, FMT_U64, n->val.i);

  case NFloatLit: // 12.3
    return str_appendfmt(s, "%f", n->val.f);

  case NComment: // #"comment"
    s = str_appendcstr(s, "#\"");
    s = str_appendrepr(s, (const char*)n->str.ptr, n->str.len);
    return str_appendc(s, '"');

  case NId: // foo
    return str_append(s, n->ref.name, symlen(n->ref.name));

  case NBinOp: // foo + bar
    s = str_append_astnode(s, n->op.left);
    s = str_appendc(s, ' ');
    s = str_appendcstr(s, TokName(n->op.op));
    s = str_appendc(s, ' ');
    return str_append_astnode(s, n->op.right);

  case NPostfixOp: // foo++
    s = str_append_astnode(s, n->op.left);
    return str_appendcstr(s, TokName(n->op.op));

  case NPrefixOp: // -foo
    s = str_appendcstr(s, TokName(n->op.op));
    return str_append_astnode(s, n->op.left); // note: prefix op uses left, not right.

  case NAssign: // thing=
    s = str_append_astnode(s, n->op.left);
    return str_appendc(s, '=');

  case NReturn: // return thing
    s = str_appendcstr(s, "return ");
    return str_append_astnode(s, n->op.left);

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
    return str_appendfmt(s, "let %s", n->field.name);

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
    s = str_append_astnode(s, n->call.receiver);
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
      s = str_append_astnode(s, n->t.fun.params);
    }
    s = str_appendcstr(s, "->");
    return str_append_astnode(s, n->t.fun.result); // ok if NULL

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


ConstStr fmtnode(const Node* n) {
  auto s = str_append_astnode(str_new(16), n);
  // TODO memgcsds(s)
  return s; // return memgcsds(s); // GC
}


ConstStr fmtast(const Node* n) {
  auto s = NodeRepr(n, str_new(16));
  // TODO memgcsds(s)
  return s; // return memgcsds(s); // GC
}
