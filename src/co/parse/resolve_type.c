// Resolve types in an AST. Usuaully run after Parse() and ResolveSym()
#include "../common.h"
#include "../util/array.h"
#include "parse.h"

ASSUME_NONNULL_BEGIN

// DEBUG_MODULE: define to enable trace logging
//#define DEBUG_MODULE ""

#ifdef DEBUG_MODULE
  #define dlog_mod(format, ...) \
    fprintf(stderr, DEBUG_MODULE "%*s " format "\n", ctx->debug_depth*2, "", ##__VA_ARGS__)
#else
  #define dlog_mod(...) do{}while(0)
#endif


typedef enum RFlag {
  RFlagNone = 0,
  RFlagExplicitTypeCast = 1 << 0,
  RFlagResolveIdeal     = 1 << 1,  // set when resolving ideal types
  RFlagEager            = 1 << 2,  // set when resolving eagerly
} RFlag;


typedef struct {
  Build* build;

  // typecontext is the "expected" type, if any.
  // E.g. the type of a var while resolving its rvalue.
  Type* nullable typecontext;       // current value; top of typecontext_stack
  Array          typecontext_stack; // stack of Type* which are all of NodeClassType
  Type*          typecontextstack_storage[32];

  Array funstack; // stack of Node*[kind==NFun] -- current function scope
  Node* funstack_storage[8];

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

  // typecontext_stack
  ArrayInitWithStorage(
    &ctx.typecontext_stack,
    ctx.typecontextstack_storage,
    countof(ctx.typecontextstack_storage)
  );

  // funstack
  ArrayInitWithStorage(&ctx.funstack, ctx.funstack_storage, countof(ctx.funstack_storage));
  // always one slot so we can access the top of the stack without checks
  ctx.funstack_storage[0] = NULL;
  ctx.funstack.len = 1;

  n = resolve_type(&ctx, n, RFlagNone);
  ArrayFree(&ctx.typecontext_stack, build->mem);
  ArrayFree(&ctx.funstack, build->mem);
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
  if (ctx->typecontext_stack.len == 0) {
    dlog_mod("typecontext_pop %s (now nil)", fmtnode(ctx->typecontext));
    ctx->typecontext = NULL;
  } else {
    UNUSED auto was = ctx->typecontext;
    ctx->typecontext = (Type*)ArrayPop(&ctx->typecontext_stack);
    dlog_mod("typecontext_pop %s (now %s)", fmtnode(was), fmtnode(ctx->typecontext));
  }
}

inline static void funstack_push(ResCtx* ctx, Node* n) {
  asserteq(n->kind, NFun);
  dlog_mod("funstack_push %s", fmtnode(n));
  ArrayPush(&ctx->funstack, n, ctx->build->mem);
}

inline static void funstack_pop(ResCtx* ctx) {
  assert(ctx->funstack.len > 1); // must never remove the bottom of the stack (NULL value)
  UNUSED auto n = ArrayPop(&ctx->funstack);
  dlog_mod("funstack_pop %s", fmtnode(n));
}

// curr_fun accesses the current function. NULL for top-level.
inline static Node* nullable curr_fun(ResCtx* ctx) {
  return ctx->funstack.v[ctx->funstack.len - 1];
}


static Node* resolveConst(Build* b, Node* n, bool mayReleaseLet) {
  switch (n->kind) {
    case NLet: {
      assert(n->let.nrefs > 0);
      assert(n->let.init != NULL);

      // reset mayReleaseLet at let boundary
      bool mayReleaseLet_child = false;
      if (mayReleaseLet && n->let.nrefs == 1) {
        // we will release n after we have visited its children, so allow children
        // to be released as well.
        mayReleaseLet_child = true;
      }

      // visit initializer node
      auto init = resolveConst(b, n->let.init, mayReleaseLet_child);

      if (mayReleaseLet && NodeUnrefLet(n) == 0) {
        // release now-unused Let node
        n->let.init = NULL;
      }
      return init;
    }

    case NId:
      assert(n->ref.target != NULL);
      return resolveConst(b, n->ref.target, mayReleaseLet);

    default:
      return n;
  }
}

// ResolveConst resolves n to its constant value
Node* ResolveConst(Build* b, Node* n) {
  return resolveConst(b, n, /*mayReleaseLet*/true);
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
);

static Node* resolve_ideal_type1(
  ResCtx* nonull   ctx,
  Node*   nonull   n,
  Node*   nullable typecontext,
  RFlag            fl
) {
  // lower ideal types in all cases but NLet
  dlog_mod("%s node %s to typecontext %s", __func__, fmtnode(n), fmtnode(typecontext));
  assert(typecontext == NULL || typecontext->kind == NBasicType);
  asserteq(n->type, Type_ideal);

  // It's really only constant literals which are actually of ideal type, so switch on those
  // and lower CType to concrete type.
  // In case n is not a constant literal, we simply continue as the AST at n is a compound
  // which contains one or more untyped constants. I.e. continue to traverse AST.

  switch (n->kind) {
    case NIntLit:
    case NFloatLit: {
      if (typecontext) {
        ConvlitFlags clfl = fl & RFlagExplicitTypeCast ? ConvlitExplicit : ConvlitImplicit;
        return convlit(ctx->build, n, typecontext, clfl | ConvlitRelaxedType);
      }
      // no type context; resolve to best effort based on value
      Node* n2 = NodeCopy(ctx->build->mem, n);
      n2->type = IdealType(n->val.ct);
      return n2;
    }

    case NBlock: {
      // the only scenario where this can happen, a block with ideal type, is when the
      // last expression of the block is ideal.
      assert_debug(n->array.a.len > 0);
      u32 lasti = n->array.a.len - 1;
      auto lastn = (Node*)n->array.a.v[lasti];
      lastn = resolve_ideal_type(ctx, lastn, typecontext, fl);
      if (lasti == 0) {
        // prefer to simplify over mutating the block
        return lastn;
      }
      n->type = lastn->type;
      n->array.a.v[lasti] = lastn;
      break;
    }

    case NReturn:
      assertnotnull_debug(n->op.left);
      n->op.left = resolve_ideal_type(ctx, n->op.left, typecontext, fl);
      n->type = n->op.left->type;
      break;

    default:
      // IMPORTANT: This relies on resolve_type to only call resolve_ideal_type for constants.
      // If this is not the case, this would create an infinite loop in some cases.
      panic("unexpected node type %s", NodeKindName(n->kind));
      break;
  }
  return n;
}


inline static Node* resolve_ideal_type(
  ResCtx* nonull   ctx,
  Node*   nonull   n,
  Node*   nullable typecontext,
  RFlag            fl
) {
  n = ResolveConst(ctx->build, n);
  return resolve_ideal_type1(ctx, n, typecontext, fl);
}


static void err_ret_type(ResCtx* ctx, Node* fun, Node* retval) {
  auto expect = fun->type->t.fun.result;
  auto rettype = retval->type;
  // function prototype claims to return type A while the body yields type B
  auto focusnode = retval->kind == NReturn ? retval->op.left : retval;
  const char* msgfmt = "cannot use %s (type %s) as return type %s";
  if (focusnode->kind == NCall) {
    msgfmt = "cannot use result from %s (type %s) as return type %s";
  }
  build_errf(ctx->build, NodePosSpan(focusnode), msgfmt,
    fmtnode(focusnode), fmtnode(rettype), fmtnode(expect));
  node_diag_trail(ctx->build, DiagNote, focusnode);
}


static Node* resolve_ret_type(ResCtx* ctx, Node* n, RFlag fl) { // n->kind==NReturn
  n->op.left = resolve_type(ctx, n->op.left, fl | RFlagResolveIdeal);
  n->type = n->op.left->type;

  // check for return type match (result type is NULL for functions with inferred types)
  auto fn = curr_fun(ctx); // NULL|Node[kind==NFun]
  assertnotnull(fn); // return can only occur inside a function (parser ensures this)
  assertnotnull(fn->type); // function's type should be resolved
  auto fnrettype = fn->type->t.fun.result;
  if (R_UNLIKELY( fnrettype && !TypeEquals(ctx->build, fnrettype, n->type) ))
    err_ret_type(ctx, fn, n);

  return n;
}


static Node* resolve_fun_type(ResCtx* ctx, Node* n, RFlag fl) { // n->kind==NFun
  funstack_push(ctx, n);
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

    if (n->fun.body->type == Type_ideal && ft->t.fun.result != Type_nil) {
      n->fun.body = resolve_ideal_type(ctx, n->fun.body, ft->t.fun.result, fl);
    }

    auto bodyType = n->fun.body->type;

    if (ft->t.fun.result == NULL) {
      // inferred return type, e.g. "fun foo() { 123 } => ()->int"
      ft->t.fun.result = bodyType;
    } else {
      // function's return type is explicit, e.g. "fun foo() int"
      // check for type mismatch
      if (R_UNLIKELY(
        ft->t.fun.result != Type_nil &&
        !TypeEquals(ctx->build, ft->t.fun.result, bodyType) ))
      {
        // function prototype claims to return type A while the body yields type B
        Node* lastexpr = n->fun.body;
        if (lastexpr->kind == NBlock)
          lastexpr = ArrayNodeLast(lastexpr);
        if (lastexpr->kind != NReturn) {
          // note: explicit "return" expressions already check and report type errors
          err_ret_type(ctx, n, lastexpr);
        }
      }
    }
  }

  // make sure its type id is set as codegen relies on this
  if (!ft->t.id)
    ft->t.id = GetTypeID(ctx->build, ft);

  n->type = ft;
  funstack_pop(ctx);
  return n;
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
        build_warnf(ctx->build, NodePosSpan(cn), "unused expression: %s", fmtnode(cn));
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
      build_errf(ctx->build, NodePosSpan(n), "tuple where %s is expected", fmtnode(ct));
    } else if (R_UNLIKELY(ct->array.a.len != n->array.a.len)) {
      build_errf(ctx->build, NodePosSpan(n), "%u expressions where %u expressions are expected %s",
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
      build_errf(ctx->build, NodePosSpan(cn), "unknown type");
    }
    NodeArrayAppend(ctx->build->mem, &tt->t.list.a, cn->type);
    if (ct)
      ctx->typecontext = ct->array.a.v[ctindex++];
  }

  // restore typecontext, set type and return type
  ctx->typecontext = ct;
  n->type = tt;
  return n;
}


static Node* resolve_arraylit_type(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NArrayLit);
  dlog("TODO: resolve_arraylit_type %s", fmtnode(n));
  return n;
}



static Node* finalize_binop(ResCtx* ctx, Node* n) {
  switch (n->op.op) {
    // comparison operators have boolean value
    case TEq:  // "=="
    case TNEq: // "!="
    case TLt:  // "<"
    case TLEq: // "<="
    case TGt:  // ">"
    case TGEq: // ">="
      n->type = Type_bool;
      break;
    default:
      break;
  }
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
      // TODO: we could pick the strongest type here by finding the CType of each operand and
      // then calling resolve_ideal_type on the stronger of the two. For example int32 > int16.
      n->op.left = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      lt = n->op.left->type;
      // note: continue to statement outside these if blocks
    } else {
      dlog_mod("[binop] 2  left is untyped, right is typed (%s)", fmtnode(rt));
      n->op.left = convlit(ctx->build, n->op.left, rt, ConvlitImplicit | ConvlitRelaxedType);
      n->type = rt;
      return finalize_binop(ctx, n);
    }
  } else if (rt == Type_ideal) {
    dlog_mod("[binop] 3  left is typed (%s), right is untyped", fmtnode(lt));
    n->op.right = convlit(ctx->build, n->op.right, lt, ConvlitImplicit | ConvlitRelaxedType);
    n->type = lt;
    return finalize_binop(ctx, n);
  } else {
    dlog_mod("[binop] 4  left & right are typed (%s, %s)", fmtnode(lt) , fmtnode(rt));
  }

  // we get here from either of the two conditions:
  // - left & right are both untyped (lhs has been resolved, above)
  // - left & right are both typed
  if (!TypeEquals(ctx->build, lt, rt)) {
    if (rt == Type_ideal) {
      dlog_mod("[binop] 6B resolve ideal type of right expr");
      n->op.right = resolve_ideal_type(ctx, n->op.right, lt, fl);
    } else {
      dlog_mod("[binop] 6B convlit right expr to type of left side (%s)", fmtnode(lt));
      n->op.right = convlit(ctx->build, n->op.right, lt, ConvlitImplicit | ConvlitRelaxedType);
    }

    // check if convlit failed
    if (R_UNLIKELY(!TypeEquals(ctx->build, lt, n->op.right->type))) {
      build_errf(ctx->build, NodePosSpan(n), "invalid operation: %s (mismatched types %s and %s)",
        fmtnode(n), fmtnode(lt), fmtnode(n->op.right->type));
      if (lt->kind == NBasicType) {
        // suggest type cast: x + (y as int)
        build_notef(ctx->build, NodePosSpan(n->op.right),
          "try a type cast: %s %s (%s as %s)",
          fmtnode(n->op.left), TokName(n->op.op), fmtnode(n->op.right), fmtnode(lt));
      }
    }
  }

  n->type = lt;
  return finalize_binop(ctx, n);
}


static Node* resolve_typecast_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n->call.receiver != NULL);

  if (R_UNLIKELY(!NodeKindIsType(n->call.receiver->kind))) {
    build_errf(ctx->build, NodePosSpan(n),
      "invalid conversion to non-type %s", fmtnode(n->call.receiver));
    n->type = Type_nil;
    return n;
  }

  // Note: n->call.receiver is a Type, not a regular Node (see check above)
  n->call.receiver = resolve_type(ctx, n->call.receiver, fl);
  n->type = n->call.receiver;
  typecontext_push(ctx, n->type);

  assertnotnull(n->call.args);
  n->call.args = resolve_type(ctx, n->call.args, fl | RFlagExplicitTypeCast);
  assertnotnull(n->call.args->type);

  if (TypeEquals(ctx->build, n->call.args->type, n->type)) {
    // source type == target type -- eliminate type cast.
    // The IR builder relies on this and will fail if a type conversion is a noop.
    n = n->call.args;
  } else {
    // attempt conversion to eliminate type cast
    n->call.args = convlit(
      ctx->build, n->call.args, n->call.receiver, ConvlitExplicit | ConvlitRelaxedType);
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
    build_errf(ctx->build, NodePosSpan(n->call.receiver),
      "cannot call %s", fmtnode(n->call.receiver));
    n->type = Type_nil;
    return n;
  }

  dlog_mod("%s ft: %s", __func__, fmtnode(ft));

  bool fail = false;

  if (ft->t.fun.params) {
    // add parameter types to the "requested type" stack
    typecontext_push(ctx, ft->t.fun.params);

    // input arguments, in context of receiver parameters
    assertnotnull(n->call.args);
    n->call.args = resolve_type(ctx, n->call.args, fl);
    dlog_mod("%s argstype: %s", __func__, fmtnode(n->call.args->type));

    // pop parameter types off of the "requested type" stack
    typecontext_pop(ctx);

    // TODO: Consider arguments with defaults:
    // fun foo(a, b int, c int = 0)
    // foo(1, 2) == foo(1, 2, 0)

    fail = !TypeEquals(ctx->build, ft->t.fun.params, n->call.args->type);
  } else {
    // no parameters
    fail = (n->call.args && n->call.args != Const_nil);
    // if (n->call.args && n->call.args != Const_nil) {
    //   auto poss = NodePosSpan(n->call.args->pos != NoPos ? n->call.args : n);
    //   build_errf(ctx->build, poss,
    //     "passing arguments to a function that does not accept any arguments");
    // }
  }

  if (R_UNLIKELY(fail)) {
    auto posSpan = NodePosSpan(n->call.args->pos != NoPos ? n->call.args : n);
    const char* argtypes = "()";
    if (n->call.args != Const_nil) {
      if (!n->call.args->type)
        n->call.args = resolve_type(ctx, n->call.args, fl | RFlagResolveIdeal | RFlagEager);
      argtypes = (const char*)fmtnode(n->call.args->type);
    }
    build_errf(ctx->build, posSpan,
      "can't call function %s %s with arguments of type %s",
      fmtnode(n->call.receiver), fmtnode(ft), argtypes);
  }

  n->type = ft->t.fun.result;
  return n;
}


static Node* resolve_if_type(ResCtx* ctx, Node* n, RFlag fl) {
  n->cond.cond = resolve_type(ctx, n->cond.cond, fl);

  if (R_UNLIKELY(n->cond.cond->type != Type_bool)) {
    build_errf(ctx->build, NodePosSpan(n->cond.cond),
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
      n->cond.elseb = convlit(
        ctx->build, n->cond.elseb, thentype, ConvlitImplicit | ConvlitRelaxedType);
      if (R_UNLIKELY(!TypeEquals(ctx->build, thentype, n->cond.elseb->type))) {
        build_errf(ctx->build, NodePosSpan(n),
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


static void resolve_arraytype_size(ResCtx* ctx, Type* n) {
  asserteq_debug(n->kind, NArrayType);
  asserteq_debug(n->t.array.size, 0); // must not be resolved already
  assertnotnull_debug(n->t.array.sizeExpr); // must be array and not slice

  // set temporary size so that we don't cause an infinite loop
  n->t.array.size = 0xFFFFFFFFFFFFFFFF;
  auto zn = NodeEval(ctx->build, n->t.array.sizeExpr, Type_usize);

  if (R_UNLIKELY(zn == NULL)) {
    // TODO: improve these error message to be more specific
    n->t.array.size = 0;
    zn = n->t.array.sizeExpr;
    build_errf(ctx->build, NodePosSpan(zn), "invalid expression %s for array size", fmtnode(zn));
    node_diag_trail(ctx->build, DiagNote, zn);
  } else {
    n->t.array.sizeExpr = zn;
    asserteq_debug(zn->kind, NIntLit);
    asserteq_debug(zn->val.ct, CType_int);
    n->t.array.size = zn->val.i;
  }
}


static bool is_type_complete(Type* n) {
  if (n->kind == NArrayType &&
      ( (n->t.array.sizeExpr && n->t.array.size == 0) ||
        !is_type_complete(n->t.array.subtype) ) )
  {
    return false;
  }
  return true;
}


static Type* resolve_arraytype_type(ResCtx* ctx, Type* n, RFlag fl) {
  asserteq_debug(n->kind, NArrayType);

  if (n->t.array.sizeExpr && n->t.array.size == 0) {
    n->t.array.sizeExpr = resolve_type(ctx, n->t.array.sizeExpr, fl);
    resolve_arraytype_size(ctx, n);
  }

  if (!is_type_complete(n->t.array.subtype))
    n->t.array.subtype = resolve_type(ctx, n->t.array.subtype, fl);

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
    dlog_mod("● %s => %s", fmtnode(n), fmtnode(n));
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

  if (NodeKindIsType(n->kind)) {
    if (is_type_complete(n))
      return n;
  } else if (n->type) {
    // Has type already. Constant literals might have ideal type.
    if (n->type == Type_ideal) {
      if ((fl & RFlagResolveIdeal) && ((fl & RFlagEager) || ctx->typecontext)) {
        if (ctx->typecontext)
          R_MUSTTAIL return resolve_ideal_type(ctx, n, ctx->typecontext, fl);
        n = NodeCopy(ctx->build->mem, n);
        n->type = IdealType(n->val.ct);
      }
      // else: leave as ideal, for now
      return n;
    } else {
      if (!is_type_complete(n->type))
        n->type = resolve_type(ctx, n->type, fl);
      return n;
    }
  }

  // branch on node kind
  switch (n->kind) {

  // uses Node.array
  case NPkg:
  case NFile:
    // File and Pkg are special in that types do not propagate
    for (u32 i = 0; i < n->array.a.len; i++)
      n->array.a.v[i] = resolve_type(ctx, (Node*)n->array.a.v[i], fl);
    // Note: Instead of setting n->type=Type_nil, leave as NULL and return early
    // to avoid check for null types.
    return n;

  case NBlock:
    R_MUSTTAIL return resolve_block_type(ctx, n, fl);

  case NArrayLit:
    R_MUSTTAIL return resolve_arraylit_type(ctx, n, fl);

  case NTuple:
    R_MUSTTAIL return resolve_tuple_type(ctx, n, fl);

  case NFun:
    R_MUSTTAIL return resolve_fun_type(ctx, n, fl);

  case NPostfixOp:
  case NPrefixOp:
    n->op.left = resolve_type(ctx, n->op.left, fl);
    n->type = n->op.left->type;
    break;

  case NReturn:
    R_MUSTTAIL return resolve_ret_type(ctx, n, fl);

  case NBinOp:
  case NAssign:
    R_MUSTTAIL return resolve_binop_or_assign_type(ctx, n, fl);

  case NTypeCast:
    R_MUSTTAIL return resolve_typecast_type(ctx, n, fl);

  case NCall:
    R_MUSTTAIL return resolve_call_type(ctx, n, fl);

  case NLet:
    if (n->let.init) {
      // leave unused Let untyped
      if (n->let.nrefs == 0)
        return n;
      n->let.init = resolve_type(ctx, n->let.init, fl);
      n->type = n->let.init->type;
    } else {
      n->type = Type_nil;
    }
    break;

  case NArg:
  case NField:
    if (n->field.init) {
      n->field.init = resolve_type(ctx, n->field.init, fl);
      n->type = n->field.init->type;
    } else {
      n->type = Type_nil;
    }
    break;

  case NIf:
    R_MUSTTAIL return resolve_if_type(ctx, n, fl);

  case NId:
    R_MUSTTAIL return resolve_id_type(ctx, n, fl);

  case NArrayType:
    R_MUSTTAIL return resolve_arraytype_type(ctx, n, fl);

  case NIntLit:
  case NFloatLit:
    if (fl & RFlagResolveIdeal) {
      if (ctx->typecontext) {
        ConvlitFlags clfl = fl & RFlagExplicitTypeCast ? ConvlitExplicit : ConvlitImplicit;
        R_MUSTTAIL return convlit(ctx->build, n, ctx->typecontext, clfl | ConvlitRelaxedType);
      }
      // fallback
      n = NodeCopy(ctx->build->mem, n);
      n->type = IdealType(n->val.ct);
      break;
    }
    // else: not ideal type; should be typed already!
    FALLTHROUGH;
  case NBoolLit:
  case NStrLit:
  case NBad:
  case NNil:
  case NFunType:
  case NNone:
  case NBasicType:
  case NTupleType:
  case _NodeKindMax:
    dlog("unexpected %s", fmtast(n));
    assert(!"expected to be typed");
    break;

  }  // switch (n->kind)

  // when and if we get here, the node should be typed
  assertnotnull_debug(n->type);

  return n;
}


ASSUME_NONNULL_END
