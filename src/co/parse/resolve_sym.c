// Resolve identifiers in an AST. Usuaully run right after parsing.
#include "../common.h"
#include "parse.h"


// DEBUG_MODULE: define to enable trace logging
//#define DEBUG_MODULE "[resolvesym] "

#ifdef DEBUG_MODULE
  #define dlog_mod(format, ...) \
    fprintf(stderr, DEBUG_MODULE "%*s " format "\n", ctx->debug_depth*2, "", ##__VA_ARGS__)
#else
  #define dlog_mod(...) do{}while(0)
#endif


typedef struct {
  Build*     build;
  ParseFlags flags;
  u32        assignNest; // level of assignment. Used to avoid early constant folding.
  Scope*     lookupscope; // scope to look up undefined symbols in

  #ifdef DEBUG_MODULE
  int debug_depth;
  #endif
} ResCtx;


static Node* resolve_sym(ResCtx* ctx, Node* n);


Node* ResolveSym(Build* build, ParseFlags fl, Node* n, Scope* scope) {
  ResCtx ctx = {
    .build = build,
    .flags = fl,
    .lookupscope = scope, // transitions to NFile scope, if an NFile is encountered
  };
  n = resolve_sym(&ctx, n);
  return n;
}


static Node* resolve_id(Node* n, ResCtx* ctx) {
  assert(n->kind == NId);
  auto name = n->ref.name;

  dlog_mod("resolve_id %s (%p)", name, n);
  while (1) {
    auto target = n->ref.target;

    if (target == NULL) {
      dlog_mod("  LOOKUP %s", n->ref.name);
      target = (Node*)ScopeLookup(ctx->lookupscope, n->ref.name);
      if (R_UNLIKELY(target == NULL)) {
        build_errf(ctx->build, NodePosSpan(n), "undefined symbol %s", name);
        n->ref.target = (Node*)NodeBad;
        return n;
      }
      n->ref.target = target;
      NodeClearUnresolved(n);
      dlog_mod("  SIMPLIFY %s => (N%s) %s",
        n->ref.name, NodeKindName(target->kind), fmtnode(target));
    }

    switch (target->kind) {
      case NId:
        // note: all built-ins which are const have targets, meaning the code above will
        // not mutate those nodes.
        n = target;
        dlog_mod("  RET id target (N%s) %s", NodeKindName(n->kind), fmtnode(n));
        break; // continue unwind loop

      case NVar: {
        // Unwind var bindings
        assert(target->var.init != NULL);
        Node* init = target->var.init;
        if ( /*NodeIsConst(init) || */ !NodeIsExpr(init)) {
          // in the case of a var target with a constant or type, resolve to that.
          // Example:
          //   "x = true ; y = x"
          //  parsed as:
          //   (Var (Id x) (BoolLit true))
          //   (Var (Id y) (Id x))
          //  transformed to:
          //   (Var (Id x) (BoolLit true))
          //   (Var (Id y) (BoolLit true))
          //
          dlog_mod("  RET var-init (N%s) %s", NodeKindName(init->kind), fmtnode(init));
          return target->var.init;
        }
        dlog_mod("  RET var (N%s) %s", NodeKindName(target->kind), fmtnode(target));
        return target;
      }

      case NBoolLit:
      case NIntLit:
      case NNil:
      case NFun:
      case NBasicType:
      case NTupleType:
      case NArrayType:
      case NStructType:
      case NFunType: {
        // unwind identifier to constant/immutable value.
        // Example:
        //   (Id true #user) -> (Id true #builtin) -> (Bool true #builtin)
        //
        dlog_mod("  RET target (N%s) %s", NodeKindName(target->kind), fmtnode(target));
        if (ctx->assignNest == 0)
          n = target;
        // assignNest is >0 when resolving the LHS of an assignment.
        // In this case we do not unwind constants as that would lead to things like this:
        //   (assign (tuple (ident a) (ident b)) (tuple (int 1) (int 2))) =>
        //   (assign (tuple (int 1) (int 2)) (tuple (int 1) (int 2)))
        return n;
      }

      default: {
        dlog_mod("resolve_id FINAL %s => N%s (target N%s) istype=%s",
          n->ref.name, NodeKindName(n->kind), NodeKindName(target->kind),
          NodeKindIsType(target->kind) ? "yes" : "no");
        dlog_mod("  RET n (N%s) %s", NodeKindName(n->kind), fmtnode(n));
        assert_debug(!NodeIsConst(target)); // should be covered in case-statements above
        return n;
      }
    }
  }
  UNREACHABLE;
}


static Node* resolve_arraylike_node(ResCtx* ctx, Node* n, NodeArray* a) {
  // n.array = map n.array (cn => resolve_sym(cn))
  for (u32 i = 0; i < a->len; i++) {
    a->v[i] = resolve_sym(ctx, (Node*)a->v[i]);
  }
  // Note: this moved to the parser:
  // // simplify blocks with a single expression; (block expr) => expr
  // if (n->array.a.len == 1 && n->kind == NBlock)
  //   n = n->array.a.v[0];
  NodeClearUnresolved(n);
  return n;
}


// TODO: improve the efficiency of this whole function.
// Currently we visit the entire AST unconditionally, doing a lot of unnecessary work
// when things are already resolved.
// Idea:
// 1. update parser to only stick a scope onto something that has unresolved refs.
// 2. update resolve_sym to only traverse subtrees with a scope.

static Node* _resolve_sym(ResCtx* ctx, Node* n);

// resolve_sym is the prelude to _resolve_sym (the real implementation), acting as a gatekeeper
// to only traverse AST's with unresolved symbols.
// In most cases the majority of a file's AST is resolved, so this saves us from a lot of
// unnecessary work.
inline static Node* resolve_sym(ResCtx* ctx, Node* n) {
  if (!NodeIsUnresolved(n))
    return n;
  return _resolve_sym(ctx, n);
}


//
// IMPORTANT: symbol resolution is only run when the parser was unable to resolve all names up-
// front. So, this code should ONLY RESOLVE stuff and apply any required transformations that the
// parser applies after resolution, like for example "Foo(3) ; Foo = int" which is parsed as a call
// to "Foo" ("Foo" is unknown) and must be converted to a TypeCast since Foo denotes a type.
//
#ifdef DEBUG_MODULE
// wrap resolve_type to print return value
static Node* _resolve_sym_dbg(ResCtx* ctx, Node* n);
static Node* _resolve_sym(ResCtx* ctx, Node* n) {
  dlog_mod("> resolve (N%s) %s", NodeKindName(n->kind), fmtnode(n));
  ctx->debug_depth++;
  Node* n2 = _resolve_sym_dbg(ctx, n);
  ctx->debug_depth--;
  if (n != n2) {
    dlog_mod("< resolve (N%s) %s => %s", NodeKindName(n->kind), fmtnode(n), fmtnode(n2));
  } else {
    dlog_mod("< resolve (N%s) %s", NodeKindName(n->kind), fmtnode(n));
  }
  return n2;
}
static Node* _resolve_sym_dbg(ResCtx* ctx, Node* n)
#else
static Node* _resolve_sym(ResCtx* ctx, Node* n)
#endif
{
  // resolve type first
  if (n->type)
    n->type = resolve_sym(ctx, n->type);

  switch (n->kind) {

  // uses u.ref
  case NId:
    return resolve_id(n, ctx);

  // array
  case NBlock:
  case NTuple:
  case NArray:
    return resolve_arraylike_node(ctx, n, &n->array.a);

  case NFile:
    return resolve_arraylike_node(ctx, n, &n->cunit.a);

  case NPkg: {
    auto lookupscope = ctx->lookupscope;
    ctx->lookupscope = n->cunit.scope;
    n = resolve_arraylike_node(ctx, n, &n->cunit.a);
    ctx->lookupscope = lookupscope;
    break;
  }

  // uses u.fun
  case NFun: {
    if (n->fun.tparams)
      n->fun.tparams = resolve_sym(ctx, n->fun.tparams);
    if (n->fun.params)
      n->fun.params = resolve_sym(ctx, n->fun.params);
    if (n->fun.result)
      n->fun.result = resolve_sym(ctx, n->fun.result);
    if (n->type)
      n->type = resolve_sym(ctx, n->type);
    // Note: Don't update lookupscope as a function's parameters should always be resolved
    auto body = n->fun.body;
    if (body)
      n->fun.body = resolve_sym(ctx, body);
    break;
  }

  // uses u.op
  case NAssign: {
    ctx->assignNest++;
    resolve_sym(ctx, n->op.left);
    ctx->assignNest--;
    assert(n->op.right != NULL);
    n->op.right = resolve_sym(ctx, n->op.right);
    break;
  }
  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
  case NReturn: {
    auto newleft = resolve_sym(ctx, n->op.left);
    if (n->op.left->kind != NId) {
      // note: in case of assignment where the left side is an identifier,
      // avoid replacing the identifier with its value.
      // This branch is taken in all other cases.
      n->op.left = newleft;
    } else {
      NodeClearUnresolved(n->op.left);
    }
    if (n->op.right) {
      n->op.right = resolve_sym(ctx, n->op.right);
    }
    break;
  }

  // uses u.call
  case NTypeCast:
  case NStructCons:
  case NCall:
    if (n->call.args)
      n->call.args = resolve_sym(ctx, n->call.args);
    n->call.receiver = resolve_sym(ctx, n->call.receiver);
    if (n->call.receiver->kind == NStructType) {
      n->kind = NStructCons;
    }
    break;

  // the following moved to type resolver:
  // case NCall:
  //   if (n->call.args)
  //     n->call.args = resolve_sym(ctx, n->call.args);
  //   auto recv = resolve_sym(ctx, n->call.receiver);
  //   // n->call.receiver = recv; // don't short circuit; messes up diagnostics
  //   if (recv->kind != NFun) {
  //     // convert to type cast, if receiver is a type. e.g. "x = uint8(4)"
  //     if (recv->kind == NBasicType) {
  //       n->kind = NTypeCast;
  //     } else if (recv->kind != NId) {
  //       build_errf(ctx->build, NodePosSpan(recv), "cannot call %s", fmtnode(recv));
  //     }
  //   }
  //   break;

  case NVar:
    if (n->var.init)
      n->var.init = resolve_sym(ctx, n->var.init);
    break;

  case NField:
    if (n->field.init)
      n->field.init = resolve_sym(ctx, n->field.init);
    break;

  case NSelector:
    n->sel.operand = resolve_sym(ctx, n->sel.operand);
    break;

  case NIndex:
    n->index.operand = resolve_sym(ctx, n->index.operand);
    n->index.index = resolve_sym(ctx, n->index.index);
    break;

  case NSlice:
    n->slice.operand = resolve_sym(ctx, n->slice.operand);
    if (n->slice.start)
      n->slice.start = resolve_sym(ctx, n->slice.start);
    if (n->slice.end)
      n->slice.end = resolve_sym(ctx, n->slice.end);
    break;

  // uses u.cond
  case NIf:
    n->cond.cond = resolve_sym(ctx, n->cond.cond);
    n->cond.thenb = resolve_sym(ctx, n->cond.thenb);
    if (ctx->flags & ParseOpt)
      n = ast_opt_ifcond(n);
    break;

  case NArrayType: {
    if (n->t.array.sizeExpr)
      n->t.array.sizeExpr = resolve_sym(ctx, n->t.array.sizeExpr);
    n->t.array.subtype = resolve_sym(ctx, n->t.array.subtype);
    break;
  }

  case NStructType:
    return resolve_arraylike_node(ctx, n, &n->t.struc.a);

  case NNone:
  case NBad:
  case NBasicType:
  case NFunType:
  case NTupleType:
  case NNil:
  case NBoolLit:
  case NIntLit:
  case NFloatLit:
  case NStrLit:
  case _NodeKindMax:
    break;

  } // switch n->kind

  NodeClearUnresolved(n);
  return n;
}

