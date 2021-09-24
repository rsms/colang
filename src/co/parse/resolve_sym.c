// Resolve identifiers in an AST. Usuaully run right after parsing.
#include "../common.h"
#include "parse.h"


// DEBUG_MODULE: define to enable trace logging
//#define DEBUG_MODULE ""

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


static Node* resolve_id(ResCtx* ctx, Node* n) {
  assert(n->kind == NId);

  Node* target = n->ref.target;
  if (target) {
    n->ref.target = resolve_sym(ctx, target);
    NodeClearUnresolved(n);
    return n;
  }

  // lookup name
  target = (Node*)ScopeLookup(ctx->lookupscope, n->ref.name);

  if (R_UNLIKELY(target == NULL)) {
    dlog_mod("LOOKUP %s FAILED", n->ref.name);
    build_errf(ctx->build, NodePosSpan(n), "undefined symbol %s", n->ref.name);
    n->ref.target = (Node*)NodeBad;
    return n;
  }

  NodeClearUnresolved(n);
  n->ref.target = target = resolve_sym(ctx, target);
  n->type = target->type;
  NodeClearUnused(target);

  if (target->kind == NVar)
    NodeRefVar(target);

  dlog_mod("LOOKUP %s => (N%s) %s",
    n->ref.name, NodeKindName(target->kind), fmtnode(target));

  return n;
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
    dlog_mod("< resolve (N%s) %s => (N%s) %s",
      NodeKindName(n->kind), fmtnode(n), NodeKindName(n2->kind), fmtnode(n2));
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

  // ref
  case NId:
    return resolve_id(ctx, n);

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

  case NFun:
    if (n->fun.params)
      n->fun.params = resolve_sym(ctx, n->fun.params);
    if (n->fun.result)
      n->fun.result = resolve_sym(ctx, n->fun.result);
    if (n->type)
      n->type = resolve_sym(ctx, n->type);
    // Note: Don't update lookupscope as a function's parameters should always be resolved
    if (n->fun.body)
      n->fun.body = resolve_sym(ctx, n->fun.body);
    break;

  case NMacro:
    if (n->macro.params)
      n->macro.params = resolve_sym(ctx, n->macro.params);
    // Note: Don't update lookupscope as a macro's parameters should always be resolved
    n->macro.template = resolve_sym(ctx, n->macro.template);
    break;

  case NAssign: {
    ctx->assignNest++;
    n->op.left = resolve_sym(ctx, n->op.left);
    ctx->assignNest--;
    assert(n->op.right != NULL);
    n->op.right = resolve_sym(ctx, n->op.right);
    break;
  }
  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
  case NReturn: {
    n->op.left = resolve_sym(ctx, n->op.left);
    if (n->op.right)
      n->op.right = resolve_sym(ctx, n->op.right);
    break;
  }

  case NTypeCast:
  case NCall:
    if (n->call.args)
      n->call.args = resolve_sym(ctx, n->call.args);
    n->call.receiver = resolve_sym(ctx, n->call.receiver);
    break;

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

  case NIf:
    n->cond.cond = resolve_sym(ctx, n->cond.cond);
    n->cond.thenb = resolve_sym(ctx, n->cond.thenb);
    if (ctx->flags & ParseOpt)
      n = ast_opt_ifcond(n);
    break;

  case NArrayType:
    if (n->t.array.sizeExpr)
      n->t.array.sizeExpr = resolve_sym(ctx, n->t.array.sizeExpr);
    n->t.array.subtype = resolve_sym(ctx, n->t.array.subtype);
    break;

  case NStructType:
    return resolve_arraylike_node(ctx, n, &n->t.struc.a);

  case NNone:
  case NBad:
  case NBasicType:
  case NFunType:
  case NTupleType:
  case NTypeType:
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

