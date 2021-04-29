// Resolve types in an AST. Usuaully run after Parse() and ResolveSym()
#include <rbase/rbase.h>
#include "parse.h"
#include "../util/array.h"
// #include "../typeid.h"
// #include "../convlit.h"

// #define DEBUG_MODULE "typeres"

ASSUME_NONNULL_BEGIN

#ifdef DEBUG_MODULE
  #define dlog_mod(format, ...) dlog("[" DEBUG_MODULE "] " format, ##__VA_ARGS__)
#else
  #define dlog_mod(...) do{}while(0)
#endif


typedef enum RFlag {
  RFlagNone = 0,
  RFlagExplicitTypeCast = 1 << 0,
  RFlagResolveIdeal     = 1 << 1,  // set when resolving inside resolve_idealtype
} RFlag;


typedef struct {
  Build*    build;
  Array     reqtypestk;
  TypeCode* reqtypestorage[4];
  bool      explicitTypeCast;
} ResCtx;


static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl);

void ResolveType(Build* build, Node* n) {
  ResCtx ctx = {
    .build = build,
  };
  ArrayInitWithStorage(
    &ctx.reqtypestk,
    ctx.reqtypestorage,
    countof(ctx.reqtypestorage));

  n->type = resolve_type(&ctx, n, RFlagNone);

  ArrayFree(&ctx.reqtypestk, build->mem);
}


inline static Node* nullable reqtype(ResCtx* ctx) {
  if (ctx->reqtypestk.len > 0)
    return (Node*)ctx->reqtypestk.v[ctx->reqtypestk.len - 1];
  return NULL;
}

inline static void reqtype_push(ResCtx* ctx, Node* t) {
  assert(NodeIsType(t));
  assert(t != Type_ideal);
  dlog_mod("reqtype_push %s", fmtnode(t));
  ArrayPush(&ctx->reqtypestk, t, ctx->build->mem);
}

inline static void reqtype_pop(ResCtx* ctx) {
  assert(ctx->reqtypestk.len > 0);
  dlog_mod("reqtype_pop %s", fmtnode(reqtype(ctx)));
  ArrayPop(&ctx->reqtypestk);
}


static Node* resolve_funtype(ResCtx* ctx, Node* n, RFlag fl) {
  Node* ft = NewNode(ctx->build->mem, NFunType);
  auto result = n->type;

  // Important: To avoid an infinite loop when resolving a function which calls itself,
  // we set the unfinished function type objects ahead of calling resolve.
  n->type = ft;

  if (n->fun.params) {
    resolve_type(ctx, n->fun.params, fl);
    ft->t.fun.params = (Node*)n->fun.params->type;
  }

  if (result) {
    ft->t.fun.result = (Node*)resolve_type(ctx, result, fl);
  }

  if (n->fun.body) {
    auto bodyType = resolve_type(ctx, n->fun.body, fl);
    if (ft->t.fun.result == NULL) {
      ft->t.fun.result = bodyType;
    } else if (!TypeEquals(ctx->build, ft->t.fun.result, bodyType)) {
      build_errf(ctx->build, n->fun.body->pos, "cannot use type %s as return type %s",
        fmtnode(bodyType), fmtnode(ft->t.fun.result));
    }
  }

  n->type = ft;
  return ft;
}


// resolve_idealtype resolves the concrete type of n. If reqtype is provided, convlit is used to
// "fit" n into that type. Otherwise the natural concrete type of n is used. (e.g. int)
// n is assumed to be Type_ideal and must be a node->kind = NIntLit | NFloatLit | NLet | NId.
//
static Node* resolve_idealtype(
  ResCtx* nonull   ctx,
  Node*   nonull   n,
  Node*   nullable reqtype,
  RFlag            fl
) {
  // lower ideal types in all cases but NLet
  assert(reqtype == NULL || reqtype->kind == NBasicType);
  dlog_mod("resolve_idealtype node %s to reqtype %s", fmtnode(n), fmtnode(reqtype));

  // It's really only constant literals which are actually of ideal type, so switch on those
  // and lower CType to concrete type.
  // In case n is not a constant literal, we simply continue as the AST at n is a compound
  // which contains one or more untyped constants. I.e. continue to traverse AST.
  switch (n->kind) {
    case NIntLit:
    case NFloatLit:
      if (reqtype == NULL) {
        n->type = IdealType(n->val.ct);
      } else {
        auto n2 = convlit(ctx->build, n, reqtype, /*explicit*/(fl & RFlagExplicitTypeCast));
        if (n2 != n) {
          memcpy(n, n2, sizeof(Node));
        }
      }
      break;

    case NLet:
      assert(n->field.init != NULL);
      return n->type = resolve_idealtype(ctx, n->field.init, reqtype, fl);

    case NId:
      assert(n->ref.target != NULL);
      return n->type = resolve_idealtype(ctx, n->ref.target, reqtype, fl);

    case NBoolLit:
      // always typed; should never be ideal
      panic("NBoolLit with ideal type");
      break;

    default:
      // IMPORTANT: This relies on resolve_type to only call resolve_idealtype for constants.
      // If this is not the case, this would create an infinite loop in some cases.
      panic("unexpected node type %s", NodeKindName(n->kind));
      break;
  }
  return n->type;
}


static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n != NULL);

  dlog_mod("resolve_type %s %p (%s class) type %s",
    NodeKindName(n->kind),
    n,
    NodeClassName(NodeClassTable[n->kind]),
    fmtnode(n->type));

  if (NodeKindIsType(n->kind)) {
    dlog_mod("  => %s", fmtnode(n));
    return n;
  }

  if (n->kind == NFun) {
    // type already resolved
    if (n->type && n->type->kind == NFunType) {
      dlog_mod("  => %s", fmtnode(n->type));
      return n->type;
    }
  } else if (n->type != NULL) {
    // Has type already. Constant literals might have ideal type.
    if (n->type == Type_ideal) {
      if (fl & RFlagResolveIdeal) {
        auto rt = reqtype(ctx);
        return resolve_idealtype(ctx, n, rt, fl);
      }
      // else: leave as ideal, for now
    }
    dlog_mod("  => %s", fmtnode(n->type));
    return n->type;
  } else {
    // Set type to nil here to break any self-referencing cycles.
    // NFun is special-cased as it stores result type in n->type. Note that resolve_funtype
    // handles breaking of cycles.
    // A nice side effect of this is that for error cases and nodes without types, the
    // type "defaults" to nil and we can avoid setting Type_nil in the switch below.
    n->type = Type_nil;
  }

  // branch on node kind
  switch (n->kind) {

  // uses u.array
  case NFile:
    n->type = Type_nil;
    NodeListForEach(&n->array.a, n,
      resolve_type(ctx, n, fl)
    );
    break;

  case NBlock: {
    // type of a block is the type of the last expression.
    auto e = n->array.a.head;
    while (e != NULL) {
      if (e->next == NULL) {
        // Last node, in which case we set the flag to resolve literals
        // so that implicit return values gets properly typed.
        // This also becomes the type of the block.
        n->type = resolve_type(ctx, e->node, fl | RFlagResolveIdeal);
        break;
      } else {
        auto t = resolve_type(ctx, e->node, fl);
        if (t == Type_ideal && NodeIsConst(e->node)) {
          // a lone, unused constant expression, e.g.
          //   { 1  # <- warning: unused expression 1
          //     2
          //   }
          // Resolve its type so that the IR builder doesn't get cranky.
          auto rt = reqtype(ctx);
          resolve_idealtype(ctx, e->node, rt, fl);
          build_errf(ctx->build, e->node->pos, "warning: unused expression %s", fmtnode(e->node));
        }
        e = e->next;
      }
    }
    // Note: No need to set n->type=Type_nil since that is done already (before the switch.)
    break;
  }

  case NTuple: {
    Node* tt = NewNode(ctx->build->mem, NTupleType);
    NodeListForEach(&n->array.a, n, {
      auto t = resolve_type(ctx, n, fl);
      if (!t) {
        t = (Node*)NodeBad;
        build_errf(ctx->build, n->pos, "unknown type");
      }
      NodeListAppend(ctx->build->mem, &tt->t.tuple, t);
    });
    n->type = tt;
    break;
  }

  // uses u.fun
  case NFun:
    n->type = resolve_funtype(ctx, n, fl);
    break;

  // uses u.op
  case NPostfixOp:
  case NPrefixOp: {
    n->type = resolve_type(ctx, n->op.left, fl);
    break;
  }
  case NReturn: {
    n->type = resolve_type(ctx, n->op.left, fl | RFlagResolveIdeal);
    break;
  }
  case NBinOp:
  case NAssign: {
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
        // and then calling resolve_idealtype on the stronger of the two. For example float > int.
        lt = resolve_idealtype(ctx, n->op.left, reqtype(ctx), fl);
        // note: continue to statement outside these if blocks
      } else {
        dlog_mod("[binop] 2  left is untyped, right is typed (%s)", fmtnode(rt));
        n->op.left = ConvlitImplicit(ctx->build, n->op.left, rt);
        n->type = rt;
        break;
      }
    } else if (rt == Type_ideal) {
      dlog_mod("[binop] 3  left is typed (%s), right is untyped", fmtnode(lt));
      n->op.right = ConvlitImplicit(ctx->build, n->op.right, lt);
      n->type = lt;
      break;
    } else {
      dlog_mod("[binop] 4  left & right are typed (%s, %s)", fmtnode(lt) , fmtnode(rt));
    }

    // we get here from either of the two conditions:
    // - left & right are both untyped (lhs has been resolved, above)
    // - left & right are both typed
    if (!TypeEquals(ctx->build, lt, rt))
      n->op.right = ConvlitImplicit(ctx->build, n->op.right, lt);

    n->type = lt;
    break;
  }


  case NTypeCast: {
    assert(n->call.receiver != NULL);
    if (!NodeKindIsType(n->call.receiver->kind)) {
      build_errf(ctx->build, n->pos,
        "invalid conversion to non-type %s", fmtnode(n->call.receiver));
      break;
    }

    fl |= RFlagExplicitTypeCast;

    n->type = resolve_type(ctx, n->call.receiver, fl);
    reqtype_push(ctx, n->type);

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

    reqtype_pop(ctx);
    break;
  }


  case NCall: {
    auto argstype = resolve_type(ctx, n->call.args, fl);
    // Note: resolve_funtype breaks handles cycles where a function calls itself,
    // making this safe (i.e. will not cause an infinite loop.)
    auto recvt = resolve_type(ctx, n->call.receiver, fl);
    assert(recvt != NULL);
    if (recvt->kind != NFunType) {
      build_errf(ctx->build, n->pos, "cannot call %s", fmtnode(n->call.receiver));
      break;
    }
    // Note: Consider arguments with defaults:
    // fun foo(a, b int, c int = 0)
    // foo(1, 2) == foo(1, 2, 0)
    if (!TypeEquals(ctx->build, recvt->t.fun.params, argstype)) {
      build_errf(ctx->build, n->pos, "incompatible arguments %s in function call. Expected %s",
        fmtnode(argstype), fmtnode(recvt->t.fun.params));
    }
    n->type = recvt->t.fun.result;
    break;
  }

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
  case NIf: {
    auto cond = n->cond.cond;
    auto condt = resolve_type(ctx, cond, fl);
    if (condt != Type_bool) {
      build_errf(ctx->build, cond->pos, "non-bool %s (type %s) used as condition",
        fmtnode(cond), fmtnode(condt));
    }
    auto thent = resolve_type(ctx, n->cond.thenb, fl);
    if (n->cond.elseb) {
      reqtype_push(ctx, thent);
      auto elset = resolve_type(ctx, n->cond.elseb, fl);
      reqtype_pop(ctx);
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
    break;
  }

  case NId: {
    auto target = n->ref.target;
    if (target == NULL) {
      // identifier failed to resolve
      break;
    }

    // if (target->type == Type_ideal && (fl & RFlagResolveIdeal) == 0) {
    //   // identifier names a let binding to an untyped constant expression.
    //   // Replace the NId node with a copy of the constant expression node and resolve its
    //   // type to a concrete type.
    //   assert(target->kind == NLet);
    //   assert(target->field.init != NULL); // let always has init
    //   assert(NodeIsConst(target->field.init)); // only constants can be of "ideal" type
    //   auto reqt = reqtype(ctx);
    //   if (reqt != NULL) {
    //     memcpy(n, target->field.init, sizeof(Node)); // convert n to copy of value of let binding.
    //     resolve_idealtype(ctx, n, reqt, fl);
    //   } else {
    //     // leave untyped for now
    //     n->type = Type_ideal;
    //   }
    //   break;
    // }

    n->type = resolve_type(ctx, target, fl);
    break;
  }

  case NIntLit:
  case NFloatLit:
      if (fl & RFlagResolveIdeal) {
        auto rt = reqtype(ctx);
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
      FALLTHROUGH;
  //   n->type = IdealType(n->val.ct);
  //   break;

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
