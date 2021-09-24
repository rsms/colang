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
  Type* nullable typecontext; // current value; top of logical typecontext stack

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

  // funstack
  ArrayInitWithStorage(&ctx.funstack, ctx.funstack_storage, countof(ctx.funstack_storage));
  // always one slot so we can access the top of the stack without checks
  ctx.funstack_storage[0] = NULL;
  ctx.funstack.len = 1;

  n = resolve_type(&ctx, n, RFlagNone);
  ArrayFree(&ctx.funstack, build->mem);
  return n;
}

// returns old type
static Type* nullable typecontext_set(ResCtx* ctx, Type* nullable newtype) {
  if (newtype) {
    assert(NodeIsType(newtype) || NodeIsMacroParam(newtype));
    assertne(newtype, Type_ideal);
  }
  dlog_mod("typecontext_set %s", fmtnode(newtype));
  auto oldtype = ctx->typecontext;
  ctx->typecontext = newtype;
  return oldtype;
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


static Node* resolve_const(Build* b, Node* n) {
  switch (n->kind) {
    case NVar: {
      assert(n->var.nrefs > 0);
      assert(n->var.init != NULL);
      // visit initializer node
      return resolve_const(b, n->var.init);
    }

    case NId:
      assert(n->ref.target != NULL);
      return resolve_const(b, n->ref.target);

    default:
      return n;
  }
}

// ResolveConst resolves n to its constant value
Node* ResolveConst(Build* b, Node* n) {
  return NodeIsConst(n) && !NodeIsParam(n) ? resolve_const(b, n) : n;
}


// resolve_ideal_type resolves the concrete type of n. If typecontext is provided, convlit is
// used to "fit" n into that type. Otherwise the natural concrete type of n is used. (e.g. int)
// n is assumed to be Type_ideal and must be a node->kind = NIntLit | NFloatLit | NVar | NId.
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
  // lower ideal types in all cases but NVar
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

    case NVar:
      assertnotnull_debug(n->var.init);
      n->var.init = resolve_ideal_type(ctx, n->var.init, typecontext, fl);
      n->type = n->var.init->type;
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


static Node* resolve_ret_type(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NReturn);
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


static Node* resolve_macro(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NMacro);

  assertnotnull(n->macro.template);
  n->macro.template = resolve_type(ctx, n->macro.template, fl);

  n->type = n->macro.template->type;

  return n;
}


static Node* resolve_fun(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NFun);
  funstack_push(ctx, n);
  Type* ft = NewNode(ctx->build->mem, NFunType);

  // Important: To avoid an infinite loop when resolving a function which calls itself,
  // we set the unfinished function type objects ahead of calling resolve.
  n->type = ft;

  if (n->fun.params) {
    n->fun.params = resolve_type(ctx, n->fun.params, fl);
    ft->t.fun.params = (Type*)n->fun.params->type;
  }

  // return type
  assertnotnull(n->fun.result);
  ft->t.fun.result = resolve_type(ctx, n->fun.result, fl);
  if (R_UNLIKELY(!NodeIsType(ft->t.fun.result))) {
    build_errf(ctx->build, NodePosSpan(n->fun.result),
      "%s is not a type", fmtnode(n->fun.result));

  } else if (n->fun.body) {
    // body
    n->fun.body = resolve_type(ctx, n->fun.body, fl);

    if (n->fun.body->type == Type_ideal && ft->t.fun.result != Type_nil) {
      n->fun.body = resolve_ideal_type(ctx, n->fun.body, ft->t.fun.result, fl);
    }

    auto bodyType = assertnotnull_debug(n->fun.body->type);
    dlog_mod("bodyType    => N%s %s", NodeKindName(bodyType->kind), fmtnode(bodyType));
    dlog_mod("return type => N%s %s",
      NodeKindName(ft->t.fun.result->kind), fmtnode(ft->t.fun.result));

    if (ft->t.fun.result == Type_auto) {
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
          lastexpr = NodeArrayLast(&lastexpr->array.a);
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


static Node* resolve_block_type(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NBlock);
  // The type of a block is the type of the last expression.
  // Resolve each entry of the block:
  if (n->array.a.len == 0) {
    n->type = Type_nil;
  } else {
    // resolve all but the last expression without requiring ideal-type resolution
    u32 lasti = n->array.a.len - 1;
    for (u32 i = 0; i < lasti; i++) {
      RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
    }
    // Last node, in which case we set the flag to resolve literals
    // so that implicit return values gets properly typed.
    // This also becomes the type of the block.
    Node* cn = RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, lasti, fl | RFlagResolveIdeal);
    n->type = cn->type;
  }
  return n;
}


static Node* resolve_array(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NArray);

  // Array expression is always in a known type context
  auto typecontext = ctx->typecontext; // save typecontext
  assertnotnull_debug(typecontext);
  asserteq_debug(typecontext->kind, NArrayType);
  assertnotnull_debug(typecontext->t.array.subtype);

  n->type = typecontext;
  typecontext_set(ctx, typecontext->t.array.subtype);

  fl |= RFlagResolveIdeal;
  for (u32 i = 0; i < n->array.a.len; i++) {
    RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
  }

  ctx->typecontext = typecontext; // restore typecontext
  return n;
}


static Node* resolve_tuple(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NTuple);

  auto typecontext = ctx->typecontext; // save typecontext
  fl |= RFlagResolveIdeal;

  Type* tupleType = typecontext;
  u32 ctindex = 0;
  if (tupleType) {
    if (R_UNLIKELY(tupleType->kind != NTupleType)) {
      build_errf(ctx->build, NodePosSpan(tupleType),
        "outer type %s where tuple is expected", fmtnode(tupleType));
      tupleType = NULL;
    } else if (R_UNLIKELY(tupleType->t.tuple.a.len != n->array.a.len)) {
      build_errf(ctx->build, NodePosSpan(n),
        "%u expressions where %u expressions are expected %s",
        n->array.a.len, tupleType->t.tuple.a.len, fmtnode(tupleType));
      tupleType = NULL;
    } else {
      assert(tupleType->t.tuple.a.len > 0); // tuples should never be empty
      typecontext_set(ctx, tupleType->t.tuple.a.v[ctindex++]);
    }
  }

  Type* tt = NewNode(ctx->build->mem, NTupleType);

  // for each tuple entry
  for (u32 i = 0; i < n->array.a.len; i++) {
    if (!n->array.a.v[i]) {
      if (ctx->typecontext) {
        NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, ctx->typecontext);
      } else {
        NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, (Node*)NodeBad);
        build_errf(ctx->build, NodePosSpan(n), "unable to infer type of tuple element %u", i);
      }
    } else {
      Node* cn = RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
      if (R_UNLIKELY(!cn->type)) {
        cn->type = (Node*)NodeBad;
        build_errf(ctx->build, NodePosSpan(cn), "unknown type");
      }
      NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, cn->type);
    }
    if (tupleType)
      typecontext_set(ctx, tupleType->t.tuple.a.v[ctindex++]);
  }

  ctx->typecontext = typecontext; // restore typecontext
  n->type = tt;
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
  NodeTransferConst2(n, n->op.left, n->op.right);
  return n;
}


static Node* resolve_binop_or_assign(ResCtx* ctx, Node* n, RFlag fl) {
  assert_debug(n->kind == NBinOp || n->kind == NAssign);
  assert(n->op.right != NULL);

  Type* lt = NULL;
  Type* rt = NULL;


  // This is a bit of a mess, but what's going on here is making sure that untyped
  // operands are requested to become the type of typed operands.
  // For example:
  //   x = 3 as int64
  //   y = x + 2
  // Parses to:
  //   int64:(Var x int64:(IntLit 3))
  //   ?:(Var y ?:(BinOp "+"
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
  auto typecontext = ctx->typecontext; // save typecontext
  n->op.left = resolve_type(ctx, n->op.left, fl & ~RFlagResolveIdeal);
  // if (n->op.op == TAssign && n->op.left->type->kind == NTupleType) {
  //   // multi assignment: support var definitions which have NULL as LHS values
  //   ctx->typecontext = n->op.left->type;
  // }
  if (n->op.left->type != Type_ideal)
    ctx->typecontext = n->op.left->type;
  n->op.right = resolve_type(ctx, n->op.right, fl & ~RFlagResolveIdeal);
  lt = n->op.left->type;
  rt = n->op.right->type;
  ctx->typecontext = typecontext; // restore typecontext
  //
  // convert operand types as needed. The following code tests all branches:
  //
  //   a = 1 + 2                         # 1  left & right are untyped
  //   a = 2 + (1 as uint32)             # 2  left is untyped, right is typed
  //   a = (1 as uint32) + 2             # 3  left is typed, right is untyped
  //   a = (1 as uint32) + (2 as uint32) # 4  left & right are typed
  //
  if (lt == Type_ideal) {
    /*if (n->op.op == TAssign) {
      dlog_mod("[binop] 1  left is untyped and an assignment target");
      Node* left = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      dlog("left %s", fmtast(left));
    } else*/ if (rt == Type_ideal) {
      dlog_mod("[binop] 2  left & right are untyped");
      // TODO: we could pick the strongest type here by finding the CType of each operand and
      // then calling resolve_ideal_type on the stronger of the two. For example int32 > int16.
      n->op.left = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      lt = n->op.left->type;
      // note: continue to statement outside these if blocks
    } else {
      dlog_mod("[binop] 3  left is untyped, right is typed (%s)", fmtnode(rt));
      n->op.left = convlit(ctx->build, n->op.left, rt, ConvlitImplicit | ConvlitRelaxedType);
      n->type = rt;
      return finalize_binop(ctx, n);
    }
  } else if (rt == Type_ideal) {
    dlog_mod("[binop] 4  left is typed (%s), right is untyped", fmtnode(lt));
    n->op.right = convlit(ctx->build, n->op.right, lt, ConvlitImplicit | ConvlitRelaxedType);
    n->type = lt;
    return finalize_binop(ctx, n);
  } else {
    dlog_mod("[binop] 5  left & right are typed (%s, %s)", fmtnode(lt) , fmtnode(rt));
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
      build_errf(ctx->build, NodePosSpan(n),
        "invalid operation: %s (mismatched types %s and %s)",
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


static Node* resolve_typecast(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NTypeCast);
  assertnotnull_debug(n->call.receiver);
  assertnotnull_debug(n->call.args);

  if (R_UNLIKELY(!NodeIsType(n->call.receiver))) {
    build_errf(ctx->build, NodePosSpan(n),
      "invalid conversion to non-type %s", fmtnode(n->call.receiver));
    n->type = Type_nil;
    return n;
  }

  // Note: n->call.receiver is a Type, not a regular Node (see check above)
  n->call.receiver = resolve_type(ctx, n->call.receiver, fl);
  n->type = n->call.receiver;
  auto typecontext = typecontext_set(ctx, n->type);

  n->call.args = resolve_type(ctx, n->call.args, fl | RFlagExplicitTypeCast);

  assertnotnull_debug(n->call.args->type);

  if (TypeEquals(ctx->build, n->call.args->type, n->type)) {
    // source type == target type: eliminate type cast.
    // The IR builder relies on this and will fail if a type conversion is a noop.
    n = n->call.args;
  } else {
    // attempt conversion to eliminate type cast
    n->call.args = convlit(
      ctx->build, n->call.args, n->call.receiver, ConvlitExplicit | ConvlitRelaxedType);
    if (TypeEquals(ctx->build, n->call.args->type, n->type)) {
      // source type == target type: eliminate type cast
      n = n->call.args;
    }
  }

  ctx->typecontext = typecontext; // restore typecontext
  return n;
}


static Node* resolve_call(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NCall);

  // Note: resolve_fun breaks handles cycles where a function calls itself,
  // making this safe (i.e. will not cause an infinite loop.)
  n->call.receiver = resolve_type(ctx, n->call.receiver, fl);

  // dlog("call (N%s) %s (type %s)",
  //   NodeKindName(n->call.receiver->kind), fmtnode(n->call.receiver),
  //   fmtnode(n->call.receiver->type));

  // Node* recv = unbox(n->call.receiver);
  Node* recv = n->call.receiver;

  // expected input type
  Type* intype = NULL;
  Type* outtype = NULL;
  Type* recvt = assertnotnull_debug(recv->type);
  switch (recvt->kind) {
    case NTypeType: // type constructor call
      intype = recvt->t.type;
      outtype = recvt->t.type;
      fl |= RFlagExplicitTypeCast;
      break;
    case NFunType: // function call
      intype = recvt->t.fun.params;
      outtype = recvt->t.fun.result;
      break;
    default:
      build_errf(ctx->build, NodePosSpan(n->call.receiver),
        "cannot call %s %s (%s)",
        TypeKindName(recvt->t.kind), fmtnode(recv), fmtnode(recvt));
      n->type = Type_nil;
      return n;
  }

  dlog_mod("%s recvt: %s", __func__, fmtnode(recvt));


  if (intype) {
    // add parameter types to the "requested type" stack
    auto typecontext = typecontext_set(ctx, intype);

    // input arguments, in context of receiver parameters
    assertnotnull(n->call.args);
    n->call.args = resolve_type(ctx, n->call.args, fl | RFlagResolveIdeal);
    dlog_mod("%s argstype: %s", __func__, fmtnode(n->call.args->type));

    // pop parameter types off of the "requested type" stack
    ctx->typecontext = typecontext;

    // TODO: Consider arguments with defaults:
    // fun foo(a, b int, c int = 0)
    // foo(1, 2) == foo(1, 2, 0)
  }

  // verify arguments
  if (recvt->kind == NFunType) {
    bool fail = false;
    if (intype) {
      fail = !TypeEquals(ctx->build, intype, n->call.args->type);
    } else { // no parameters
      fail = n->call.args && n->call.args != Const_nil;
    }

    if (R_UNLIKELY(fail)) {
      auto posSpan = NodePosSpan(n->call.args->pos != NoPos ? n->call.args : n);
      const char* argtypes = "()";
      if (n->call.args != Const_nil) {
        if (!n->call.args->type) {
          n->call.args = resolve_type(
            ctx, n->call.args, fl | RFlagResolveIdeal | RFlagEager);
        }
        argtypes = (const char*)fmtnode(n->call.args->type);
      }
      build_errf(ctx->build, posSpan,
        "cannot call %s %s (%s) with arguments of type %s",
        TypeKindName(recvt->t.kind), fmtnode(recv), fmtnode(recvt), argtypes);
    }
  }

  n->type = outtype;
  return n;
}


static Node* resolve_if(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NIf);
  n->cond.cond = resolve_type(ctx, n->cond.cond, fl | RFlagResolveIdeal | RFlagEager);

  // condition must be of boolean type
  if (R_UNLIKELY(n->cond.cond->type != Type_bool)) {
    build_errf(ctx->build, NodePosSpan(n->cond.cond),
      "non-bool %s (type %s) used as condition",
      fmtnode(n->cond.cond), fmtnode(n->cond.cond->type));
    n->type = Type_nil;
    return n;
  }

  // visit then branch
  n->cond.thenb = resolve_type(ctx, n->cond.thenb, fl | RFlagResolveIdeal | RFlagEager);
  auto thentype = n->cond.thenb->type;

  // visit else branch
  if (n->cond.elseb) {
    auto typecontext = typecontext_set(ctx, thentype);
    n->cond.elseb = resolve_type(ctx, n->cond.elseb, fl);
    ctx->typecontext = typecontext; // restore typecontext

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


static Node* resolve_id(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NId);
  if (R_UNLIKELY(n->ref.target == NULL)) {
    // identifier failed to resolve
    n->type = Type_nil;
    return n;
  }
  n->ref.target = resolve_type(ctx, n->ref.target, fl);
  n->type = n->ref.target->type;
  return n;
}


static Node* nullable eval_usize(ResCtx* ctx, Node* sizeExpr) {
  assertnotnull_debug(sizeExpr); // must be array and not slice

  auto zn = NodeEval(ctx->build, sizeExpr, Type_usize);

  if (R_UNLIKELY(zn == NULL))
    return NULL;

  asserteq_debug(zn->kind, NIntLit);
  asserteq_debug(zn->val.ct, CType_int);
  // result in zn->val.i
  return zn;
}


static void resolve_arraytype_size(ResCtx* ctx, Type* n) {
  asserteq_debug(n->kind, NArrayType);
  asserteq_debug(n->t.array.size, 0); // must not be resolved already
  assertnotnull_debug(n->t.array.sizeExpr); // must be array and not slice

  // set temporary size so that we don't cause an infinite loop
  n->t.array.size = 0xFFFFFFFFFFFFFFFF;

  Node* zn = eval_usize(ctx, n->t.array.sizeExpr);

  if (R_UNLIKELY(zn == NULL)) {
    // TODO: improve these error message to be more specific
    n->t.array.size = 0;
    zn = n->t.array.sizeExpr;
    build_errf(ctx->build, NodePosSpan(zn), "invalid expression %s for array size", fmtnode(zn));
    node_diag_trail(ctx->build, DiagNote, zn);
  } else {
    n->t.array.size = zn->val.i;
    n->t.array.sizeExpr = zn;
  }
}


static bool is_type_complete(Type* n) {
  switch (n->kind) {
    case NArrayType:
      return !( (n->t.array.sizeExpr && n->t.array.size == 0) ||
                !is_type_complete(n->t.array.subtype) );

    case NStructType:
      return (n->flags & NodeFlagCustomInit) == 0;

    default:
      return true;
  }
}


static Type* resolve_array_type(ResCtx* ctx, Type* n, RFlag fl) {
  asserteq_debug(n->kind, NArrayType);

  if (n->t.array.sizeExpr && n->t.array.size == 0) {
    n->t.array.sizeExpr = resolve_type(ctx, n->t.array.sizeExpr, fl);
    resolve_arraytype_size(ctx, n);
  }

  if (!is_type_complete(n->t.array.subtype))
    n->t.array.subtype = resolve_type(ctx, n->t.array.subtype, fl);

  return n;
}


static Type* resolve_struct_type(ResCtx* ctx, Type* t, RFlag fl) {
  asserteq_debug(t->kind, NStructType);

  // clear flag
  t->flags &= ~NodeFlagCustomInit;

  // make sure we resolve ideals
  fl |= RFlagResolveIdeal | RFlagEager;

  auto typecontext = ctx->typecontext; // save

  for (u32 i = 0; i < t->t.struc.a.len; i++) {
    Node* field = t->t.struc.a.v[i];
    field = resolve_type(ctx, field, fl);
    t->t.struc.a.v[i] = field;

    // if (field->field.init) {
    //   ctx->typecontext = field->type;
    //   field->field.init = resolve_type(ctx, field->field.init, fl);
    //   //field->flags &= ~NodeFlagCustomInit; // not neccessary, but nice for consistency
    //   if (!field->type)
    //     field->type = field->field.init->type;
    // }

    assertnotnull_debug(field->type);
    if (!is_type_complete(field->type)) {
      ctx->typecontext = field->type; // in case it changed above
      field->type = resolve_type(ctx, field->type, fl);
    }
  }

  ctx->typecontext = typecontext; // restore
  // t->type = Type_type;
  t->type = NewTypeType(ctx->build->mem, t);

  return t;
}


static Type* nullable resolve_selector_struct_field(ResCtx* ctx, Node* seln, Type* st) {
  asserteq_debug(st->kind, NStructType);
  asserteq_debug(seln->kind, NSelector);

  for (u32 i = 0; i < st->t.struc.a.len; i++) {
    Node* field = st->t.struc.a.v[i];
    if (field->field.name == seln->sel.member) {
      TARRAY_APPEND(&seln->sel.indices, ctx->build->mem, i);
      return field->type;
    }
  }

  // look for field in "parent" base structs e.g. "A{x T};B{A};b.x" => T
  u32 ii = seln->sel.indices.len;
  TARRAY_APPEND(&seln->sel.indices, ctx->build->mem, 0); // preallocate
  for (u32 i = 0; i < st->t.struc.a.len; i++) {
    Node* field = st->t.struc.a.v[i];
    Type* t;
    if ((field->flags & NodeFlagBase) &&
        field->type->kind == NStructType &&
        (t = resolve_selector_struct_field(ctx, seln, field->type)) )
    {
      seln->sel.indices.v[ii] = i;
      return t;
    }
  }

  // not found
  seln->sel.indices.len--; // undo preallocation
  return NULL;
}


static Node* resolve_selector(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NSelector);

  n->sel.operand = resolve_type(ctx, n->sel.operand, fl);
  Node* recvt = assertnotnull_debug(n->sel.operand->type);

  // if the receiver is a struct, attempt to resolve field
  if (recvt->kind == NStructType) {
    n->type = resolve_selector_struct_field(ctx, n, recvt);
    if (!n->type) {
      build_errf(ctx->build, NodePosSpan(n),
        "no member %s in %s", n->sel.member, fmtnode(recvt));
      n->type = Type_nil;
    }
    return n;
  }

  // if resolve field on struct failed, treat as call (convert n to NCall).
  panic("TODO: convert n to NCall");

  // note: don't treat as call when it's the target of assignment.
  n->type = Type_nil; // FIXME

  return n;
}


static Node* resolve_index_tuple(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NIndex);
  asserteq_debug(n->index.operand->type->kind, NTupleType);

  Node* zn = eval_usize(ctx, n->index.index);

  if (R_UNLIKELY(zn == NULL)) {
    build_errf(ctx->build, NodePosSpan(n->index.index),
      "%s is not a compile-time expression", fmtnode(n->index.index));
    node_diag_trail(ctx->build, DiagNote, n->index.index);
    return n;
  }

  n->index.index = zn; // note: zn->val.i holds valid index
  Type* rtype = n->index.operand->type;
  u64 index = zn->val.i;

  if (R_UNLIKELY(index >= (u64)rtype->t.tuple.a.len)) {
    build_errf(ctx->build, NodePosSpan(n->index.index),
      "no element %s in %s", fmtnode(zn), fmtnode(n));
    node_diag_trail(ctx->build, DiagNote, n->index.index);
    return n;
  }

  n->type = rtype->t.tuple.a.v[index];

  return n;
}


static Node* resolve_index(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NIndex);
  n->index.operand = resolve_type(ctx, n->index.operand, fl);

  auto typecontext = typecontext_set(ctx, Type_usize);
  n->index.index = resolve_type(ctx, n->index.index, fl | RFlagResolveIdeal | RFlagEager);
  ctx->typecontext = typecontext; // restore

  Type* rtype = n->index.operand->type;
  switch (rtype->kind) {
    case NArrayType:
      assertnotnull_debug(rtype->t.array.subtype);
      n->type = rtype->t.array.subtype;
      break;

    case NTupleType:
      R_MUSTTAIL return resolve_index_tuple(ctx, n, fl);

    default:
      build_errf(ctx->build, NodePosSpan(n),
        "cannot access %s of type %s by index",
        fmtnode(n->index.operand), fmtnode(rtype));
  }
  return n;
}


static Node* resolve_slice(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NSlice);
  n->slice.operand = resolve_type(ctx, n->slice.operand, fl);
  fl |= RFlagResolveIdeal | RFlagEager;
  if (n->slice.start)
    n->slice.start = resolve_type(ctx, n->slice.start, fl);
  if (n->slice.end)
    n->slice.end = resolve_type(ctx, n->slice.end, fl);
  panic("TODO");
  return n;
}


static Node* resolve_var(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NVar);
  assertnotnull_debug(n->var.init);
  assert_debug( ! NodeIsMacroParam(n)); // macro params should be typed already

  // // leave unused Var untyped
  // if (n->var.nrefs == 0)
  //   return n;

  n->var.init = resolve_type(ctx, n->var.init, fl);
  n->type = assertnotnull_debug(n->var.init->type);
  return n;
}


static Node* resolve_field(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NField);

  if (n->field.init) {
    n->flags &= ~NodeFlagCustomInit;

    auto typecontext = typecontext_set(ctx, n->type);

    n->field.init = resolve_type(ctx, n->field.init, fl);

    if (n->field.init->type != n->type) {
      build_errf(ctx->build, NodePosSpan(n->field.init),
        "value of type %s where type %s is expected",
        fmtnode(n->field.init->type), fmtnode(n->type));
      if (n->type->kind == NBasicType) {
        // suggest type cast: x + (y as int)
        build_notef(ctx->build, NodePosSpan(n->op.right),
          "try a type cast: %s(%s)", fmtnode(n->type), fmtnode(n->field.init));
      }
    }

    ctx->typecontext = typecontext; // restore typecontext
  }

  return n;
}


// ————————————————————————————————————————————————————————————————————————————
// resolve_type

#ifdef DEBUG_MODULE
// wrap resolve_type to print return value
static Node* _resolve_type(ResCtx* ctx, Node* n, RFlag fl);
static Node* resolve_type(ResCtx* ctx, Node* n, RFlag fl) {
  assert(n != NULL);
  dlog_mod("○ %s %s (%p, class %s, type %s%s%s)",
    NodeKindName(n->kind), fmtnode(n),
    n,
    NodeClassStr(NodeKindClass(n->kind)),
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
  } else {
    if (n->flags & NodeFlagRValue)
      fl |= RFlagResolveIdeal | RFlagEager;

    if (n->type) {
      // Has type already. Constant literals might have ideal type.
      if (n->type == Type_ideal) {
        if ((fl & RFlagResolveIdeal) && ((fl & RFlagEager) || ctx->typecontext)) {
          if (ctx->typecontext)
            return resolve_ideal_type(ctx, n, ctx->typecontext, fl);
          n = NodeCopy(ctx->build->mem, n);
          assert_debug(NodeHasNVal(n));
          n->type = IdealType(n->val.ct);
        }
        // else: leave as ideal, for now
        return n;
      } else if (n->flags & NodeFlagCustomInit) {
        // clear flag and continue
        //n->flags &= ~NodeFlagCustomInit;
      } else {
        if (!is_type_complete(n->type))
          n->type = resolve_type(ctx, n->type, fl);
        return n;
      }
    }
  }

  // branch on node kind
  switch (n->kind) {

  // uses Node.cunit
  case NPkg:
  case NFile:
    // File and Pkg are special in that types do not propagate
    for (u32 i = 0; i < n->cunit.a.len; i++)
      n->cunit.a.v[i] = resolve_type(ctx, (Node*)n->cunit.a.v[i], fl);
    // Note: Instead of setting n->type=Type_nil, leave as NULL and return early
    // to avoid check for null types.
    return n;

  case NBlock:
    R_MUSTTAIL return resolve_block_type(ctx, n, fl);

  case NArray:
    R_MUSTTAIL return resolve_array(ctx, n, fl);

  case NTuple:
    R_MUSTTAIL return resolve_tuple(ctx, n, fl);

  case NFun:
    R_MUSTTAIL return resolve_fun(ctx, n, fl);

  case NMacro:
    R_MUSTTAIL return resolve_macro(ctx, n, fl);

  case NPostfixOp:
  case NPrefixOp:
    n->op.left = resolve_type(ctx, n->op.left, fl);
    n->type = n->op.left->type;
    break;

  case NReturn:
    R_MUSTTAIL return resolve_ret_type(ctx, n, fl);

  case NBinOp:
  case NAssign:
    R_MUSTTAIL return resolve_binop_or_assign(ctx, n, fl);

  case NTypeCast:
    R_MUSTTAIL return resolve_typecast(ctx, n, fl);

  case NCall:
    R_MUSTTAIL return resolve_call(ctx, n, fl);

  case NVar:
    R_MUSTTAIL return resolve_var(ctx, n, fl);

  case NField:
    R_MUSTTAIL return resolve_field(ctx, n, fl);

  case NIf:
    R_MUSTTAIL return resolve_if(ctx, n, fl);

  case NId:
    R_MUSTTAIL return resolve_id(ctx, n, fl);

  case NSelector:
    R_MUSTTAIL return resolve_selector(ctx, n, fl);

  case NIndex:
    R_MUSTTAIL return resolve_index(ctx, n, fl);

  case NSlice:
    R_MUSTTAIL return resolve_slice(ctx, n, fl);

  case NArrayType:
    R_MUSTTAIL return resolve_array_type(ctx, n, fl);

  case NStructType:
    R_MUSTTAIL return resolve_struct_type(ctx, n, fl);

  case NIntLit:
  case NFloatLit:
    if (fl & RFlagResolveIdeal) {
      if (ctx->typecontext) {
        ConvlitFlags clfl = (fl & RFlagExplicitTypeCast) ? ConvlitExplicit : ConvlitImplicit;
        return convlit(ctx->build, n, ctx->typecontext, clfl | ConvlitRelaxedType);
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
  case NTypeType:
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
