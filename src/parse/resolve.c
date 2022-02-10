// semantic analysis and resolution -- late symbol bindings, type resolution, simplification
#include "../coimpl.h"
#include "resolve.h"
#include "resolve_id.h"
#include "universe.h"
#undef resolve_ast

// CO_PARSE_RESOLVE_DEBUG: define to enable trace logging
#define CO_PARSE_RESOLVE_DEBUG

#if defined(CO_WITH_LIBC) && defined(CO_PARSE_RESOLVE_DEBUG)
  #include <unistd.h> // isatty
#endif

typedef struct R R;     // resolver state
typedef u32      rflag; // flags

struct R {
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
};

enum rflag {
  flExplicitTypeCast = 1 << 0,
  flResolveIdeal     = 1 << 1,  // set when resolving ideal types
  flEager            = 1 << 2,  // set when resolving eagerly
} END_TYPED_ENUM(rflag)


#ifdef CO_PARSE_RESOLVE_DEBUG
  #ifndef CO_WITH_LIBC
    #define isatty(fd) false
  #endif
  #define dlog2(format, args...) ({                                                 \
    if (isatty(2)) log("\e[1;31m▍\e[0m%*s" format, (r)->debug_depth*2, "", ##args); \
    else           log("[resolve] %*s" format, (r)->debug_depth*2, "", ##args);     \
    fflush(stderr); })
#else
  #define dlog2(...) ((void)0)
#endif


#define resolve_sym(r,fl,n)  _resolve_sym((r),(fl),as_Node(n))
#define resolve_type(r,fl,n) _resolve_type((r),(fl),as_Node(n))

static Node* _resolve(R* r, rflag fl, Node* n);
static Node* _resolve_sym(R* r, rflag fl, Node* n);
static Node* _resolve_type(R* r, rflag fl, Node* n);


#ifdef CO_PARSE_RESOLVE_DEBUG
  static Node* resolve_debug(R* r, rflag fl, Node* n) {
    assert(n != NULL);
    dlog2("○ %s %s (%p%s%s%s%s%s)",
      nodename(n), fmtnode(n),
      n,

      is_Expr(n) ? " type=" : "",
      is_Expr(n) ? fmtnode(((Expr*)n)->type) : "",

      r->typecontext ? " typecontext=" : "",
      r->typecontext ? fmtnode(r->typecontext) : "",

      NodeIsRValue(n) ? " rvalue" : ""
    );

    r->debug_depth++;
    Node* n2 = _resolve(r, fl, n);
    r->debug_depth--;

    if (n == n2) {
      dlog2("● %s %s resolved", nodename(n), fmtnode(n));
    } else {
      dlog2("● %s %s resolved => %s", nodename(n), fmtnode(n), fmtnode(n2));
    }
    return n2;
  }
  #define resolve(r,fl,n) resolve_debug((r),(fl),as_Node(n))
#else
  #define resolve(r,fl,n) _resolve((r),(fl),as_Node(n))
#endif

// --------------------------------------------------------------------------------------

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


static Node* _resolve_type(R* r, rflag fl, Node* n) {
  return n;
}


static Node* _resolve(R* r, rflag fl, Node* n) {
  n = resolve_sym(r, fl, n);
  return resolve_type(r, fl, n);
}


Node* resolve_ast(BuildCtx* build, Scope* lookupscope, Node* n) {
  R r = {
    .build = build,
    .lookupscope = lookupscope,
  };
  ExprArrayInitStorage(&r.funstack, r.funstack_storage, countof(r.funstack_storage));

  // always one slot so we can access the top of the stack without checks
  r.funstack_storage[0] = NULL;
  r.funstack.len = 1;

  #ifdef DEBUG
  NodeKind initial_kind = n->kind;
  #endif

  n = resolve(&r, 0, n);

  ExprArrayFree(&r.funstack, build->mem);
  asserteq(initial_kind, n->kind); // since we typecast the result (see header file)
  return n;
}
