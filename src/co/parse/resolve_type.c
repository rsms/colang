// Resolve types in an AST. Usuaully run after Parse() and ResolveSym()
#include <rbase/rbase.h>
#include "parse.h"
#include "../util/array.h"

ASSUME_NONNULL_BEGIN

// DEBUG_MODULE: define to enable trace logging
#define DEBUG_MODULE ""

#ifdef DEBUG_MODULE
  #define dlog_mod(format, ...) \
    fprintf(stderr, DEBUG_MODULE "%*s " format "\n", ctx->debug_depth*2, "", ##__VA_ARGS__)
#else
  #define dlog_mod(...) do{}while(0)
#endif


typedef enum RFlag {
  RFlagNone = 0,
  RFlagExplicitTypeCast = 1 << 0,
  RFlagResolveIdeal     = 1 << 1,  // set when resolving inside resolve_ideal_type
} RFlag;


typedef struct {
  Build* build;

  // typecontext is the "expected" type, if any.
  // E.g. the type of a var while resolving its rvalue.
  Type* nullable typecontext;       // current value; top of typecontext_stack
  Array          typecontext_stack; // stack of Type* which are all of NodeClassType
  Type*          typecontext_storage[32];

  bool explicitTypeCast;

  #ifdef DEBUG_MODULE
  int debug_depth;
  #endif
} ResCtx;


static Type* resolve_type(ResCtx* ctx, Node* n, RFlag fl);

Node* ResolveType(Build* build, Node* n) {
  // setup ResCtx
  ResCtx ctx = {
    .build = build,
  };
  ArrayInitWithStorage(
    &ctx.typecontext_stack,
    ctx.typecontext_storage,
    countof(ctx.typecontext_storage)
  );
  n = resolve_type(&ctx, n, RFlagNone);
  ArrayFree(&ctx.typecontext_stack, build->mem);
  return n;
}

static void typecontext_push(ResCtx* ctx, Type* t) {
  assert(NodeIsType(t));
  assertne(t, Type_ideal);
  dlog_mod("typecontext_push %s", fmtnode(t));
  if (ctx->typecontext)
    ArrayPush(&ctx->typecontext_stack, ctx->typecontext, ctx->build->mem);
  ctx->typecontext = t;
}

static void typecontext_pop(ResCtx* ctx) {
  assertnotnull(ctx->typecontext);
  dlog_mod("typecontext_pop %s", fmtnode(ctx->typecontext));
  if (ctx->typecontext_stack.len == 0) {
    ctx->typecontext = NULL;
  } else {
    ctx->typecontext = (Type*)ArrayPop(&ctx->typecontext_stack);
  }
}


// RESOLVE_ARRAY_NODE_TYPE_MUT is a helper macro for applying resolve_type on an array element.
// Node* RESOLVE_ARRAY_NODE_TYPE_MUT(NodeArray* a, u32 index)
#define RESOLVE_ARRAY_NODE_TYPE_MUT(a, index, flags) ({ \
  __typeof__(index) idx__ = (index);                    \
  Node* cn = (Node*)(a)->v[idx__];                      \
  cn = resolve_type(ctx, cn, (flags));                  \
  (a)->v[idx__] = cn;                                   \
  cn;                                                   \
})


// resolve_ideal_type resolves the concrete type of n. If typecontext is provided, convlit is
// used to "fit" n into that type. Otherwise the natural concrete type of n is used. (e.g. int)
// n is assumed to be Type_ideal and must be a node->kind = NIntLit | NFloatLit | NLet | NId.
//
static Node* resolve_ideal_type(
  ResCtx* nonull   ctx,
  Node*   nonull   n,
  Node*   nullable typecontext,
  RFlag            fl
) {
  // lower ideal types in all cases but NLet
  dlog_mod("resolve_ideal_type node %s to typecontext %s", fmtnode(n), fmtnode(typecontext));
  assert(typecontext == NULL || typecontext->kind == NBasicType);

  // It's really only constant literals which are actually of ideal type, so switch on those
  // and lower CType to concrete type.
  // In case n is not a constant literal, we simply continue as the AST at n is a compound
  // which contains one or more untyped constants. I.e. continue to traverse AST.

  switch (n->kind) {
    case NIntLit:
    case NFloatLit: {
      if (typecontext) {
        bool explicit_cast = fl & RFlagExplicitTypeCast;
        return convlit(ctx->build, n, typecontext, explicit_cast);
      }
      // no type context; resolve to best effort based on value
      Node* n2 = NodeCopy(ctx->build->mem, n);
      n2->type = IdealType(n->val.ct);
      return n2;
    }

    case NLet: {
      assert(n->field.init != NULL);
      Node* init2 = resolve_ideal_type(ctx, n->field.init, typecontext, fl);
      if (init2 != n->field.init) {
        Node* n2 = NodeCopy(ctx->build->mem, n);
        n2->field.init = init2;
        n = n2;
      }
      n->type = init2->type;
      break;
    }

    case NId:
      assert(n->ref.target != NULL);
      n->ref.target = resolve_ideal_type(ctx, n->ref.target, typecontext, fl);
      n->type = n->ref.target->type;
      break;

    default:
      // IMPORTANT: This relies on resolve_type to only call resolve_ideal_type for constants.
      // If this is not the case, this would create an infinite loop in some cases.
      panic("unexpected node type %s", NodeKindName(n->kind));
      break;
  }
  return n;
}


static Node* resolve_fun_type(ResCtx* ctx, Node* n, RFlag fl) {
  Type* ft = NewNode(ctx->build->mem, NFunType);

  // Important: To avoid an infinite loop when resolving a function which calls itself,
  // we set the unfinished function type objects ahead of calling resolve.
  n->type = ft;

  if (n->fun.params) {
    resolve_type(ctx, n->fun.params, fl);
    ft->t.fun.params = (Type*)n->fun.params->type;
  }

  if (n->fun.result) {
    n->fun.result = resolve_type(ctx, n->fun.result, fl);
    ft->t.fun.result = n->fun.result->type;
  }

  if (n->fun.body) {
    n->fun.body = resolve_type(ctx, n->fun.body, fl);
    auto bodyType = n->fun.body->type;
    if (ft->t.fun.result == NULL) {
      ft->t.fun.result = bodyType;
    } else if (R_UNLIKELY(
      ft->t.fun.result != Type_nil &&
      !TypeEquals(ctx->build, ft->t.fun.result, bodyType) ))
    {
      // function prototype claims to return type A while the body yields type B
      build_errf(ctx->build, n->fun.body->pos, NodeEndPos(n->fun.body),
        "cannot use type %s as return type %s",
        fmtnode(bodyType), fmtnode(ft->t.fun.result));
    }
  }

  // make sure its type id is set as codegen relies on this
  if (!ft->t.id)
    ft->t.id = GetTypeID(ctx->build, ft);

  n->type = ft;
  return n;
}


static Node* resolve_block_type(ResCtx* ctx, Node* n, RFlag fl) { // n->kind==NBlock
  // The type of a block is the type of the last expression.
  // Resolve each entry of the block:
  if (n->array.a.len == 0) {
    n->type = Type_nil;
  } else {
    // resolve all but the last expression without requiring ideal-type resolution
    u32 lasti = n->array.a.len - 1;
    for (u32 i = 0; i < lasti; i++) {
      Node* cn = RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
      if (R_UNLIKELY(cn->type == Type_ideal && NodeIsConst(cn))) {
        // an unused constant expression, e.g.
        //   { 1  # <- warning: unused expression 1
        //     2
        //   }
        build_warnf(ctx->build, cn->pos, NodeEndPos(cn), "unused expression: %s", fmtnode(cn));
      }
    }
    // Last node, in which case we set the flag to resolve literals
    // so that implicit return values gets properly typed.
    // This also becomes the type of the block.
    Node* cn = RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, lasti, fl | RFlagResolveIdeal);
    n->type = cn->type;
  }
  return n;
}


static Node* resolve_tuple_type(ResCtx* ctx, Node* n, RFlag fl) { // n->kind==NTuple
  Type* tt = NewNode(ctx->build->mem, NTupleType);

  // typecontext
  Type* ct = ctx->typecontext;
  u32 ctindex = 0;
  if (ct) {
    if (R_UNLIKELY(ct->kind != NTupleType)) {
      build_errf(ctx->build, n->pos, NodeEndPos(n),
        "tuple where %s is expected", fmtnode(ct));
    } else if (R_UNLIKELY(ct->array.a.len != n->array.a.len)) {
      build_errf(ctx->build, n->pos, NodeEndPos(n),
        "%u expressions where %u expressions are expected %s",
        n->array.a.len, ct->array.a.len, fmtnode(ct));
    }
    assert(ct->array.a.len > 0); // tuples should never be empty
    ctx->typecontext = ct->array.a.v[ctindex++];
  }

  // for each tuple entry
  for (u32 i = 0; i < n->array.a.len; i++) {
    Node* cn = RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
    if (R_UNLIKELY(!cn->type)) {
      cn->type = (Node*)NodeBad;
      build_errf(ctx->build, cn->pos, NodeEndPos(cn), "unknown type");
    }
    NodeArrayAppend(ctx->build->mem, &tt->t.array.a, cn->type);
    if (ct)
      ctx->typecontext = ct->array.a.v[ctindex++];
  }

  // restore typecontext, set type and return type
  ctx->typecontext = ct;
  n->type = tt;
  return n;
}


static Node* resolve_binop_or_assign_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n->op.right != NULL);

  Type* lt = NULL;
  Type* rt = NULL;

  // This is a bit of a mess, but what's going on here is making sure that untyped
  // operands are requested to become the type of typed operands.
  // For example:
  //   x = 3 as int64
  //   y = x + 2
  // Parses to:
  //   int64:(Let x int64:(IntLit 3))
  //   ?:(Let y ?:(BinOp "+"
  //                ?:(Id x)
  //                *:(IntLit 2)))
  // If we were to simply resolve types by visiting the two operands without requesting
  // a type, we'd get mixed types, specifically the untyped constant 2 is int, not int64:
  //   ...        (BinOp "+"
  //                int64:(Id x)
  //                int:(IntLit 2)))
  // To remedy this, we check operands. When one is untyped and the other is not, we first
  // resolve the operand with a concrete type, then set that type as the requested type and
  // finally we resolve the other, untyped, operand in the context of the requested type.
  //
  // fl & ~RFlagResolveIdeal = clear "resolve ideal" flag
  n->op.left = resolve_type(ctx, n->op.left, fl & ~RFlagResolveIdeal);
  n->op.right = resolve_type(ctx, n->op.right, fl & ~RFlagResolveIdeal);
  lt = n->op.left->type;
  rt = n->op.right->type;
  //
  // convert operand types as needed. The following code tests all branches:
  //
  //   a = 1 + 2                         # 1  left & right are untyped
  //   a = 2 + (1 as uint32)             # 2  left is untyped, right is typed
  //   a = (1 as uint32) + 2             # 3  left is typed, right is untyped
  //   a = (1 as uint32) + (2 as uint32) # 4  left & right are typed
  //
  if (lt == Type_ideal) {
    if (rt == Type_ideal) {
      dlog_mod("[binop] 1  left & right are untyped");
      // TODO: we could pick the strongest type here by finding the CType of each operand
      // and then calling resolve_ideal_type on the stronger of the two. For example float > int.
      n->op.left = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      lt = n->op.left->type;
      // note: continue to statement outside these if blocks
    } else {
      dlog_mod("[binop] 2  left is untyped, right is typed (%s)", fmtnode(rt));
      n->op.left = ConvlitImplicit(ctx->build, n->op.left, rt);
      n->type = rt;
      return n;
    }
  } else if (rt == Type_ideal) {
    dlog_mod("[binop] 3  left is typed (%s), right is untyped", fmtnode(lt));
    n->op.right = ConvlitImplicit(ctx->build, n->op.right, lt);
    n->type = lt;
    return n;
  } else {
    dlog_mod("[binop] 4  left & right are typed (%s, %s)", fmtnode(lt) , fmtnode(rt));
  }

  // we get here from either of the two conditions:
  // - left & right are both untyped (lhs has been resolved, above)
  // - left & right are both typed
  if (!TypeEquals(ctx->build, lt, rt)) {
    n->op.right = ConvlitImplicit(ctx->build, n->op.right, lt);

    if (R_UNLIKELY(!TypeEquals(ctx->build, lt, rt))) {
      build_errf(ctx->build, n->op.left->pos, n->op.right->pos,
        "invalid operation: %s (mismatched types %s and %s)",
        fmtnode(n), fmtnode(lt), fmtnode(rt));
      if (lt->kind == NBasicType) {
        // suggest type cast: x + (y as int)
        build_infof(ctx->build, n->op.right->pos, NodeEndPos(n->op.right),
          "try a type cast: %s %s (%s as %s)",
          fmtnode(n->op.left), TokName(n->op.op), fmtnode(n->op.right), fmtnode(lt));
      }
    }
  }

  n->type = lt;
  return n;
}


static Node* resolve_typecast_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n->call.receiver != NULL);

  if (R_UNLIKELY(!NodeKindIsType(n->call.receiver->kind))) {
    build_errf(ctx->build, n->pos, NodeEndPos(n),
      "invalid conversion to non-type %s", fmtnode(n->call.receiver));
    n->type = Type_nil;
    return n;
  }

  // Note: n->call.receiver is a Type, not a regular Node (see check above)
  n->call.receiver = resolve_type(ctx, n->call.receiver, fl);
  n->type = n->call.receiver;
  typecontext_push(ctx, n->type);

  n->call.args = resolve_type(ctx, n->call.args, fl | RFlagExplicitTypeCast);
  assert(n->call.args->type);

  if (TypeEquals(ctx->build, n->call.args->type, n->type)) {
    // source type == target type -- eliminate type cast.
    // The IR builder relies on this and will fail if a type conversion is a noop.
    n = n->call.args;
  } else {
    // attempt conversion to eliminate type cast
    n->call.args = ConvlitExplicit(ctx->build, n->call.args, n->call.receiver);
    if (TypeEquals(ctx->build, n->call.args->type, n->type)) {
      // source type == target type -- eliminate type cast
      n = n->call.args;
    }
  }

  typecontext_pop(ctx);
  return n;
}


static Node* resolve_call_type(ResCtx* ctx, Node* n, RFlag fl) {
  // Note: resolve_fun_type breaks handles cycles where a function calls itself,
  // making this safe (i.e. will not cause an infinite loop.)
  n->call.receiver = resolve_type(ctx, n->call.receiver, fl);
  auto ft = n->call.receiver->type;
  assert(ft != NULL);

  if (R_UNLIKELY(ft->kind != NFunType)) {
    build_errf(ctx->build, n->pos, n->call.receiver->pos,
      "cannot call %s", fmtnode(n->call.receiver));
    n->type = Type_nil;
    return n;
  }

  dlog_mod("resolve_call_type ft: %s", fmtnode(ft));

  // add parameter types to the "requested type" stack
  typecontext_push(ctx, ft->t.fun.params);

  // input arguments, in context of receiver parameters
  n->call.args = resolve_type(ctx, n->call.args, fl);
  auto argstype = n->call.args->type;
  dlog_mod("resolve_call_type argstype: %s", fmtnode(argstype));

  // pop parameter types off of the "requested type" stack
  typecontext_push(ctx, ft->t.fun.params);

  // Note: Consider arguments with defaults:
  // fun foo(a, b int, c int = 0)
  // foo(1, 2) == foo(1, 2, 0)
  if (R_UNLIKELY(!TypeEquals(ctx->build, ft->t.fun.params, argstype))) {
    build_errf(ctx->build, n->call.args->pos, NodeEndPos(n->call.args),
      "incompatible arguments %s in function call; expected %s",
      fmtnode(argstype), fmtnode(ft->t.fun.params));
  }
  n->type = ft->t.fun.result;
  return n;
}


static Node* resolve_if_type(ResCtx* ctx, Node* n, RFlag fl) {
  n->cond.cond = resolve_type(ctx, n->cond.cond, fl);

  if (R_UNLIKELY(n->cond.cond->type != Type_bool)) {
    build_errf(ctx->build, n->cond.cond->pos, NodeEndPos(n->cond.cond),
      "non-bool %s (type %s) used as condition",
      fmtnode(n->cond.cond), fmtnode(n->cond.cond->type));
    n->type = Type_nil;
    return n;
  }

  // visit then branch
  n->cond.thenb = resolve_type(ctx, n->cond.thenb, fl);
  auto thentype = n->cond.thenb->type;

  // visit else branch
  if (n->cond.elseb) {
    typecontext_push(ctx, thentype);
    n->cond.elseb = resolve_type(ctx, n->cond.elseb, fl);
    typecontext_pop(ctx);

    // branches must be of the same type
    auto elsetype = n->cond.elseb->type;
    if (!TypeEquals(ctx->build, thentype, elsetype)) {
      // attempt implicit cast. E.g.
      //
      // x = 3 as int16 ; y = if true x else 0
      //                              ^      ^
      //                            int16   int
      //
      n->cond.elseb = ConvlitImplicit(ctx->build, n->cond.elseb, thentype);
      if (R_UNLIKELY(!TypeEquals(ctx->build, thentype, n->cond.elseb->type))) {
        build_errf(ctx->build, n->pos, NodeEndPos(n),
          "if..else branches of mixed incompatible types %s %s",
          fmtnode(thentype), fmtnode(elsetype));
      }
    }
  }

  n->type = thentype;
  return n;
}


static Type* resolve_id_type(ResCtx* ctx, Node* n, RFlag fl) {
  if (R_UNLIKELY(n->ref.target == NULL)) {
    // identifier failed to resolve
    n->type = Type_nil;
    return n;
  }
  n->ref.target = resolve_type(ctx, n->ref.target, fl);
  n->type = n->ref.target->type;
  return n;
}


#ifdef DEBUG_MODULE
// wrap resolve_type to print return value
static Node* _resolve_type(ResCtx* ctx, Node* n, RFlag fl);
static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n != NULL);
  dlog_mod("○ %s %s (%p, class %s, type %s%s%s)",
    NodeKindName(n->kind), fmtnode(n),
    n,
    DebugNodeClassStr(NodeKindClass(n->kind)),
    fmtnode(n->type),
    ctx->typecontext ? ", typecontext " : "",
    ctx->typecontext ? fmtnode(ctx->typecontext) : "" );

  ctx->debug_depth++;
  Node* n2 = _resolve_type(ctx, n, fl);
  ctx->debug_depth--;

  if (NodeKindIsType(n->kind)) {
    dlog_mod("● %s == %s", fmtnode(n), fmtnode(n));
  } else {
    dlog_mod("● %s => %s", fmtnode(n), fmtnode(n2->type));
  }
  return n2;
}
static Node* _resolve_type(ResCtx* ctx, Node* n, RFlag fl)
#else
static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl)
#endif
{
  assert(n != NULL);

  if (NodeKindIsType(n->kind))
    return n;

  if (n->type) {
    // Has type already. Constant literals might have ideal type.
    if (n->type == Type_ideal) {
      if (fl & RFlagResolveIdeal)
        R_MUSTTAIL return resolve_ideal_type(ctx, n, ctx->typecontext, fl);
      // else: leave as ideal, for now
    }
    return n;
  }

  // branch on node kind
  switch (n->kind) {

  // uses u.array

  case NPkg:
  case NFile:
    n->type = Type_nil;
    for (u32 i = 0; i < n->array.a.len; i++)
      n->array.a.v[i] = resolve_type(ctx, (Node*)n->array.a.v[i], fl);
    break;

  case NBlock:
    R_MUSTTAIL return resolve_block_type(ctx, n, fl);

  case NTuple:
    R_MUSTTAIL return resolve_tuple_type(ctx, n, fl);

  case NFun:
    R_MUSTTAIL return resolve_fun_type(ctx, n, fl);

  // uses u.op
  case NPostfixOp:
  case NPrefixOp:
    n->op.left = resolve_type(ctx, n->op.left, fl);
    n->type = n->op.left->type;
    break;
  case NReturn:
    n->op.left = resolve_type(ctx, n->op.left, fl | RFlagResolveIdeal);
    n->type = n->op.left->type;
    break;

  case NBinOp:
  case NAssign:
    R_MUSTTAIL return resolve_binop_or_assign_type(ctx, n, fl);

  case NTypeCast:
    R_MUSTTAIL return resolve_typecast_type(ctx, n, fl);

  case NCall:
    R_MUSTTAIL return resolve_call_type(ctx, n, fl);

  // uses u.field
  case NLet:
  case NArg:
  case NField:
    if (n->field.init) {
      n->field.init = resolve_type(ctx, n->field.init, fl);
      n->type = n->field.init->type;
    } else {
      n->type = Type_nil;
    }
    break;

  // uses u.cond
  case NIf:
    R_MUSTTAIL return resolve_if_type(ctx, n, fl);

  case NId:
    R_MUSTTAIL return resolve_id_type(ctx, n, fl);

  case NIntLit:
  case NFloatLit:
    if (fl & RFlagResolveIdeal) {
      if (ctx->typecontext)
        R_MUSTTAIL return convlit(ctx->build, n, ctx->typecontext, (fl & RFlagExplicitTypeCast));
      // fallback
      n = NodeCopy(ctx->build->mem, n);
      n->type = IdealType(n->val.ct);
      break;
    }
    // else: not ideal type; should be typed already!
    FALLTHROUGH;
  case NBoolLit:
  case NBad:
  case NNil:
  case NComment:
  case NFunType:
  case NNone:
  case NBasicType:
  case NTupleType:
  case NZeroInit:
  case _NodeKindMax:
    dlog("unexpected %s", fmtast(n));
    assert(0 && "expected to be typed");
    break;

  }  // switch (n->kind)

  // when and if we get here the node should be typed.
  assert(n->type);
  return n;
}


ASSUME_NONNULL_END
