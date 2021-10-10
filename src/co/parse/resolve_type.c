// Resolve types in an AST. Usuaully run after Parse() and ResolveSym()
#include "../common.h"
#include "../util/array.h"
#include "../util/stk_array.h"
#include "../util/str_extras.h"
#include "parse.h"

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


ATTR_FORMAT(printf,4,5)
static Node* resolve_failed(ResCtx* ctx, Node* n, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  build_diagv(ctx->build, DiagError, pos, fmt, ap);
  va_end(ap);

  if (n->type == NULL)
    n->type = Type_nil;

  return n;
}


typedef struct TypeMismatchReport TypeMismatchReport;
struct TypeMismatchReport {
  Type*          ltype;  // expected/destination type
  Type*          rtype;  // actual/source type
  Node* nullable rvalue; // if set, may be used to suggest a fix, e.g. a type cast
  PosSpan        pos;    // unless {NoPos,*}, focus source code "pointer" here

  // msg can be set to customize str_fmtpat message.
  // Available vars:
  //   {ltype}  expected/destination type e.g. "i64", "[int 3]"
  //   {rtype}  actual/source type
  //   {rvalue} rvalue
  const char* nullable msg;
};


static bool report_type_mismatch(ResCtx* ctx, const TypeMismatchReport* r) {
  Type* ltype = assertnotnull(r->ltype);
  Type* rtype = assertnotnull(r->rtype);
  Node* nullable rvalue = r->rvalue;
  const char* msg = r->msg;

  if (!msg) // default message
    msg = "mismatched types {ltype} and {rtype}";

  // format AST nodes
  Str s1 = str_new(64);
  u32 ltypeoffs  = str_len(s1); s1 = str_appendc(NodeStr(s1, ltype), '\0');
  u32 rtypeoffs  = str_len(s1); s1 = str_appendc(NodeStr(s1, rtype), '\0');
  u32 rvalueoffs = str_len(s1); s1 = str_appendc(NodeStr(s1, rvalue), '\0');
  const char* kv[] = {
    "ltype", &s1[ltypeoffs],
    "rtype", &s1[rtypeoffs],
    "rvalue", &s1[rvalueoffs],
  };
  Str s2 = str_fmtpat(str_new(64), ctx->build->mem, msg, countof(kv), kv);

  // source position
  PosSpan pos = r->pos;
  if (pos.start == NoPos)
    pos = NodePosSpan(rvalue ? rvalue : rtype);

  // report to build session
  build_errf(ctx->build, pos, "%s", s2);

  str_free(s2);
  str_free(s1);

  // if rvalue is provided, suggest a fix if possible
  if (rvalue) {
    if (rvalue->type && rvalue->type->kind == NArrayType) {
      // array; suggest a slice if the sizes are known and compatible, but only
      // if the rvalueue is not a literal (or it's better to take elements off.)
      if (ltype->kind == NArrayType &&
          rtype->kind == NArrayType &&
          rvalue->kind != NArray &&
          ltype->t.array.size < rtype->t.array.size )
      {
        build_notef(ctx->build, NodePosSpan(rvalue),
          "try a slice: %s[:%u]", fmtnode(rvalue), ltype->t.array.size);
      }
    } else {
      build_notef(ctx->build, NodePosSpan(rvalue),
        "try a type cast: %s(%s)", fmtnode(ltype), fmtnode(rvalue));
    }
  }

  return false;
}


inline static bool check_type_eq(
  ResCtx* ctx,
  Type* expect,
  Type* subject,
  Node* nullable rvalue,
  const char* nullable msg)
{
  if (R_UNLIKELY(!TypeEquals(ctx->build, subject, expect))) {
    TypeMismatchReport r = { .ltype=expect, .rtype=subject, .rvalue=rvalue, .msg=msg };
    return report_type_mismatch(ctx, &r);
  }
  return true;
}


// resolve_ideal_type resolves the concrete type of n.
// If typecontext is provided, convlit is used to "fit" n into that type.
// Otherwise the natural concrete type of n is used. (e.g. int)
// n is assumed to be Type_ideal and must be a
// node->kind = NIntLit | NFloatLit | NVar | NId.
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
  n = NodeUnbox(n, /*unrefVars=*/true);
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
    assertnotnull_debug(n->fun.params->type);
    ft->t.fun.params = n->fun.params;
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

  // // make sure its type id is set as codegen relies on this
  // if (!ft->t.id)
  //   ft->t.id = GetTypeID(ctx->build, ft);

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

  fl |= RFlagResolveIdeal | RFlagEager;

  auto typecontext = ctx->typecontext; // save typecontext
  if (typecontext) {
    asserteq_debug(typecontext->kind, NArrayType);
    assertnotnull_debug(typecontext->t.array.subtype);
    n->type = typecontext;
    typecontext_set(ctx, typecontext->t.array.subtype);
    for (u32 i = 0; i < n->array.a.len; i++) {
      RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
    }
  } else {
    Type* t = NewNode(ctx->build->mem, NArrayType);
    t->t.kind = TypeKindArray;
    t->t.array.size = (u64)n->array.a.len;
    n->type = t;
    if (n->array.a.len == 0) {
      t->t.array.subtype = Type_nil;
    } else {
      // first element influences types of the others
      Node* cn = (Node*)n->array.a.v[0];
      cn = resolve_type(ctx, cn, fl);
      n->array.a.v[0] = cn;
      t->t.array.subtype = cn->type;
      typecontext_set(ctx, cn->type);
      for (u32 i = 1; i < n->array.a.len; i++) {
        RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
      }
    }
  }

  // check types
  Type* elemt = assertnotnull_debug(n->type->t.array.subtype);
  for (u32 i = 0; i < n->array.a.len; i++) {
    Node* cn = (Node*)n->array.a.v[i];
    check_type_eq(ctx, elemt, cn->type, cn,
      "incompatible type {rtype} in array of type {ltype}");
  }

  ctx->typecontext = typecontext; // restore typecontext
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


typedef enum {
  ClearConstDefault = 0,
  ClearConstStrict  = 1 << 0, // error if const var is encountered (for assignment)
} ClearConstFlags;

// clear_const marks any NVar or NField at n as being mutated
static void clear_const(ResCtx* ctx, Node* n, ClearConstFlags fl) {
  Node* nbase = n;
  while (1) {
    NodeClearConst(n);
    switch (n->kind) {
      case NIndex:
        n = n->index.operand;
        break;
      case NSelector:
        n = n->sel.operand;
        break;
      case NVar:
        if (R_UNLIKELY(n->var.isconst && (fl & ClearConstStrict))) {
          NodeSetConst(n); // undo NodeClearConst
          build_errf(ctx->build, NodePosSpan(nbase),
            "cannot mutate constant variable %s", n->var.name);
          if (n->pos != NoPos)
            build_notef(ctx->build, NodePosSpan(n), "%s defined here", n->var.name);
        }
        return;
      case NId:
        n = assertnotnull_debug(n->id.target);
        break;
      default:
        return;
    }
  }
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
  lt = assertnotnull_debug(n->op.left->type);
  rt = assertnotnull_debug(n->op.right->type);
  ctx->typecontext = typecontext; // restore typecontext

  // assignment
  if (n->op.op == TAssign) {
    // storing to a var upgrades it to mutable
    clear_const(ctx, n->op.left, ClearConstStrict);

    // storing to an array is not allowed
    if (lt->kind == NArrayType && rt->kind == NArrayType) {
      build_errf(ctx->build, NodePosSpan(n),
        "array type %s is not assignable", fmtnode(lt));
    }
  }

  // convert operand types as needed. The following code tests all branches:
  //
  //   a = 1 + 2                         # 1  left & right are ideal
  //   a = 2 + (1 as uint32)             # 2  left is ideal, right is typed
  //   a = (1 as uint32) + 2             # 3  left is typed, right is ideal
  //   a = (1 as uint32) + (2 as uint32) # 4  left & right are typed
  //
  if (lt == Type_ideal) {
    /*if (n->op.op == TAssign) {
      dlog_mod("[binop] 1  left is ideal and an assignment target");
      Node* left = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      dlog("left %s", fmtast(left));
    } else*/ if (rt == Type_ideal) {
      dlog_mod("[binop] 2  left & right are ideal");
      // TODO: we could pick the strongest type here by finding the CType of each operand and
      // then calling resolve_ideal_type on the stronger of the two. For example int32 > int16.
      n->op.left = resolve_ideal_type(ctx, n->op.left, ctx->typecontext, fl);
      lt = n->op.left->type;
      // note: continue to statement outside these if blocks
    } else {
      dlog_mod("[binop] 3  left is ideal, right is typed (%s)", fmtnode(rt));
      n->op.left = convlit(
        ctx->build, n->op.left, rt, ConvlitImplicit | ConvlitRelaxedType);
      n->type = rt;
      return finalize_binop(ctx, n);
    }
  } else if (rt == Type_ideal) {
    dlog_mod("[binop] 4  left is typed (%s), right is ideal", fmtnode(lt));
    n->op.right = convlit(
      ctx->build, n->op.right, lt, ConvlitImplicit | ConvlitRelaxedType);
    n->type = lt;
    return finalize_binop(ctx, n);
  } else {
    dlog_mod("[binop] 5  left & right are typed (%s, %s)", fmtnode(lt) , fmtnode(rt));
  }

  // we get here from either of the two conditions:
  // - left & right are both ideal (lhs has been resolved, above)
  // - left & right are both typed
  if (!TypeEquals(ctx->build, lt, rt)) {
    if (rt == Type_ideal) {
      dlog_mod("[binop] 6B resolve ideal type of right expr");
      n->op.right = resolve_ideal_type(ctx, n->op.right, lt, fl);
    } else {
      dlog_mod("[binop] 6B convlit right expr to type of left side (%s)", fmtnode(lt));
      n->op.right = convlit(
        ctx->build, n->op.right, lt, ConvlitImplicit | ConvlitRelaxedType);
    }

    // check if convlit failed
    check_type_eq(ctx, lt, n->op.right->type, n->op.right, NULL);
  }

  n->type = lt;
  return finalize_binop(ctx, n);
}


static Node* resolve_tuple(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NTuple);

  Type* typecontext = ctx->typecontext; // save typecontext
  fl |= RFlagResolveIdeal;

  const NodeArray* ctlist = NULL;
  u32 ctindex = 0;

  if (typecontext) {
    switch (typecontext->kind) {
      case NTupleType:
        ctlist = &typecontext->t.tuple.a;
        assert(ctlist->len > 0); // tuples should never be empty
        break;
      case NStructType:
        ctlist = &typecontext->t.struc.a;
        break;
      default:
        build_errf(ctx->build, NodePosSpan(typecontext),
          "unexpected context type %s", fmtnode(typecontext));
        goto end;
    }

    if (R_UNLIKELY(ctlist->len != n->array.a.len)) {
      build_errf(ctx->build, NodePosSpan(n),
        "%u expressions where %u expressions are expected %s",
        n->array.a.len, ctlist->len, fmtnode(typecontext));
      goto end;
    }
  }

  Type* tt = NewNode(ctx->build->mem, NTupleType);

  // for each tuple entry
  for (u32 i = 0; i < n->array.a.len; i++) {
    if (ctlist) {
      assert_debug(ctindex < ctlist->len);
      Node* ct = ctlist->v[ctindex++];
      typecontext_set(ctx, NodeIsType(ct) ? ct : assertnotnull_debug(ct->type));
    }
    if (!n->array.a.v[i]) {
      if (ctx->typecontext) {
        NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, ctx->typecontext);
      } else {
        NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, (Node*)NodeBad);
        build_errf(ctx->build, NodePosSpan(n),
          "unable to infer type of tuple element %u", i);
      }
    } else {
      Node* cn = RESOLVE_ARRAY_NODE_TYPE_MUT(&n->array.a, i, fl);
      if (R_UNLIKELY(!cn->type)) {
        cn->type = (Node*)NodeBad;
        build_errf(ctx->build, NodePosSpan(cn), "unknown type");
      }
      NodeArrayAppend(ctx->build->mem, &tt->t.tuple.a, cn->type);
    }
  }

  n->type = tt;

end:
  ctx->typecontext = typecontext; // restore typecontext
  return n;
}


static bool is_named_params(Node* collection) {
  switch (collection->kind) {
    case NTuple:
      if (collection->array.a.len == 0)
        return false;
      Node* param0 = collection->array.a.v[0];
      return param0->kind == NVar && param0->var.name != sym__;

    case NStructType:
      // note: fields always has a name and are not permitted to be named "_"
      return collection->t.struc.a.len > 0;

    default:
      return false;
  }
}


static Node* nullable find_named_param_tuple(Node* n, Sym name, u32* index_out) {
  asserteq_debug(n->kind, NTuple);
  for (u32 i = 0; i < n->array.a.len; i++) {
    Node* cn = (Node*)n->array.a.v[i];
    asserteq_debug(cn->kind, NVar);
    if (cn->var.name == name) {
      *index_out = i;
      return cn;
    }
  }
  return NULL;
}


static Node* nullable find_named_param_struct(Node* n, Sym name, u32* index_out) {
  asserteq_debug(n->kind, NStructType);
  for (u32 i = 0; i < n->t.struc.a.len; i++) {
    Node* field = (Node*)n->t.struc.a.v[i];
    asserteq_debug(field->kind, NField);
    if (field->field.name == name) {
      *index_out = i;
      return field;
    }
  }
  return NULL;
}


static Node* resolve_call_args(ResCtx* ctx, Node* call, Node* args, Node* params) {
  asserteq_debug(call->kind, NCall);
  asserteq_debug(args->kind, NTuple);
  bool hasNamedArgs = args->flags & NodeFlagNamed;

  Node* recv = assertnotnull_debug(call->call.receiver);
  Type* recvt = assertnotnull_debug(recv->type);

  u32 argc;
  STK_ARRAY_DEFINE(argv, Node*, 16); // len = argc
  STK_ARRAY_DEFINE(typv, Type*, 16); // len = argc
  __typeof__(find_named_param_tuple)* find_named_param;

  switch (params->kind) {
    case NTuple: {
      Type* paramst = assertnotnull_debug(params->type);
      args->type = paramst;
      asserteq_debug(paramst->kind, NTupleType);
      asserteq_debug(paramst->t.tuple.a.len, args->array.a.len); // len(args) == len(params)
      typv = (Type**)paramst->t.tuple.a.v;
      argc = paramst->t.tuple.a.len;
      find_named_param = find_named_param_tuple;
      // if there are named arguments, copy pointers so that we can sort them
      if (hasNamedArgs) {
        if (!is_named_params(params)) {
          // missing parameter names, eg "fun (int, bool) int"
          return resolve_failed(ctx, args, NodePosSpan(args),
            "cannot call %s %s with named parameters", fmtnode(recv), fmtnode(recvt));
        }
        STK_ARRAY_INIT(argv, ctx->build->mem, argc);
        memset(argv, 0, sizeof(void*) * argc); // zero
      } else {
        argv = (Node**)args->array.a.v; // ok since len(args) == len(params)
      }
      break;
    }
    case NStructType: {
      // struct field types are members of each field; extract them
      args->type = Type_nil; // fulfill typechecker expectations
      Node** fieldsv = (Node**)params->t.struc.a.v;
      argc = params->t.struc.a.len;
      if (R_UNLIKELY(args->array.a.len > argc)) {
        return resolve_failed(ctx, args, NodePosSpan(args),
          "extra argument in type constructor %s %s",
          fmtnode(recv), fmtnode(recvt));
      }
      STK_ARRAY_INIT(argv, ctx->build->mem, argc);
      STK_ARRAY_INIT(typv, ctx->build->mem, argc);
      memset(argv, 0, sizeof(void*) * argc); // zero
      for (u32 i = 0; i < argc; i++) {
        typv[i] = fieldsv[i]->type;
      }
      find_named_param = find_named_param_struct;
      break;
    }
    default:
      panic("unexpected argument receiver kind %s", NodeKindName(params->kind));
  }

  Type* typecontext = ctx->typecontext; // save typecontext
  u32 argsLen = args->array.a.len;
  assert_debug(argsLen <= argc);

  // resolve arguments
  u32 i = 0;
  for (; i < argsLen; i++) {
    Node* arg = (Node*)args->array.a.v[i];
    if (arg->kind == NNamedVal)
      break;
    // positional argument
    Type* paramt = assertnotnull_debug(typv[i]);
    assert_debug(NodeIsType(paramt));
    ctx->typecontext = paramt;
    arg = resolve_type(ctx, arg, RFlagResolveIdeal);
    argv[i] = assertnotnull_debug(arg);
  }

  // resolve named arguments (remaining args are named)
  for (; i < argsLen; i++) {
    Node* arg = (Node*)args->array.a.v[i];
    asserteq_debug(arg->kind, NNamedVal);
    Sym name = arg->namedval.name;

    u32 argi = 0;
    Node* param = find_named_param(params, name, &argi);

    if (R_UNLIKELY(param == NULL)) {
      build_errf(ctx->build, NodePosSpan(arg),
        "no parameter named \"%s\" in %s %s",
        name, fmtnode(recv), fmtnode(recvt));
      arg->type = Type_nil;
      goto end;
    }

    if (R_UNLIKELY(argv[argi] != NULL)) {
      build_errf(ctx->build, NodePosSpan(arg),
        "duplicate argument %s %s in call to %s %s",
        name, fmtnode(arg), fmtnode(recv), fmtnode(recvt));
      arg->type = Type_nil;
      goto end;
    }

    ctx->typecontext = assertnotnull_debug(param->type);
    arg = resolve_type(ctx, arg, RFlagResolveIdeal);
    argv[argi] = assertnotnull_debug(arg);
  }

  // check types
  for (u32 i = 0; i < argc; i++) {
    Type* paramt = typv[i];
    Node* arg = argv[i];
    if (!arg) // absent argument
      continue;
    Type* argt = assertnotnull_debug(arg->type);
    if (R_UNLIKELY(!TypeEquals(ctx->build, paramt, argt))) {
      build_errf(ctx->build, NodePosSpan(arg),
        "incompatible argument type %s, expecting %s in call to %s %s",
        fmtnode(argt), fmtnode(paramt), fmtnode(recv), fmtnode(recvt));
    }
  }

  // copy argv to args if needed
  if (argc != argsLen || hasNamedArgs) {
    if (args->array.a.cap < argc)
      ArrayGrow(&args->array.a, argc - args->array.a.cap, ctx->build->mem);
    memcpy(args->array.a.v, argv, sizeof(void*) * argc);
    args->array.a.len = argc;
  }

end:
  STK_ARRAY_DISPOSE(argv);
  STK_ARRAY_DISPOSE(typv);
  ctx->typecontext = typecontext; // restore typecontext
  return args;
}


static Node* resolve_call_fun(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NCall);
  Node* recv = assertnotnull_debug(n->call.receiver);
  Type* recvt = assertnotnull_debug(recv->type);
  asserteq_debug(recvt->kind, NFunType);

  n->type = assertnotnull_debug(recvt->t.fun.result);

  Node* nullable params = recvt->t.fun.params;

  // check input-output cardinality
  if (params) {
    assertnotnull_debug(params->type);
    asserteq_debug(params->type->kind, NTupleType);
  }
  if (n->call.args)
    asserteq_debug(n->call.args->kind, NTuple);
  u32 nparams = params ? params->type->t.tuple.a.len : 0;
  u32 nargs = n->call.args ? n->call.args->array.a.len : 0;

  if (R_UNLIKELY(nparams != nargs)) {
    const char* msgfmt = (
      nargs < nparams ? "missing argument in call to %s %s" :
                        "extra argument in call to %s %s" );
    return resolve_failed(ctx, n, NodePosSpan(n), msgfmt, fmtnode(recv), fmtnode(recvt));
  }

  // resolve input arguments
  if (n->call.args) {
    assertnotnull_debug(params); // earlier cardinality guards this case
    n->call.args = resolve_call_args(ctx, n, n->call.args, params);
  }

  return n;
}


static Node* resolve_call_type(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NCall);
  Node* tt = assertnotnull_debug(n->call.receiver->type);
  asserteq_debug(tt->kind, NTypeType);
  Node* recvt = tt->t.type;
  assert_debug(NodeIsType(recvt));

  Node* nullable args = n->call.args;
  n->type = recvt; // result of the type constructor call is the type

  if (args && args->array.a.len > 0) {
    // disallow mixed positional and named arguments in type constructors
    if (R_UNLIKELY(
      (args->flags & NodeFlagNamed) &&
      assertnotnull_debug((Node*)args->array.a.v[0])->kind != NNamedVal ))
    {
      return resolve_failed(ctx, n, NodePosSpan(args),
        "mixed positional and named initializer values");
    }

    // resolve input arguments
    n->call.args = resolve_call_args(ctx, n, n->call.args, recvt);
  }

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
  Node* recv = assertnotnull_debug(n->call.receiver);
  Type* recvt = assertnotnull_debug(recv->type);
  switch (recvt->kind) {
    case NFunType: // function call
      R_MUSTTAIL return resolve_call_fun(ctx, n, fl);
    case NTypeType: // type constructor call
      R_MUSTTAIL return resolve_call_type(ctx, n, fl | RFlagExplicitTypeCast);
    default:
      return resolve_failed(ctx, n, NodePosSpan(n->call.receiver),
        "cannot call %s %s of type %s",
        TypeKindName(recvt->t.kind), fmtnode(NodeUnbox(recv, false)), fmtnode(recvt));
  }
}


static Node* resolve_typecast(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NTypeCast);
  assertnotnull_debug(n->call.receiver);
  assertnotnull_debug(n->call.args); // note: "T()" w/o args is a call, not a cast

  if (R_UNLIKELY(!NodeIsType(n->call.receiver))) {
    return resolve_failed(ctx, n, NodePosSpan(n),
      "invalid conversion to non-type %s", fmtnode(n->call.receiver));
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
  if (R_UNLIKELY(n->id.target == NULL)) {
    // identifier failed to resolve
    n->type = Type_nil;
    return n;
  }
  n->id.target = resolve_type(ctx, n->id.target, fl);
  n->type = n->id.target->type;
  return n;
}


static void resolve_arraytype_size(ResCtx* ctx, Type* n) {
  asserteq_debug(n->kind, NArrayType);
  asserteq_debug(n->t.array.size, 0); // must not be resolved already
  assertnotnull_debug(n->t.array.sizeexpr); // must be array and not slice

  // set temporary size so that we don't cause an infinite loop
  n->t.array.size = 0xDEADBEEF;

  Node* zn = NodeEvalUint(ctx->build, n->t.array.sizeexpr);

  if (R_UNLIKELY(zn == NULL)) {
    // TODO: improve these error message to be more specific
    n->t.array.size = 0;
    zn = n->t.array.sizeexpr;
    build_errf(ctx->build, NodePosSpan(zn),
      "invalid expression %s for array size", fmtnode(zn));
    node_diag_trail(ctx->build, DiagNote, zn);
  } else {
    n->t.array.size = zn->val.i;
    n->t.array.sizeexpr = zn;
  }
}


static bool is_type_complete(Type* n) {
  switch (n->kind) {
    case NArrayType:
      return (n->t.array.sizeexpr == NULL || n->t.array.size > 0) &&
             is_type_complete(n->t.array.subtype);

    case NStructType:
      return (n->flags & NodeFlagCustomInit) == 0;

    default:
      return true;
  }
}


static Type* resolve_array_type(ResCtx* ctx, Type* n, RFlag fl) {
  asserteq_debug(n->kind, NArrayType);

  if (n->t.array.sizeexpr && n->t.array.size == 0) {
    n->t.array.sizeexpr = resolve_type(ctx, n->t.array.sizeexpr, fl);
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

  Node* zn = NodeEvalUint(ctx->build, n->index.index);

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

  auto typecontext = typecontext_set(ctx, Type_uint);
  n->index.index = resolve_type(ctx, n->index.index, fl | RFlagResolveIdeal | RFlagEager);
  ctx->typecontext = typecontext; // restore

  Type* rtype = assertnotnull_debug(n->index.operand->type);

rtype_switch:
  switch (rtype->kind) {
    case NRefType:
      // unbox reference type e.g. "&[int]" => "[int]"
      rtype = assertnotnull_debug(rtype->t.ref);
      goto rtype_switch;

    case NArrayType:
      n->type = assertnotnull_debug(rtype->t.array.subtype);
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


static void report_var_init_type_mismatch(ResCtx* ctx, Node* n) {
  TypeMismatchReport r = {
    .ltype  = n->type,
    .rtype  = n->var.init->type,
    .rvalue = n->var.init,
    .msg    = "incompatible initializer type {rtype} for var of type {ltype}",
  };

  if (r.ltype->kind == NArrayType && r.rtype->kind == NArrayType) {
    // initializing array with array
    bool isArrayLit = n->var.init->kind == NArray;
    u32 lsize = r.ltype->t.array.size;
    u32 rsize = r.rtype->t.array.size;

    if (isArrayLit && lsize > 0 && lsize < rsize) {
      r.msg = "excess element in array initializer {ltype}";
      r.pos = NodePosSpan(n->var.init->array.a.v[lsize]);
      if (rsize - lsize > 1) {
        r.msg = "excess elements in array initializer {ltype}";
        for (u32 i = lsize; i < rsize; i++)
          r.pos.end = NodePosSpan(n->var.init->array.a.v[i]).end;
      }
    }
  }

  report_type_mismatch(ctx, &r);
}


static Node* resolve_var(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NVar);
  assertnotnull_debug(n->var.init);
  assert_debug( ! NodeIsMacroParam(n)); // macro params should be typed already

  auto typecontext = typecontext_set(ctx, n->type);
  if (n->type)
    fl |= RFlagResolveIdeal | RFlagEager;
  n->var.init = resolve_type(ctx, n->var.init, fl);
  ctx->typecontext = typecontext; // restore

  if (!n->type) {
    n->type = assertnotnull_debug(n->var.init->type);
  } else if (R_UNLIKELY(!TypeEquals(ctx->build, n->type, n->var.init->type))) {
    // TODO: allow initializing with higher-fidelity type, e.g. "x [int] = [1,2,3]"
    report_var_init_type_mismatch(ctx, n);
  }

  if (R_UNLIKELY(n->type->kind == NArrayType && n->var.init->kind != NArray)) {
    // loading fixed-size arrays is not allowed
    build_errf(ctx->build, NodePosSpan(n),
      "array type %s is not assignable", fmtnode(n->type));
    // suggest a ref
    build_notef(ctx->build, NodePosSpan(n->var.init),
      "try making a reference: &%s", fmtnode(n->var.init));
  }

  return n;
}


static Node* resolve_field(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NField);
  assertnotnull_debug(n->type);

  if (n->field.init) {
    n->flags &= ~NodeFlagCustomInit;

    auto typecontext = typecontext_set(ctx, n->type);

    n->field.init = resolve_type(ctx, n->field.init, fl);

    if (R_UNLIKELY(n->field.init->type != n->type)) {
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


static Node* resolve_namedval(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NNamedVal);
  assertnotnull_debug(n->namedval.value);
  n->namedval.value = resolve_type(ctx, n->namedval.value, fl);
  n->type = assertnotnull_debug(n->namedval.value->type);
  return n;
}


static Node* resolve_ref(ResCtx* ctx, Node* n, RFlag fl) {
  asserteq_debug(n->kind, NRef);
  assertnotnull_debug(n->ref.target);
  n->ref.target = resolve_type(ctx, n->ref.target, fl);
  Type* t = NewNode(ctx->build->mem, NRefType);
  clear_const(ctx, n->ref.target, ClearConstDefault); // maybe upgrade target var to mut
  t->flags = n->ref.target->flags & NodeFlagConst;
  t->t.ref = n->ref.target->type;
  n->type = t;
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
          n = NodeCopy(ctx->build->mem, NodeUnbox(n, false));
          assert_debug(NodeHasNVal(n)); // expected to be a primitive value (e.g. int)
          n->type = IdealType(n->val.ct);
        }
        // else: leave as ideal, for now
        return n;
      }
      // it's not ideal
      // make sure its type is resolved
      if (!is_type_complete(n->type))
        n->type = resolve_type(ctx, n->type, fl);
      // now, unless n requires explicit visiting, n is done
      if ((n->flags & NodeFlagPartialType) == 0)
        return n;
    }
  }

  // clear NodeFlagPartialType
  n->flags &= ~NodeFlagPartialType;

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

  case NRef:
    R_MUSTTAIL return resolve_ref(ctx, n, fl);

  case NField:
    R_MUSTTAIL return resolve_field(ctx, n, fl);

  case NNamedVal:
    R_MUSTTAIL return resolve_namedval(ctx, n, fl);

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
  case NRefType:
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
