#include <rbase/rbase.h>
#include "parse.h"
#include "../util/tstyle.h"
#include "../util/array.h"


typedef struct ReprCtx {
  int         ind;  // indentation level
  int         maxdepth;
  TStyleTable style;
  bool        pretty;
  bool        includeTypes;
  Array       seen; // cycle guard
} ReprCtx;


// seen_add adds n to ctx->seen. Returns true if added, false if already in ctx->seen
static bool ctx_seen_add(ReprCtx* ctx, const Node* n) {
  if (n->kind != NFun)
    return true;
  //dlog("seen_add %s", fmtnode(n));
  if (ArrayIndexOf(&ctx->seen, (void*)n) > -1)
    return false; // already in ctx->seen
  ArrayPush(&ctx->seen, (void*)n, NULL);
  return true;
}

// seen_rm removes n from ctx->seen
static void ctx_seen_rm(ReprCtx* ctx, const Node* n) {
  if (n->kind != NFun)
    return;
  auto i = ArrayLastIndexOf(&ctx->seen, (void*)n); // last since most likely last
  assert(i > -1);
}


static Str indent(Str s, const ReprCtx* ctx) {
  if (ctx->ind > 0) {
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


// static Scope* getScope(const Node* n) {
//   switch (n->kind) {
//     case NBlock:
//     case NTuple:
//     case NFile:
//       return n->array.scope;
//     case NFun:
//       return n->fun.scope;
//     default:
//       return NULL;
//   }
// }


Str NValFmt(Str s, const NVal* v) {
  switch (v->ct) {

  case CType_int:
    if (v->i > 0x7fffffffffffffff) {
      return str_appendfmt(s, "%llu", v->i);
    } else {
      return str_appendfmt(s, "%lld", (i64)v->i);
    }

  case CType_rune:
  case CType_float:
  case CType_str:
    dlog("TODO NValFmt");
    break;

  case CType_bool:
    return str_appendcstr(s, v->i == 0 ? "false" : "true");

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
    s = str_appendcstr(s, ctx->style[TStyle_grey]);
    s = str_appendcstr(s, "...");
    s = str_appendcstr(s, ctx->style[TStyle_nocolor]);
    return s;
  }

  // cycle guard
  if (!ctx_seen_add(ctx, n)) {
    s = str_appendfmt(s, " [cyclic %s]", NodeKindName(n->kind));
    return s;
  }

  auto isType = NodeKindIsType(n->kind);
  if (!isType) {
    s = indent(s, ctx);

    if (n->kind != NPkg && n->kind != NFile && ctx->includeTypes) {
      s = str_appendcstr(s, ctx->style[TStyle_blue]);
      if (n->type) {
        s = nodeRepr(n->type, s, ctx, depth + 1);
        s = str_appendcstr(s, ctx->style[TStyle_blue]); // in case nodeRepr changed color
        s = str_append(s, ":", 1);
      } else {
        s = str_append(s, "?:", 2);
      }
      s = str_appendcstr(s, ctx->style[TStyle_nocolor]);
    }
    s = str_appendfmt(s, "(%s ", NodeKindName(n->kind));
  }

  ctx->ind += 2;

  // auto scope = getScope(n);
  // if (scope) {
  //   s = str_appendfmt(s, "[scope %p] ", scope);
  // }

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
    s = str_appendfmt(s, "%llu", n->val.i);
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
    s = str_appendcstr(s, ctx->style[TStyle_red]);
    assert(n->ref.name != NULL);
    s = str_append(s, n->ref.name, symlen(n->ref.name));
    s = str_appendcstr(s, ctx->style[TStyle_nocolor]);
    if (n->ref.target) {
      s = str_appendfmt(s, " @%s", NodeKindName(n->ref.target->kind));
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
      s = str_append(s, n->field.name, symlen(n->field.name));
    } else {
      s = str_append(s, "_", 1);
    }
    if (n->field.init != NULL) {
      s = nodeRepr(n->field.init, s, ctx, depth + 1);
    }
    break;
  }

  // uses u.fun
  case NFun: {
    auto f = &n->fun;

    if (f->name) {
      s = str_append(s, f->name, symlen(f->name));
    } else {
      s = str_append(s, "_", 1);
    }

    // include address
    s = str_appendcstr(s, ctx->style[TStyle_red]);
    s = str_appendfmt(s, " %p", n);
    s = str_appendcstr(s, ctx->style[TStyle_nocolor]);

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

    break;
  }

  // uses u.call
  case NTypeCast: // TODO
  case NCall: {
    auto recv = n->call.receiver;

    const Node* funTarget = (
      recv->kind == NFun ? recv :
      (recv->kind == NId && recv->ref.target != NULL && recv->ref.target->kind == NFun) ?
        recv->ref.target :
      NULL
    );

    if (funTarget != NULL) {
      // print receiver function when we know it
      if (funTarget->fun.name) {
        s = str_append(s, funTarget->fun.name, symlen(funTarget->fun.name));
      } else {
        s = str_append(s, "_", 1);
      }
      s = str_appendcstr(s, ctx->style[TStyle_red]);
      s = str_appendfmt(s, " %p", funTarget);
      s = str_appendcstr(s, ctx->style[TStyle_nocolor]);
    } else if (recv->kind == NId && recv->ref.target == NULL) {
      // when the receiver is an ident without a resolved target, print its name
      s = str_append(s, recv->ref.name, symlen(recv->ref.name));
    } else {
      s = nodeRepr(recv, s, ctx, depth + 1);
    }

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
    s = str_appendcstr(s, ctx->style[TStyle_blue]);
    if (n == Type_ideal) {
      s = str_appendc(s, '*');
    } else {
      s = str_append(s, n->t.basic.name, symlen(n->t.basic.name));
    }
    s = str_appendcstr(s, ctx->style[TStyle_nocolor]);
    break;

  case NFunType: // TODO
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
    s = str_appendcstr(s, " <");
    if (n->t.id)
      s = str_append(s, n->t.id, symlen(n->t.id));
    s = str_appendcstr(s, ">");
    break;

  // uses u.t.tuple
  case NTupleType: {
    s = str_append(s, "(", 1);
    bool first = true;
    for (u32 i = 0; i < n->t.array.a.len; i++) {
      Node* cn = n->t.array.a.v[i];
      if (first) {
        first = false;
      } else {
        s = str_append(s, " ", 1);
      }
      s = nodeRepr(cn, s, ctx, depth + 1);
    }
    s = str_append(s, ")", 1);
    break;
  }

  case _NodeKindMax: break;
  // Note: No default case, so that the compiler warns us about missing cases.

  }

  ctx->ind -= 2;
  ctx_seen_rm(ctx, n);

  if (!isType) {
    s = str_append(s, ")", 1);
  }
  return s;
}


Str NodeRepr(const Node* n, Str s) {
  ReprCtx ctx = {
    .maxdepth     = 48,
    .pretty       = true,
    .includeTypes = true,
    .style        = TStyle16,
  };
  void* seen_storage[16];
  ArrayInitWithStorage(&ctx.seen, seen_storage, countof(seen_storage));
  s = nodeRepr(n, s, &ctx, /*depth*/ 1);
  ArrayFree(&ctx.seen, NULL);
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
    return str_appendfmt(s, "%llu", n->val.i);

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
    s = str_appendc(s, '{');
    s = str_append_astnode(s, n->type);
    return str_appendc(s, '}');

  case NTuple: { // (one two 3)
    s = str_appendc(s, '(');
    s = str_append_NodeArray(s, &n->array.a);
    return str_appendc(s, ')');
  }

  case NPkg: // pkg
    return str_appendcstr(s, "pkg");

  case NFile: // file
    return str_appendcstr(s, "file");

  case NLet: // let
    return str_appendfmt(s, "let %s", n->field.name);

  case NArg: // foo
    return str_append(s, n->field.name, symlen(n->field.name));

  case NFun: // fun foo
    if (n->fun.name == NULL)
      return str_appendcstr(s, "fun _");
    return str_appendfmt(s, "fun %s", n->fun.name);

  case NTypeCast: // typecast<int16>
    s = str_appendcstr(s, "typecast<");
    s = str_append_astnode(s, n->call.receiver);
    return str_appendc(s, '>');

  case NCall: // call foo
    s = str_appendcstr(s, "call ");
    return str_append_astnode(s, n->call.receiver);

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
    s = str_append_NodeArray(s, &n->t.array.a);
    return str_appendc(s, ')');

  // The remaining types are not expected to appear. Use their kind if they do.
  case NBad:
  case NNone:
  case NField: // field is not yet implemented by parser
    return str_appendcstr(s, NodeKindName(n->kind));

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
