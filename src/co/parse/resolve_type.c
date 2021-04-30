// Resolve types in an AST. Usuaully run after Parse() and ResolveSym()
#include <rbase/rbase.h>
#include "parse.h"
#include "../util/array.h"

#define DEBUG_MODULE ""
ASSUME_NONNULL_BEGIN

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
  Node* nullable typecontext;       // current value; top of typecontext_stack
  Array          typecontext_stack; // stack of Node* which are all of NodeClassType
  Node*          typecontext_storage[32];

  bool explicitTypeCast;

  #ifdef DEBUG_MODULE
  int debug_depth;
  #endif
} ResCtx;


static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl);

void ResolveType(Build* build, Node* n) {
  // setup ResCtx
  ResCtx ctx = {
    .build = build,
  };
  ArrayInitWithStorage(
    &ctx.typecontext_stack,
    ctx.typecontext_storage,
    countof(ctx.typecontext_storage));

  n->type = resolve_type(&ctx, n, RFlagNone);

  ArrayFree(&ctx.typecontext_stack, build->mem);
}

static void typecontext_push(ResCtx* ctx, Node* t) {
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
    ctx->typecontext = (Node*)ArrayPop(&ctx->typecontext_stack);
  }
}


static Node* resolve_fun_type(ResCtx* ctx, Node* n, RFlag fl) {
  Node* ft = NewNode(ctx->build->mem, NFunType);

  // Important: To avoid an infinite loop when resolving a function which calls itself,
  // we set the unfinished function type objects ahead of calling resolve.
  n->type = ft;

  if (n->fun.params) {
    resolve_type(ctx, n->fun.params, fl);
    ft->t.fun.params = (Node*)n->fun.params->type;
  }

  if (n->fun.result)
    ft->t.fun.result = (Node*)resolve_type(ctx, n->fun.result, fl);

  if (n->fun.body) {
    auto bodyType = resolve_type(ctx, n->fun.body, fl);
    if (ft->t.fun.result == NULL) {
      ft->t.fun.result = bodyType;
    } else if (!TypeEquals(ctx->build, ft->t.fun.result, bodyType)) {
      // function prototype claims to return type A while the body yields type B
      build_errf(ctx->build, n->fun.body->pos, "cannot use type %s as return type %s",
        fmtnode(bodyType), fmtnode(ft->t.fun.result));
    }
  }

  // make sure its type id is set as codegen relies on this
  if (!ft->t.id)
    ft->t.id = GetTypeID(ctx->build, n);

  n->type = ft;
  return ft;
}


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
  assert(typecontext == NULL || typecontext->kind == NBasicType);
  dlog_mod("resolve_ideal_type node %s to typecontext %s", fmtnode(n), fmtnode(typecontext));

  // It's really only constant literals which are actually of ideal type, so switch on those
  // and lower CType to concrete type.
  // In case n is not a constant literal, we simply continue as the AST at n is a compound
  // which contains one or more untyped constants. I.e. continue to traverse AST.
  switch (n->kind) {
    case NIntLit:
    case NFloatLit:
      if (typecontext == NULL) {
        n->type = IdealType(n->val.ct);
      } else {
        auto n2 = convlit(ctx->build, n, typecontext, /*explicit*/(fl & RFlagExplicitTypeCast));
        if (n2 != n) {
          memcpy(n, n2, sizeof(Node));
        }
      }
      break;

    case NLet:
      assert(n->field.init != NULL);
      return n->type = resolve_ideal_type(ctx, n->field.init, typecontext, fl);

    case NId:
      assert(n->ref.target != NULL);
      return n->type = resolve_ideal_type(ctx, n->ref.target, typecontext, fl);

    case NBoolLit:
      // always typed; should never be ideal
      panic("NBoolLit with ideal type");
      break;

    default:
      // IMPORTANT: This relies on resolve_type to only call resolve_ideal_type for constants.
      // If this is not the case, this would create an infinite loop in some cases.
      panic("unexpected node type %s", NodeKindName(n->kind));
      break;
  }
  return n->type;
}


static Node* resolve_block_type(ResCtx* ctx, Node* n, RFlag fl) {
  // The type of a block is the type of the last expression.
  // Resolve each entry of the block:
  if (n->array.a.len == 0) {
    n->type = Type_nil;
  } else {
    // resolve all but the last expression without requiring ideal-type resolution
    u32 lasti = n->array.a.len - 1;
    for (u32 i = 0; i < lasti; i++) {
      Node* cn = (Node*)n->array.a.v[i];
      auto t = resolve_type(ctx, cn, fl);
      dlog("THING %d %d", t == Type_ideal, NodeIsConst(cn));
      if (t == Type_ideal && NodeIsConst(cn)) {
        // an unused constant expression, e.g.
        //   { 1  # <- warning: unused expression 1
        //     2
        //   }
        // Resolve its type so that the IR builder doesn't get cranky.
        auto rt = ctx->typecontext;
        resolve_ideal_type(ctx, cn, rt, fl);
        build_warnf(ctx->build, cn->pos, "unused expression %s", fmtnode(cn));
      }
    }
    // Last node, in which case we set the flag to resolve literals
    // so that implicit return values gets properly typed.
    // This also becomes the type of the block.
    Node* cn = (Node*)n->array.a.v[lasti];
    n->type = resolve_type(ctx, cn, fl | RFlagResolveIdeal);
  }
  // Note: No need to set n->type=Type_nil since that is done already,
  //       before the switch in resolve_type.
  return n->type;
}


static Node* resolve_tuple_type(ResCtx* ctx, Node* n, RFlag fl) {
  Node* tt = NewNode(ctx->build->mem, NTupleType);

  // typecontext
  Node* ct = ctx->typecontext;
  u32 ctindex = 0;
  if (ct) {
    if (R_UNLIKELY(ct->kind != NTupleType))
      build_errf(ctx->build, n->pos, "tuple where %s is expected", fmtnode(ct));
    if (R_UNLIKELY(ct->array.a.len != n->array.a.len)) {
      build_errf(ctx->build, n->pos, "%u expressions where %u expressions are expected %s",
        n->array.a.len, ct->array.a.len, fmtnode(ct));
    }
    assert(ct->array.a.len > 0); // tuples should never be empty
    ctx->typecontext = ct->array.a.v[ctindex++];
  }

  // for each tuple entry
  for (u32 i = 0; i < n->array.a.len; i++) {
    Node* cn = (Node*)n->array.a.v[i];
    auto t = resolve_type(ctx, cn, fl);
    if (!t) {
      t = (Node*)NodeBad;
      build_errf(ctx->build, cn->pos, "unknown type");
    }
    NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, t);
    if (ct)
      ctx->typecontext = ct->array.a.v[ctindex++];
  }

  // restore typecontext, set type and return type
  ctx->typecontext = ct;
  n->type = tt;
  return tt;
}


static Node* resolve_binop_or_assign_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n->op.right != NULL);

  Node* lt = NULL;
  Node* rt = NULL;

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
  auto fl1 = fl; // save fl
  fl &= ~RFlagResolveIdeal; // clear "resolve ideal" flag
  lt = resolve_type(ctx, n->op.left, fl);
  rt = resolve_type(ctx, n->op.right, fl);
  fl = fl1; // restore fl
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
      lt = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      // note: continue to statement outside these if blocks
    } else {
      dlog_mod("[binop] 2  left is untyped, right is typed (%s)", fmtnode(rt));
      n->op.left = ConvlitImplicit(ctx->build, n->op.left, rt);
      n->type = rt;
      return rt;
    }
  } else if (rt == Type_ideal) {
    dlog_mod("[binop] 3  left is typed (%s), right is untyped", fmtnode(lt));
    n->op.right = ConvlitImplicit(ctx->build, n->op.right, lt);
    n->type = lt;
    return lt;
  } else {
    dlog_mod("[binop] 4  left & right are typed (%s, %s)", fmtnode(lt) , fmtnode(rt));
  }

  // we get here from either of the two conditions:
  // - left & right are both untyped (lhs has been resolved, above)
  // - left & right are both typed
  if (!TypeEquals(ctx->build, lt, rt))
    n->op.right = ConvlitImplicit(ctx->build, n->op.right, lt);

  n->type = lt;
  return lt;
}


static Node* resolve_typecast_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n->call.receiver != NULL);
  if (!NodeKindIsType(n->call.receiver->kind)) {
    build_errf(ctx->build, n->pos,
      "invalid conversion to non-type %s", fmtnode(n->call.receiver));
    return Type_nil;
  }

  fl |= RFlagExplicitTypeCast;

  n->type = resolve_type(ctx, n->call.receiver, fl);
  typecontext_push(ctx, n->type);

  auto argstype = resolve_type(ctx, n->call.args, fl);
  if (argstype != NULL && TypeEquals(ctx->build, argstype, n->type)) {
    // eliminate type cast since source is already target type
    memcpy(n, n->call.args, sizeof(Node));
  } else {
    // attempt conversion to eliminate type cast
    n->call.args = ConvlitExplicit(ctx->build, n->call.args, n->call.receiver);
    if (TypeEquals(ctx->build, n->call.args->type, n->type))
      memcpy(n, n->call.args, sizeof(Node));
  }

  typecontext_pop(ctx);
  return n->type;
}


static Node* resolve_call_type(ResCtx* ctx, Node* n, RFlag fl) {
  // Note: resolve_fun_type breaks handles cycles where a function calls itself,
  // making this safe (i.e. will not cause an infinite loop.)
  auto ft = resolve_type(ctx, n->call.receiver, fl);
  assert(ft != NULL);
  if (ft->kind != NFunType) {
    build_errf(ctx->build, n->pos, "cannot call %s", fmtnode(n->call.receiver));
    return Type_nil;
  }
  dlog_mod("resolve_call_type ft: %s", fmtnode(ft));

  // add parameter types to the "requested type" stack
  typecontext_push(ctx, ft->t.fun.params);

  // input arguments, in context of receiver parameters
  auto argstype = resolve_type(ctx, n->call.args, fl);
  dlog_mod("resolve_call_type argstype: %s", fmtnode(argstype));

  // pop parameter types off of the "requested type" stack
  typecontext_push(ctx, ft->t.fun.params);

  // Note: Consider arguments with defaults:
  // fun foo(a, b int, c int = 0)
  // foo(1, 2) == foo(1, 2, 0)
  if (!TypeEquals(ctx->build, ft->t.fun.params, argstype)) {
    build_errf(ctx->build, n->pos, "incompatible arguments %s in function call; expected %s",
      fmtnode(argstype), fmtnode(ft->t.fun.params));
  }
  n->type = ft->t.fun.result;
  return n->type;
}


static Node* resolve_if_type(ResCtx* ctx, Node* n, RFlag fl) {
  auto cond = n->cond.cond;
  auto condt = resolve_type(ctx, cond, fl);
  if (condt != Type_bool) {
    build_errf(ctx->build, cond->pos, "non-bool %s (type %s) used as condition",
      fmtnode(cond), fmtnode(condt));
    return Type_nil;
  }
  auto thent = resolve_type(ctx, n->cond.thenb, fl);
  if (n->cond.elseb) {
    typecontext_push(ctx, thent);
    auto elset = resolve_type(ctx, n->cond.elseb, fl);
    typecontext_pop(ctx);
    // branches must be of the same type
    if (!TypeEquals(ctx->build, thent, elset)) {
      // attempt implicit cast. E.g.
      //
      // x = 3 as int16 ; y = if true x else 0
      //                              ^      ^
      //                            int16   int
      //
      n->cond.elseb = ConvlitImplicit(ctx->build, n->cond.elseb, thent);
      if (!TypeEquals(ctx->build, thent, n->cond.elseb->type)) {
        build_errf(ctx->build, n->pos, "if..else branches of mixed incompatible types %s %s",
          fmtnode(thent), fmtnode(elset));
      }
    }
  }
  n->type = thent;
  return thent;
}


static Node* resolve_id_type(ResCtx* ctx, Node* n, RFlag fl) {
  auto target = n->ref.target;
  if (target == NULL) // identifier failed to resolve
    return Type_nil;

  // if (target->type == Type_ideal && (fl & RFlagResolveIdeal) == 0) {
  //   // identifier names a let binding to an untyped constant expression.
  //   // Replace the NId node with a copy of the constant expression node and resolve its
  //   // type to a concrete type.
  //   assert(target->kind == NLet);
  //   assert(target->field.init != NULL); // let always has init
  //   assert(NodeIsConst(target->field.init)); // only constants can be of "ideal" type
  //   auto reqt = ctx->typecontext;
  //   if (reqt != NULL) {
  //     memcpy(n, target->field.init, sizeof(Node)); // convert n to copy of value of let binding.
  //     resolve_ideal_type(ctx, n, reqt, fl);
  //   } else {
  //     // leave untyped for now
  //     n->type = Type_ideal;
  //   }
  //   break;
  // }

  n->type = resolve_type(ctx, target, fl);
  return n->type;
}


#ifdef DEBUG_MODULE
// wrap resolve_type to print return value
static Node* _resolve_type(ResCtx* ctx, Node* n, RFlag fl);
static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n != NULL);
  dlog_mod("○ %s %s (%p, class %s, type %s%s%s)",
    NodeKindName(n->kind), fmtnode(n),
    n,
    NodeClassName(NodeClassTable[n->kind]),
    fmtnode(n->type),
    ctx->typecontext ? ", typecontext " : "",
    ctx->typecontext ? fmtnode(ctx->typecontext) : "" );

  ctx->debug_depth++;
  Node* t = _resolve_type(ctx, n, fl);
  ctx->debug_depth--;

  dlog_mod("● %s => %s", fmtnode(n), fmtnode(n->type));
  return t;
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
      if (fl & RFlagResolveIdeal) {
        auto rt = ctx->typecontext;
        return resolve_ideal_type(ctx, n, rt, fl);
      }
      // else: leave as ideal, for now
    }
    return n->type;
  } else {
    // Set type to nil here to break any self-referencing cycles.
    // NFun is special-cased as it stores result type in n->type. Note that resolve_fun_type
    // handles breaking of cycles.
    // A nice side effect of this is that for error cases and nodes without types, the
    // type "defaults" to nil and we can avoid setting Type_nil in the switch below.
    n->type = Type_nil;
  }

  // branch on node kind
  switch (n->kind) {

  // uses u.array

  case NPkg:
  case NFile:
    n->type = Type_nil;
    for (u32 i = 0; i < n->array.a.len; i++)
      resolve_type(ctx, (Node*)n->array.a.v[i], fl);
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
    n->type = resolve_type(ctx, n->op.left, fl);
    break;
  case NReturn:
    n->type = resolve_type(ctx, n->op.left, fl | RFlagResolveIdeal);
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
  case NField: {
    if (n->field.init) {
      n->type = resolve_type(ctx, n->field.init, fl);
    } else {
      n->type = Type_nil;
    }
    break;
  }

  // uses u.cond
  case NIf:
    R_MUSTTAIL return resolve_if_type(ctx, n, fl);

  case NId:
    R_MUSTTAIL return resolve_id_type(ctx, n, fl);

  case NIntLit:
  case NFloatLit:
    if (fl & RFlagResolveIdeal) {
      auto rt = ctx->typecontext;
      if (rt == NULL) {
        n->type = IdealType(n->val.ct);
      } else {
        auto n2 = convlit(ctx->build, n, rt, /*explicit*/(fl & RFlagExplicitTypeCast));
        if (n2 != n) {
          memcpy(n, n2, sizeof(Node));
        }
      }
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

  return n->type;
}


ASSUME_NONNULL_END
