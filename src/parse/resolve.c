// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

// CO_PARSE_RESOLVE_DEBUG: define to enable trace logging
#define CO_PARSE_RESOLVE_DEBUG

//———————————————————————————————————————————————————————————————————————————————————————
// resolve_id impl

static Node* simplify_id(IdNode* id, Node* nullable target);


Node* resolve_id(IdNode* id, Node* nullable target) {
  assertnotnull(id->name);

  if (target == NULL) {
    if (id->target) {
      // simplify already-resolved id, which must be flagged as an rvalue
      assert(NodeIsRValue(id));
      MUSTTAIL return simplify_id(id, target);
    }
    // mark id as unresolved
    NodeSetUnresolved(id);
    return as_Node(id);
  }

  assertnull(id->target);
  id->target = target;
  id->type = TypeOfNode(target);

  switch (target->kind) {
    case NMacro:
    case NFun:
      // Note: Don't transfer "unresolved" attribute of functions
      break;
    case NLocal_BEG ... NLocal_END:
      // increment local's ref count
      NodeRefLocal((LocalNode*)target);
      FALLTHROUGH;
    default:
      NodeTransferUnresolved(id, target);
  }

  // set id const == target const
  id->flags = (id->flags & ~NF_Const) | (target->flags & NF_Const);

  MUSTTAIL return simplify_id(id, target);
}

// simplify_id returns an Id's target if it's a simple constant, or the id itself.
// This reduces the complexity of common code.
//
// For example:
//   var x bool
//   x = true
// Without simplifying these ids the AST would look like this:
//   (Local x (Id bool -> (BasicType bool)))
//   (Assign (Id x -> (Local x)) (Id true -> (BoolLit true)))
// With simplify_id, the AST would instead look like this:
//   (Local (Id x) (BasicType bool))
//   (Assign (Local x) (BoolLit true))
//
// Note:
//   We would need to make sure post_resolve_id uses the same algorithm to get the
//   same outcome for cases like this:
//     fun foo()   | (Fun foo
//       x = true  |   (Assign (Id x -> ?) (BoolLit true)) )
//     var x bool  | (Local x (BasicType bool))
//
static Node* simplify_id(IdNode* id, Node* nullable _ign) {
  assertnotnull(id->target);

  // unwind local targeting a type
  Node* tn = id->target;
  while (NodeIsConst(tn) &&
         (is_VarNode(tn) || is_ConstNode(tn)) &&
         LocalInitField(tn) != NULL )
  {
    tn = as_Node(LocalInitField(tn));
    // Note: no NodeUnrefLocal here
  }

  // when the id is an rvalue, simplify its target no matter what kind it is
  if (NodeIsRValue(id)) {
    id->target = tn;
    id->type = TypeOfNode(tn);
  }

  // if the target is a pimitive constant, use that instead of the id node
  switch (id->target->kind) {
    case NNil:
    case NBasicType:
    case NBoolLit:
      return id->target;
    default:
      return (Node*)id;
  }
}

//———————————————————————————————————————————————————————————————————————————————————————
// resolve_ast impl
#undef resolve_ast

#if !defined(CO_NO_LIBC) && defined(CO_PARSE_RESOLVE_DEBUG)
  #include <unistd.h> // isatty
#endif

typedef u32 rflag;
enum rflag {
  flExplicitTypeCast = 1 << 0,
  flResolveIdeal     = 1 << 1,  // set when resolving ideal types
  flEager            = 1 << 2,  // set when resolving eagerly
} END_ENUM(rflag)

// resolver state
typedef struct R {
  BuildCtx* build;
  Scope*    lookupscope; // scope to look up undefined symbols in

  // typecontext is the "expected" type, if any.
  // E.g. the type of a var while resolving its rvalue.
  Type* nullable typecontext; // current value; top of logical typecontext stack

  ExprArray funstack; // stack of Node*[kind==NFun] -- current function scope
  Expr*     funstack_storage[8];

  bool explicitTypeCast;

  #ifdef CO_PARSE_RESOLVE_DEBUG
  int debug_depth;
  #endif
} R;

#define FMTNODE(n,bufno) \
  fmtnode(n, r->build->tmpbuf[bufno], sizeof(r->build->tmpbuf[bufno]))


#ifdef CO_PARSE_RESOLVE_DEBUG
  #ifdef CO_NO_LIBC
    #define isatty(fd) false
  #endif
  #define dlog2(format, args...) ({                                                 \
    if (isatty(2)) log("\e[1;31m▍\e[0m%*s" format, (r)->debug_depth*2, "", ##args); \
    else           log("[resolve] %*s" format, (r)->debug_depth*2, "", ##args);     \
    fflush(stderr); })
#else
  #define dlog2(...) ((void)0)
#endif


static Node* _resolve(R* r, rflag fl, Node* n);


#ifdef CO_PARSE_RESOLVE_DEBUG
  static Node* resolve_debug(R* r, rflag fl, Node* n) {
    char tmpbuf[256];
    assert(n != NULL);
    dlog2("○ %s %s (%p%s%s%s%s%s)",
      nodename(n), FMTNODE(n,0),
      n,

      is_Expr(n) ? " type=" : "",
      is_Expr(n) ? FMTNODE(((Expr*)n)->type,1) : "",

      r->typecontext ? " typecontext=" : "",
      r->typecontext ? fmtnode(r->typecontext, tmpbuf, sizeof(tmpbuf)) : "",

      NodeIsRValue(n) ? " rvalue" : ""
    );

    r->debug_depth++;
    Node* n2 = _resolve(r, fl, n);
    r->debug_depth--;

    if (n == n2) {
      dlog2("● %s %s resolved", nodename(n), FMTNODE(n,0));
    } else {
      dlog2("● %s %s resolved => %s", nodename(n), FMTNODE(n,0), FMTNODE(n2,1));
    }
    return n2;
  }
  #define resolve(r,fl,n) resolve_debug((r),(fl),as_Node(n))
#else
  #define resolve(r,fl,n) _resolve((r),(fl),as_Node(n))
#endif


//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve_sym

#define resolve_sym(r,fl,n)  _resolve_sym((r),(fl),as_Node(n))
static Node* _resolve_sym(R* r, rflag fl, Node* n);


static void resolve_sym_array(R* r, rflag fl, const NodeArray* a) {
  for (u32 i = 0; i < a->len; i++)
    a->v[i] = resolve_sym(r, fl, a->v[i]);
}


static Node* _resolve_sym1(R* r, rflag fl, Node* np) {
  NodeClearUnresolved(np); // do this up front to allow tail calls

  // resolve type first
  if (is_Expr(np) && ((Expr*)np)->type && NodeIsUnresolved(((Expr*)np)->type))
    ((Expr*)np)->type = as_Type(resolve_sym(r, fl, ((Expr*)np)->type));

  #define _(NAME)  return np; } case N##NAME: { \
    UNUSED auto n = (NAME##Node*)np;
  #define _G(NAME) return np; } case N##NAME##_BEG ... N##NAME##_END: { \
    UNUSED auto n = (struct NAME##Node*)np;
  switch (np->kind) { case NBad: {

  _(Comment) break; // unexpected

  _G(CUnit)
    Scope* lookupscope = r->lookupscope; // save
    if (n->scope)
      r->lookupscope = n->scope;
    resolve_sym_array(r, fl, &n->a);
    r->lookupscope = lookupscope; // restore

  _(Field)      panic("TODO %s", nodename(n));

  _(Nil)        panic("TODO %s", nodename(n));
  _(BoolLit)    panic("TODO %s", nodename(n));
  _(IntLit)     panic("TODO %s", nodename(n));
  _(FloatLit)   panic("TODO %s", nodename(n));
  _(StrLit)     panic("TODO %s", nodename(n));

  _(Id)
    Node* target = ScopeLookup(r->lookupscope, n->name);
    if UNLIKELY(target == NULL) {
      dlog2("LOOKUP %s FAILED", n->name);
      b_errf(r->build, NodePosSpan(n), "undefined symbol %s", n->name);
      n->target = kNode_bad;
      return np;
    }
    if (NodeIsUnused(target)) // must check to avoid editing universe
      NodeClearUnused(target);
    return resolve_id(n, target);

  _(BinOp)
    n->left  = as_Expr(resolve_sym(r, fl, n->left));
    n->right = as_Expr(resolve_sym(r, fl, n->right));

  _G(UnaryOp)
    n->expr = as_Expr(resolve_sym(r, fl, n->expr));

  _(Return)
    n->expr = as_Expr(resolve_sym(r, fl, n->expr));

  _(Assign)
    n->dst = as_Expr(resolve_sym(r, fl, n->dst));
    n->val = as_Expr(resolve_sym(r, fl, n->val));

  _G(ListExpr)
    resolve_sym_array(r, fl, as_NodeArray(&n->a));

  _(Fun)
    if (n->params)
      n->params = as_TupleNode(resolve_sym(r, fl, n->params));
    if (n->result)
      n->result = as_Type(resolve_sym(r, fl, n->result));
    // Note: Don't update lookupscope as a function's parameters should always be resolved
    if (n->body)
      n->body = as_Expr(resolve_sym(r, fl, n->body));

  _(Macro)      panic("TODO %s", nodename(n));
  _(Call)       panic("TODO %s", nodename(n));
  _(TypeCast)   panic("TODO %s", nodename(n));

  _G(Local)
    if (LocalInitField(n))
      SetLocalInitField(n, as_Expr(resolve_sym(r, fl, LocalInitField(n))));

  _(Ref)        panic("TODO %s", nodename(n));
  _(NamedArg)   panic("TODO %s", nodename(n));
  _(Selector)   panic("TODO %s", nodename(n));
  _(Index)      panic("TODO %s", nodename(n));
  _(Slice)      panic("TODO %s", nodename(n));
  _(If)         panic("TODO %s", nodename(n));

  _(TypeType)   panic("TODO %s", nodename(n));
  _(NamedType)  panic("TODO %s", nodename(n));
  _(AliasType)  panic("TODO %s", nodename(n));
  _(RefType)    panic("TODO %s", nodename(n));
  _(BasicType)  panic("TODO %s", nodename(n));
  _(ArrayType)  panic("TODO %s", nodename(n));
  _(TupleType)  panic("TODO %s", nodename(n));
  _(StructType) panic("TODO %s", nodename(n));
  _(FunType)    panic("TODO %s", nodename(n));

  }}
  #undef _
  #undef _G
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  UNREACHABLE;
}


inline static Node* _resolve_sym(R* r, rflag fl, Node* np) {
  if (!NodeIsUnresolved(np))
    return np;
  MUSTTAIL return _resolve_sym1(r, fl, np);
}

// end resolve_sym
//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve_type

#define resolve_type(r,fl,n) _resolve_type((r),(fl),as_Node(n))
static Node* _resolve_type(R* r, rflag fl, Node* n);

static Node* _resolve_type(R* r, rflag fl, Node* n) {
  dlog("TODO _resolve_type");
  return n;
}

// end resolve_type
//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve

static Node* _resolve(R* r, rflag fl, Node* n) {
  n = resolve_sym(r, fl, n);
  return resolve_type(r, fl, n);
}


Node* resolve_ast(BuildCtx* build, Scope* lookupscope, Node* n) {
  R r = {
    .build = build,
    .lookupscope = lookupscope,
  };
  array_init(&r.funstack, r.funstack_storage, sizeof(r.funstack_storage));

  // always one slot so we can access the top of the stack without checks
  r.funstack_storage[0] = NULL;
  r.funstack.len = 1;

  #ifdef DEBUG
  NodeKind initial_kind = n->kind;
  #endif

  n = resolve(&r, 0, n);

  array_free(&r.funstack);
  asserteq(initial_kind, n->kind); // since we typecast the result (see header file)
  return n;
}
