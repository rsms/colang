// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

// CO_PARSE_RESOLVE_DEBUG: define to enable trace logging
#define CO_PARSE_RESOLVE_DEBUG

//———————————————————————————————————————————————————————————————————————————————————————
// resolve_id impl


Expr* resolve_id_expr(IdNode* id, Expr* target) {
  assertnotnull(id->name);
  assertnull(id->target);

  id->flags &= ~NF_Unresolved;
  id->type = unbox_id_type(TypeOfNode(target));
  id->target = target;

  switch (target->kind) {
    case NTemplate:
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

  return (Expr*)id;
}


Type* resolve_id_type(IdTypeNode* id, Type* target) {
  assertnotnull(id->name);
  assertnull(id->target);

  id->target = target;
  id->flags &= ~NF_Unresolved;
  id->flags &= ~NF_Const;
  id->flags &= (target->flags & (NF_Const | NF_Unresolved));

  if (is_LocalNode(target))
    NodeRefLocal((LocalNode*)target);

  return (Type*)id;
}



//———————————————————————————————————————————————————————————————————————————————————————
// resolve_ast impl
#undef resolve_ast

#if !defined(CO_NO_LIBC) && defined(CO_PARSE_RESOLVE_DEBUG)
  #include <unistd.h> // isatty
#endif

typedef u32 RFlag;
enum RFlag {
  RF_ExplicitTypeCast = 1 << 0,
  RF_ResolveIdeal     = 1 << 1, // set when resolving ideal types
  RF_Eager            = 1 << 2, // set when resolving eagerly
  RF_Unsafe           = 1 << 3, // in unsafe context (unsafe function body or unsafe block)
  RF_Template         = 1 << 4, // in template body

  RF_INVALID          = 0xffffffff, // specific flags value that is invalid
  _RF_UNUSED_         = 1 << 31,    // make sure we can never compose RF_INVALID
};

// resolver state
typedef struct R {
  BuildCtx* build;
  RFlag     flags;
  Scope*    lookupscope; // for looking up undefined symbols (initially build->pkg.scope)

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


// T* errf(R* r, T* origin_node, const char* fmt, ...)
#define errf(r, origin_node, fmt, args...) ({ \
  __typeof__(origin_node) n__ = (origin_node); \
  b_errf((r)->build, NodePosSpan(n__), fmt, ##args); \
  n__; \
})


// mknode allocates a new ast node
// T* mknode(R*, KIND)
#define mknode(r, KIND, pos) \
  b_mknode((r)->build, KIND, (pos))

#define mknode_array(r, KIND, pos, ARRAY_FIELD, count) \
  b_mknode_array((r)->build, KIND, (pos), ARRAY_FIELD, (count))


#define FMTNODE(n,bufno) \
  fmtnode(n, r->build->tmpbuf[bufno], sizeof(r->build->tmpbuf[bufno]))


// #define check_memalloc(r,n,ok) _check_memalloc((r),as_Node(n),(ok))
// inline static bool _check_memalloc(R* r, Node* n, bool ok) {
//   if UNLIKELY(!ok)
//     errf(r, n, "failed to allocate memory");
//   return ok;
// }


#ifdef CO_PARSE_RESOLVE_DEBUG
  #ifdef CO_NO_LIBC
    #define isatty(fd) false
  #endif
  #define dlog2(format, args...) ({                                                 \
    if (isatty(2)) log("\e[1;30m▍\e[0m%*s" format, (r)->debug_depth*2, "", ##args); \
    else           log("[resolve] %*s" format, (r)->debug_depth*2, "", ##args);     \
    fflush(stderr); })
#else
  #define dlog2(...) ((void)0)
#endif


// Node* resolve(R* r, Node* n)
static Node* _resolve(R* r, Node* n);

#ifdef CO_PARSE_RESOLVE_DEBUG
  static Node* resolve_debug(R* r, Node* n) {
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
    Node* n2 = _resolve(r, n);
    r->debug_depth--;

    tmpbuf[0] = 0;
    if (is_Expr(n)) {
      fmtnode(((Expr*)n2)->type, tmpbuf, sizeof(tmpbuf));
    } else {
      memcpy(tmpbuf, "type\0", 5);
    }

    if (n == n2) {
      dlog2("● %s %s resolved : %s", nodename(n), FMTNODE(n,0), tmpbuf);
    } else {
      dlog2("● %s %s resolved => %s : %s", nodename(n), FMTNODE(n,0), FMTNODE(n2,1), tmpbuf);
    }

    if (is_Expr(n2))
      assertf(((Expr*)n2)->type != NULL, "did not assign type to %s", nodename(n2));

    return n2;
  }
  #define resolve(r,n) resolve_debug((r),as_Node(n))
#else
  #define resolve(r,n) _resolve((r),as_Node(n))
#endif


//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve_sym

#define resolve_sym(r,n)  _resolve_sym((r),as_Node(n))
static Node* _resolve_sym1(R* r, Node* n);
inline static Node* _resolve_sym(R* r, Node* np) {
  if UNLIKELY(NodeIsUnresolved(np))
    MUSTTAIL return _resolve_sym1(r, np);
  return np;
}

static void resolve_syms_in_array(R* r, const NodeArray* a) {
  for (u32 i = 0; i < a->len; i++)
    a->v[i] = resolve_sym(r, a->v[i]);
}

static Node* _resolve_sym1(R* r, Node* np) {
  NodeClearUnresolved(np); // do this up front to allow tail calls

  // resolve type first
  if (is_Expr(np) && ((Expr*)np)->type && NodeIsUnresolved(((Expr*)np)->type))
    ((Expr*)np)->type = unbox_id_type(as_Type(resolve_sym(r, ((Expr*)np)->type)));

  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Comment) break; // unexpected

  GNCASE(CUnit)
    Scope* lookupscope = r->lookupscope; // save
    if (n->scope)
      r->lookupscope = n->scope;
    resolve_syms_in_array(r, &n->a);
    r->lookupscope = lookupscope; // restore
    return np;

  NCASE(Field)      panic("TODO %s", nodename(n));
  NCASE(Nil)        panic("TODO %s", nodename(n));
  NCASE(BoolLit)    panic("TODO %s", nodename(n));
  NCASE(IntLit)     panic("TODO %s", nodename(n));
  NCASE(FloatLit)   panic("TODO %s", nodename(n));
  NCASE(StrLit)     panic("TODO %s", nodename(n));

  NCASE(Id) {
    Node* target = ScopeLookup(r->lookupscope, n->name);
    if (UNLIKELY(!target) || UNLIKELY(!is_Expr(target))) {
      dlog2("LOOKUP expr \"%s\" FAILED", n->name);
      errf(r, n, "undefined identifier %s", n->name);
      target = (Node*)kExpr_nil;
    }
    if (NodeIsUnused(target)) // must check to avoid editing universe
      NodeClearUnused(target);
    return (Node*)resolve_id_expr(n, (Expr*)target);
  }

  NCASE(IdType) {
    Node* target = ScopeLookup(r->lookupscope, n->name);
    if (UNLIKELY(!target) || UNLIKELY(!is_Type(target))) {
      dlog2("LOOKUP type \"%s\" FAILED", n->name);
      errf(r, n, "undefined identifier %s", n->name);
      target = (Node*)kType_nil;
    }
    if (NodeIsUnused(target)) // must check to avoid editing universe
      NodeClearUnused(target);
    return (Node*)resolve_id_type(n, (Type*)target);
  }

  NCASE(BinOp)
    n->left  = as_Expr(resolve_sym(r, n->left));
    n->right = as_Expr(resolve_sym(r, n->right));
    return np;

  GNCASE(UnaryOp)
    n->expr = as_Expr(resolve_sym(r, n->expr));
    return np;

  NCASE(Return)
    n->expr = as_Expr(resolve_sym(r, n->expr));
    return np;

  NCASE(Assign)
    n->dst = as_Expr(resolve_sym(r, n->dst));
    n->val = as_Expr(resolve_sym(r, n->val));
    return np;

  GNCASE(ListExpr)
    resolve_syms_in_array(r, as_NodeArray(&n->a));
    return np;

  NCASE(Fun)
    resolve_syms_in_array(r, as_NodeArray(&n->params));
    if (n->result)
      n->result = as_Type(resolve_sym(r, n->result));
    // Note: Don't update lookupscope as a function's parameters should always be resolved
    if (n->body)
      n->body = as_Expr(resolve_sym(r, n->body));
    return np;

  NCASE(Call)
    n->receiver = resolve_sym(r, n->receiver);
    resolve_syms_in_array(r, as_NodeArray(&n->args));
    return np;

  GNCASE(Local)
    if (LocalInitField(n))
      SetLocalInitField(n, as_Expr(resolve_sym(r, LocalInitField(n))));
    return np;


  NCASE(Template)    panic("TODO %s", nodename(n));
  NCASE(TypeCast) panic("TODO %s", nodename(n));
  NCASE(Ref)      panic("TODO %s", nodename(n));
  NCASE(NamedArg) panic("TODO %s", nodename(n));
  NCASE(Selector) panic("TODO %s", nodename(n));
  NCASE(Index)    panic("TODO %s", nodename(n));
  NCASE(Slice)    panic("TODO %s", nodename(n));
  NCASE(If)       panic("TODO %s", nodename(n));
  NCASE(TypeExpr) panic("TODO %s", nodename(n));

  NCASE(TypeType)   panic("TODO %s", nodename(n));
  NCASE(AliasType)  panic("TODO %s", nodename(n));
  NCASE(RefType)    panic("TODO %s", nodename(n));
  NCASE(BasicType)  panic("TODO %s", nodename(n));
  NCASE(ArrayType)  panic("TODO %s", nodename(n));
  NCASE(TupleType)  panic("TODO %s", nodename(n));
  NCASE(StructType) panic("TODO %s", nodename(n));
  NCASE(FunType)    panic("TODO %s", nodename(n));
  NCASE(TemplateType)  panic("TODO %s", nodename(n));
  NCASE(TemplateParamType) panic("TODO %s", nodename(n));

  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  UNREACHABLE;
}

// end resolve_sym
//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve_type


#define resolve_type(r,n) _resolve_type((r),as_Node(n))
static Node* _resolve_type(R* r, Node* n);

// set_typecontext returns r->typecontext as it was prior to setting t
static Type* nullable set_typecontext(R* r, Type* nullable t) {
  if (t) {
    assert(is_Type(t) || is_TemplateParamNode(t));
    assertne(t, kType_ideal);
  }
  Type* prev = r->typecontext;
  r->typecontext = unbox_id_type(t);
  return prev;
}

// *_flags returns r->flags as they were prior to altering them
static RFlag add_flags(R* r, RFlag fl) { RFlag f = r->flags; r->flags |= fl;  return f; }
static RFlag clear_flags(R* r, RFlag fl)  { RFlag f = r->flags; r->flags &= ~fl; return f; }
//static RFlag set_flags(R* r, RFlag fl) { RFlag f = r->flags; r->flags = fl; return f; }

// #define flags_scope(FLAGS) \
//   for (RFlag prevfl__ = r->flags, tmp1__ = RF_INVALID; \
//        tmp1__ && (r->flags = (FLAGS), 1); \
//        tmp1__ = 0, r->flags = prevfl__)

// #define typecontext_scope(TYPECONTEXT) \
//   for (  Type* new__ = (TYPECONTEXT), \
//          *prev__ = (assert(new__ != kType_ideal), r->typecontext), \
//          *tmp1__ = kType_nil; \
//        tmp1__ && (r->typecontext = new__, 1); \
//        tmp1__ = NULL, r->typecontext = prev__ )

// mark_local_mutable marks any Var or Field n as being mutable
#define mark_local_mutable(r,n) _mark_local_mutable((r),as_Node(n))
static Node* _mark_local_mutable(R* r, Node* n) {
  while (1) {
    switch (n->kind) {
      case NIndex:
        n = as_Node(as_IndexNode(n)->operand);
        break;
      case NSelector:
        n = as_Node(as_SelectorNode(n)->operand);
        break;
      case NId:
        n = as_Node(assertnotnull( ((IdNode*)n)->target ));
        break;
      case NVar:
      case NField:
        NodeClearConst(n);
        return n;
      default:
        return n;
    }
  }
}

static bool is_type_complete(Type* np) {
  switch ((enum NodeKind)np->kind) { case NBad: { return false;
    NCASE(ArrayType)
      return (n->sizeexpr == NULL || n->size > 0) && is_type_complete(n->elem);
    NCASE(RefType)
      return is_type_complete(n->elem);
    NCASE(StructType)
      return (n->flags & (NF_CustomInit | NF_PartialType)) == 0;
    NCASE(IdType)
      return (n->flags & NF_Unresolved) == 0;
    NDEFAULTCASE
      return (n->flags & NF_PartialType) == 0;
  }}
}


// find_param_by_name returns the first index of local "name", or -1 if not found
static isize find_param_by_name(R* r, const ParamArray* params, Sym name) {
  assert((usize)params->len <= (usize)ISIZE_MAX);
  for (isize i = 0, len = (isize)params->len; i < len; i++) {
    if (params->v[i]->name == name)
      return i;
  }
  return -1;
}


#define TODO_RESTYPE_IMPL \
  errf(r, n, "\e[33;1mTODO %s\e[0m  %s:%d", __FUNCTION__, __FILE__, __LINE__)


//——————————————————————————————————————
// node-specific type resolver functions

static Node* resolve_cunit(R* r, CUnitNode* n) {
  // File and Pkg are special in that types do not propagate
  // Note: Instead of setting n->type=Type_nil, leave as NULL and return early
  // to avoid check for null types.
  for (u32 i = 0; i < n->a.len; i++)
    n->a.v[i] = resolve(r, n->a.v[i]);
  return as_Node(n);
}


static FunTypeNode* resolve_fun_proto(R* r, FunNode* n) {
  auto t = mknode(r, FunType, n->pos);
  n->type = as_Type(t);
  t->flags |= (n->flags & NF_Unsafe);

  RFlag rflags = r->flags; // save
  SET_FLAG(r->flags, RF_Unsafe, NodeIsUnsafe(n)); // are we in safe or unsafe context?

  for (u32 i = 0; i < n->params.len; i++) {
    UNUSED Node* cn = resolve(r, n->params.v[i]);
    assert(cn == (Node*)n->params.v[i]);
  }
  t->params = &n->params;

  if (n->result) {
    n->result = as_Type(resolve(r, n->result));
    t->result = unbox_id_type(n->result);
  }

  r->flags = rflags; // restore
  return t;
}


static Node* resolve_fun(R* r, FunNode* n) {
  FunTypeNode* t = resolve_fun_proto(r, n);

  if (!n->body)
    return as_Node(n);

  // resolve function body
  Type* typecontext = set_typecontext(r, t->result);
  n->body = as_Expr(resolve(r, n->body));
  r->typecontext = typecontext;

  if UNLIKELY(
    t->result && t->result != kType_nil &&
    !b_typeeq(r->build, t->result, n->body->type) &&
    r->build->errcount == 0)
  {
    // TODO: focus the message on the first return expression of n->body
    // which is of a different type than t->result
    errf(r, n->body,
      "incompatible result type %s for function returning %s",
      FMTNODE(n->body->type,0), FMTNODE(t->result,1));
  }

  return as_Node(n);
}


// is_named_params returns true if params are named; ie (x T, y T) but not (T, T)
static bool is_named_params(ParamArray* params) {
  return params->len > 0 && params->v[0]->name != kSym__;
}


static int named_arg_sortfn(const NamedArgNode** x, const NamedArgNode** y, R* r) {
  // parameter indices are stored in irval field
  intptr xi = (intptr)(*x)->irval;
  intptr yi = (intptr)(*y)->irval;
  return xi - yi;
}


static bool resolve_positional_call_args(R* r, CallNode* n, ParamArray* params) {
  RFlag rflags = add_flags(r, RF_ResolveIdeal);
  Type* typecontext = r->typecontext;

  for (u32 i = 0; i < n->args.len; i++) {
    r->typecontext = unbox_id_type(params->v[i]->type);
    n->args.v[i] = as_Expr(resolve(r, n->args.v[i]));
  }

  r->typecontext = typecontext;
  r->flags = rflags;
  return true;
}


static bool resolve_named_call_args(R* r, CallNode* n, ParamArray* params) {
  // if the parameters aren't named, we can't call them by name
  // e.g. "fun foo(int) ; foo(x=1)"
  if UNLIKELY(!is_named_params(params)) {
    b_errf(r->build, CallNodeArgsPosSpan(n),
      "%s does not accept named parameters", FMTNODE(n->receiver,0));
    return false;
  }

  ExprArray* args = &n->args;
  asserteq(params->len, args->len);

  // save-then-update resolver state
  Type* typecontext = r->typecontext;
  RFlag rflags = add_flags(r, RF_ResolveIdeal);

  // Start by resolving positional arguments and just looking up position for named args.
  // The parser guarantees these come before named ones.
  u32 i = 0;
  for (; i < args->len && args->v[i]->kind != NNamedArg; i++) {
    r->typecontext = unbox_id_type(params->v[i]->type);
    args->v[i] = as_Expr(resolve(r, args->v[i]));
  }

  // find canonical parameter positions for remaining named arguments
  assert(i < args->len); // or bug above or wherever NF_Named flag was set
  u32 named_start_idx = i;
  for (; i < args->len; i++) {
    NamedArgNode* namedarg = as_NamedArgNode(args->v[i]);
    isize param_idx = find_param_by_name(r, params, namedarg->name);
    if UNLIKELY(param_idx < 0) {
      b_errf(r->build, CallNodeArgsPosSpan(n),
        "no parameter named \"%s\" in %s", namedarg->name, FMTNODE(n->receiver,0));
      goto end;
    }
    // ditch the named value
    Expr* arg = namedarg->value;
    // temporarily store parameter index for sort function to access
    arg->irval = (void*)(uintptr)param_idx;
    // resolve argument
    r->typecontext = unbox_id_type(params->v[param_idx]->type);
    args->v[i] = as_Expr(resolve(r, arg));
  }

  // sort arguments
  xqsort(&args->v[named_start_idx], args->len - named_start_idx, sizeof(void*),
    (xqsort_cmp)&named_arg_sortfn, r);

  // clear temporaries
  for (i = named_start_idx; i < args->len; i++)
    args->v[i]->irval = NULL;

end:
  // restore resolver state
  r->typecontext = typecontext;
  r->flags = rflags;
  return true;
}


static bool resolve_call_args(R* r, CallNode* n, ParamArray* params) {
  // resolve arguments in the context of the function's parameters
  bool ok;
  if (n->flags & NF_Named) {
    ok = resolve_named_call_args(r, n, params);
  } else {
    ok = resolve_positional_call_args(r, n, params);
  }
  if UNLIKELY(!ok)
    return false;

  for (u32 i = 0, len = n->args.len; i < len; i++) {
    Expr* arg = n->args.v[i];
    Type* param_typ = assertnotnull(params->v[i]->type);
    if (is_TemplateParamTypeNode(param_typ)) {
      assertf(r->flags & RF_Template, "template parameter outside template");
      continue;
    }
    if UNLIKELY(!b_typelteq(r->build, param_typ, arg->type)) {
      char tmpbuf[128];
      errf(r, arg, "incompatible argument type %s, expecting %s in call to %s",
        FMTNODE(arg->type,0), FMTNODE(param_typ,1),
        fmtnode(n->receiver, tmpbuf, sizeof(tmpbuf)));
      return false;
    }
  }

  return true;
}


static Node* resolve_call_type(R* r, CallNode* n) {
  Type* recvt = (
    is_Expr(n->receiver) ? ((Expr*)n->receiver)->type :
    is_Type(n->receiver) ? (Type*)n->receiver :
    kType_nil
  );
  dlog("TODO type of TypeType");
  n->type = unbox_id_type(recvt);
  return as_Node(n);
}


static Node* resolve_call_fun(R* r, CallNode* n, FunTypeNode* ft) {
  if (NodeIsUnsafe(ft) && (r->flags & RF_Unsafe) == 0) {
    b_errf(r->build, NodePosSpan(n),
      "call to unsafe function requires unsafe function or block");
  }

  n->type = ft->result; // note: already unbox_id_type'ed
  if (!n->type)
    n->type = kType_nil;

  // check argument cardinality
  if (n->args.len != ft->params->len) {
    b_errf(r->build, CallNodeArgsPosSpan(n),
      "wrong number of arguments: %u; expecting %u", n->args.len, ft->params->len);
    return as_Node(n);
  }

  if (n->args.len > 0)
    resolve_call_args(r, n, ft->params);

  return as_Node(n);
}


static Node* instantiate_template(R* r, TemplateNode* tpl, NodeArray* tplvals) {
  // TODO: need to traverse AST and copy paths with changes,
  // kind of like instertion in a HAMT.
  dlog("TODO instantiate_template %s", FMTNODE(tpl,0));
  return (Node*)kExpr_nil;
}


static Node* resolve_call(R* r, CallNode* n);


static Node* resolve_call_template_fun(R* r, CallNode* n, TemplateNode* tpl) {
  // indicate that we are "inside" a template
  RFlag rflags = add_flags(r, RF_Template);

  // Resolve the function prototype
  FunNode* fn = as_FunNode(tpl->body);
  FunTypeNode* ft = resolve_fun_proto(r, fn);

  // Resolve the function call with potential template parameters as function parameters.
  // This gives us definitive types of arguments,
  // which we use to infer template parameter types.
  resolve_call_fun(r, n, ft);
  if UNLIKELY(r->build->errcount)
    goto bail;
  asserteq(n->args.len, ft->params->len); // resolve_call_fun should check this

  // tplvals holds effective template parameter values
  Node* tplvals_st[16];
  NodeArray tplvals = array_make(NodeArray, tplvals_st, sizeof(tplvals_st));
  if UNLIKELY(!array_reserve(&tplvals, tpl->params.len)) {
    b_err_nomem(r->build, NodePosSpan(n));
    goto bail;
  }
  tplvals.len = tpl->params.len;
  memset(tplvals.v, 0, sizeof(void*) * tplvals.len); // set all to NULL
  u32 min_index = U32_MAX, max_index = 0;
  assert(tplvals.len < U32_MAX);

  // TODO: populate tplvals with explicitly provided template values,
  // e.g. T=int in "foo<int>(3)"

  // populate tplvals based on arguments to call
  for (u32 i = 0; i < n->args.len; i++) {
    ParamNode* param = fn->params.v[i];
    if (!is_TemplateParamTypeNode(param->type))
      continue;

    Type* t = assertnotnull(n->args.v[i]->type);
    //dlog("args[%u] %s : %s", i, FMTNODE(n->args.v[i],0), FMTNODE(t,1));

    // e.g. param is "x T" in "fun foo(x T, y int)"
    TemplateParamTypeNode* tparamt = (TemplateParamTypeNode*)param->type;
    TemplateParamNode* tparam = tparamt->param;

    // When a template parameter is used more than once, select the first use.
    // e.g. T=int in "fun foo<T>(x T, y T) ; foo(1, 2.1)" (rather than T=f64)
    if (tplvals.v[tparam->index])
      continue;

    // let the value of template parameter P be t
    tplvals.v[tparam->index] = as_Node(t);
    min_index = MIN(min_index, tparam->index);
    max_index = MIN(max_index, tparam->index);
  }

  // it is an error if some template parameters were not explicitly passed or could
  // not be inferred from arguments
  if UNLIKELY(min_index != 0 || max_index != tplvals.len - 1) {
    u32 nerrors = 0;
    for (u32 i = 0; i < tplvals.len; i++) {
      TemplateParamNode* tparam = tpl->params.v[i];
      if (tplvals.v[i] || tparam->nrefs == 0)
        continue;
      nerrors++;
      b_errf(r->build, NodePosSpan(tparam),
        "unable to infer value of template parameter %s", tparam->name);
    }
    if (nerrors > 0) {
      if (n->pos != NoPos)
        b_notef(r->build, NodePosSpan(n), "template instantiated here");
      goto bail;
    }
  }

  // dlog2
  #ifdef CO_PARSE_RESOLVE_DEBUG
  dlog2("Effective template parameter values:");
  for (u32 i = 0; i < tplvals.len; i++)
    dlog2("  %s = %s", tpl->params.v[i]->name, FMTNODE(tplvals.v[i],0));
  #endif

  // instantiate template to create function implementation
  Node* concrete_fn = instantiate_template(r, tpl, &tplvals);
  concrete_fn = resolve(r, concrete_fn);
  array_free(&tplvals);
  r->flags = rflags; // restore

  // update call to point to concrete function and then resolve the actual call
  n->receiver = concrete_fn;
  return resolve_call(r, n);

bail:
  r->flags = rflags; // restore
  return as_Node(n);
}


static Node* resolve_call_template(R* r, CallNode* n) {
  TemplateNode* tpl = as_TemplateNode(NodeEval(r->build, as_Expr(n->receiver), NULL, 0));
  TemplateTypeNode* tt = as_TemplateTypeNode(tpl->type);

  if (tt->prodkind == TF_KindFunc)
    return resolve_call_template_fun(r, n, tpl);

  if (tt->prodkind == TF_KindType)
    dlog("TODO call templated type");

  b_errf(r->build, NodePosSpan(n), "%s is not callable", FMTNODE(tpl,0));
  return as_Node(n);
}


static Node* resolve_call(R* r, CallNode* n) {
  n->receiver = resolve(r, n->receiver);

  Type* recvt = (
    is_Expr(n->receiver) ? ((Expr*)n->receiver)->type :
    is_Type(n->receiver) ? kType_type :
    kType_nil
  );

  if (recvt == kType_type)         return resolve_call_type(r, n);
  if (is_TemplateTypeNode(recvt))  return resolve_call_template(r, n);
  if LIKELY(is_FunTypeNode(recvt)) return resolve_call_fun(r, n, (FunTypeNode*)recvt);

  b_errf(r->build, NodePosSpan(n), "%s is not callable", FMTNODE(n->receiver,0));
  n->type = kType_nil;
  return (Node*)n;
}


static Node* resolve_tuple(R* r, TupleNode* n) {
  auto t = mknode_array(r, TupleType, n->pos, a, n->a.len);
  if UNLIKELY(!t) {
    n->type = kType_nil;
    return as_Node(n);
  }
  n->type = as_Type(t);

  Type* typecontext = r->typecontext; // save

  // do we have a tuple context type?
  const TypeArray* ctx_types = NULL;
  if (typecontext) {
    if (typecontext->kind == NTupleType) {
      ctx_types = &((TupleTypeNode*)typecontext)->a;
      assert(ctx_types->len > 0); // tuples should never be empty
    } else {
      // TODO: improve this error message
      errf(r, typecontext, "unexpected context type %s", FMTNODE(typecontext,1));
      return as_Node(n);
    }
    if UNLIKELY(ctx_types->len != n->a.len) {
      errf(r, n, "%u expressions where %u expressions are expected %s",
        n->a.len, ctx_types->len, FMTNODE(typecontext,1));
      return as_Node(n);
    }
  }

  for (u32 i = 0; i < n->a.len; i++) {
    if (ctx_types)
      r->typecontext = unbox_id_type(ctx_types->v[i]);
    Expr* cn = as_Expr(resolve(r, n->a.v[i]));
    cn->type = unbox_id_type(cn->type);
    array_push(&t->a, cn->type);
    n->a.v[i] = cn;
  }

  r->typecontext = typecontext; // restore
  return as_Node(n);
}


static Node* resolve_array(R* r, ArrayNode* n) {
  panic("TODO (impl probably almost identical to resolve_tuple)");
  return as_Node(n);
}


static Node* resolve_block(R* r, BlockNode* n) {
  // The type of a block is the type of the last expression

  if (n->a.len == 0) {
    // empty block has no type (void)
    n->type = kType_nil;
    return as_Node(n);
  }

  RFlag rflags = r->flags; // save
  if (NodeIsUnsafe(n))
    r->flags |= RF_Unsafe;

  // resolve all but the last expression without requiring ideal-type resolution
  r->flags &= ~RF_ResolveIdeal;
  u32 lasti = n->a.len - 1;
  for (u32 i = 0; i < lasti; i++)
    n->a.v[i] = as_Expr(resolve(r, n->a.v[i]));

  // Last node, in which case we set the flag to resolve literals so that implicit return
  // values gets properly typed.
  r->flags |= RF_ResolveIdeal;
  n->a.v[lasti] = as_Expr(resolve(r, n->a.v[lasti]));

  n->type = unbox_id_type(n->a.v[lasti]->type);

  r->flags = rflags; // restore
  return as_Node(n);
}


static Node* resolve_field(R* r, FieldNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}


static Node* resolve_intlit(R* r, IntLitNode* n) {
  Type* t = kType_int;
  if (r->typecontext && r->typecontext->kind != NTemplateParamType)
    t = r->typecontext;
  Expr* n2 = ctypecast_implicit(r->build, n, t, NULL, n);
  return as_Node(n2);
}


static Node* resolve_floatlit(R* r, FloatLitNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_f64;
  return as_Node(n);
}

static Node* resolve_strlit(R* r, StrLitNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_id(R* r, IdNode* n) {
  assertnotnull(n->target);
  n->target = as_Expr(resolve(r, n->target));
  n->type = unbox_id_type(((Expr*)n->target)->type);
  return as_Node(n);
}


static Node* resolve_binop(R* r, BinOpNode* n) {
  Expr* x = n->left;
  Expr* y = n->right;
  bool prefer_y = false;

  Type* typecontext = set_typecontext(r, NULL); {
    if (x->type && x->type != kType_ideal) {
      r->typecontext = unbox_id_type(x->type);
    } else if (y->type && y->type != kType_ideal) {
      r->typecontext = unbox_id_type(y->type);
      prefer_y = true;
    }
    x = as_Expr(resolve(r, x));
    y = as_Expr(resolve(r, y));
  }
  r->typecontext = typecontext;

  // if the types differ, attempt an implicit cast
  if UNLIKELY(!b_typeeq(r->build, x->type, y->type)) {
    // note: ctypecast_implicit does a full b_typeeq check before attempting cast
    Expr** xp = prefer_y ? &y : &x;
    Expr** yp = prefer_y ? &x : &y;
    *yp = ctypecast_implicit(r->build, *xp, (*yp)->type, NULL, n);
  }

  n->left = x;
  n->right = y;
  n->type = unbox_id_type(x->type);

  return as_Node(n);
}


static Node* resolve_prefixop(R* r, PrefixOpNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_postfixop(R* r, PostfixOpNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_return(R* r, ReturnNode* n) {
  n->expr = as_Expr(resolve(r, n->expr));
  n->type = unbox_id_type(n->expr->type);
  return as_Node(n);
}


static Node* resolve_assign(R* r, AssignNode* n) {
  // 1. resolve destination (lvalue) and
  // 2. resolve value (rvalue) witin the type context of destination

  RFlag rflags = clear_flags(r, RF_ResolveIdeal);
  n->dst = as_Expr(resolve(r, n->dst));
  Type* typecontext = r->typecontext; // save
  r->typecontext = (n->dst->type != kType_ideal) ? unbox_id_type(n->dst->type) : NULL;
  n->val = as_Expr(resolve(r, n->val));
  r->typecontext = typecontext; // restore
  r->flags = rflags; // restore

  // clear n's NF_Const flag since storing to var upgrades it to mutable.
  Node* leaf = mark_local_mutable(r, n->dst);
  if UNLIKELY(leaf->kind == NConst) {
    Sym name = as_ConstNode(leaf)->name;
    errf(r, n->dst, "cannot store to constant %s", name);
    if (leaf->pos != NoPos)
      b_notef(r->build, NodePosSpan(leaf), "%s defined here", name);
  }

  // the type of the assignment expression is the type of the destination (var/field)
  n->type = unbox_id_type(n->dst->type);

  // check & convert rvalue type
  if UNLIKELY(n->type->kind == NArrayType) {
    // storing to a local or field of array type is not allowed
    errf(r, n, "array type %s is not assignable", FMTNODE(n->type,0));
  } else if (!b_typelteq(r->build, n->type, n->val->type)) {
    // convert rvalue (if it's a different type than dst)
    n->val = ctypecast_implicit(r->build, n->val, n->type, NULL, n);
  }

  return as_Node(n);
}


static Node* resolve_template(R* r, TemplateNode* n) {
  // TODO_RESTYPE_IMPL;
  dlog("TODO resolve_template");
  n->type = kType_nil;
  return as_Node(n);
}


static Node* resolve_typecast(R* r, TypeCastNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}


static Node* resolve_const(R* r, ConstNode* n) {
  n->value = as_Expr(resolve(r, n->value));
  n->type = unbox_id_type(n->value->type);
  return as_Node(n);
}


static Node* resolve_var(R* r, VarNode* n) {
  // parser should make sure that var without explicit type has initializer
  assertnotnull(n->init);
  n->init = as_Expr(resolve(r, n->init));
  n->type = unbox_id_type(n->init->type);
  return as_Node(n);
}


static Node* resolve_param(R* r, ParamNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_templateparam(R* r, TemplateParamNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_ref(R* r, RefNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}


static Node* resolve_namedarg(R* r, NamedArgNode* n) {
  n->value = as_Expr(resolve(r, n->value));
  n->type = unbox_id_type(n->value->type);
  return as_Node(n);
}


static Node* resolve_selector(R* r, SelectorNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_index(R* r, IndexNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_slice(R* r, SliceNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_if(R* r, IfNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}

static Node* resolve_typeexpr(R* r, TypeExprNode* n) {
  TODO_RESTYPE_IMPL; n->type = kType_nil;
  return as_Node(n);
}



static Node* resolve_typetype(R* r, TypeTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_idtype(R* r, IdTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_templateparamtype(R* r, TemplateParamTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_templatetype(R* r, TemplateTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_aliastype(R* r, AliasTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_reftype(R* r, RefTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_basictype(R* r, BasicTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_arraytype(R* r, ArrayTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_tupletype(R* r, TupleTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_structtype(R* r, StructTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}

static Node* resolve_funtype(R* r, FunTypeNode* n) {
  TODO_RESTYPE_IMPL;
  return as_Node(n);
}



static Node* _resolve_type(R* r, Node* np) {
  if (is_Type(np)) {
    if (is_type_complete((Type*)np))
      return np;
  } else if (np->flags & NF_PartialType) {
    np->flags &= ~NF_PartialType;
    // continue
  } else if (is_Expr(np) && ((Expr*)np)->type) {
    // Has type already. Constant literals might have ideal type.
    Expr* n = (Expr*)np;
    if (n->type == kType_ideal && ((r->flags & RF_ResolveIdeal) || NodeIsRValue(n))) {
      dlog2("resolving ideally-typed node %s", nodename(n));
      // continue
    } else {
      // make sure its type is resolved
      if (!is_type_complete(n->type))
        n->type = unbox_id_type(as_Type(resolve(r, n->type)));
      return np;
    }
  }

  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Field)      return resolve_field(r, n);
  GNCASE(CUnit)     return resolve_cunit(r, n);
  NCASE(Comment)    // not possible

  NCASE(Nil)           // not possible
  NCASE(BoolLit)       // not possible
  NCASE(IntLit)        return resolve_intlit(r, n);
  NCASE(FloatLit)      return resolve_floatlit(r, n);
  NCASE(StrLit)        return resolve_strlit(r, n);
  NCASE(Id)            return resolve_id(r, n);
  NCASE(BinOp)         return resolve_binop(r, n);
  NCASE(PrefixOp)      return resolve_prefixop(r, n);
  NCASE(PostfixOp)     return resolve_postfixop(r, n);
  NCASE(Return)        return resolve_return(r, n);
  NCASE(Assign)        return resolve_assign(r, n);
  NCASE(Tuple)         return resolve_tuple(r, n);
  NCASE(Array)         return resolve_array(r, n);
  NCASE(Block)         return resolve_block(r, n);
  NCASE(Fun)           return resolve_fun(r, n);
  NCASE(Template)      return resolve_template(r, n);
  NCASE(Call)          return resolve_call(r, n);
  NCASE(TypeCast)      return resolve_typecast(r, n);
  NCASE(Const)         return resolve_const(r, n);
  NCASE(Var)           return resolve_var(r, n);
  NCASE(Param)         return resolve_param(r, n);
  NCASE(TemplateParam) return resolve_templateparam(r, n);
  NCASE(Ref)           return resolve_ref(r, n);
  NCASE(NamedArg)      return resolve_namedarg(r, n);
  NCASE(Selector)      return resolve_selector(r, n);
  NCASE(Index)         return resolve_index(r, n);
  NCASE(Slice)         return resolve_slice(r, n);
  NCASE(If)            return resolve_if(r, n);
  NCASE(TypeExpr)      return resolve_typeexpr(r, n);

  NCASE(TypeType)      return resolve_typetype(r, n);
  NCASE(IdType)        return resolve_idtype(r, n);
  NCASE(AliasType)     return resolve_aliastype(r, n);
  NCASE(RefType)       return resolve_reftype(r, n);
  NCASE(BasicType)     return resolve_basictype(r, n);
  NCASE(ArrayType)     return resolve_arraytype(r, n);
  NCASE(TupleType)     return resolve_tupletype(r, n);
  NCASE(StructType)    return resolve_structtype(r, n);
  NCASE(FunType)       return resolve_funtype(r, n);
  NCASE(TemplateType)  return resolve_templatetype(r, n);
  NCASE(TemplateParamType) return resolve_templateparamtype(r, n);

  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  UNREACHABLE;
}

// end resolve_type
//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve

static Node* _resolve(R* r, Node* n) {
  // resolve identifiers
  n = resolve_sym(r, n);

  if UNLIKELY(r->build->errcount != 0) {
    // if we encountered an error, for example undefined identifier,
    // don't bother with type resolution as it is likely going to yield
    // confusing error messages as a cascading issue of prior errors.
    if (is_Expr(n) && !((Expr*)n)->type)
      ((Expr*)n)->type = kType_nil;
    return n;
  }

  // resolve types
  return resolve_type(r, n);
}


Node* resolve_ast(BuildCtx* build, Node* n) {
  R r = {
    .build = build,
    .lookupscope = build->pkg.scope,
  };
  array_init(&r.funstack, r.funstack_storage, sizeof(r.funstack_storage));

  // always one slot so we can access the top of the stack without checks
  r.funstack_storage[0] = NULL;
  r.funstack.len = 1;

  #ifdef DEBUG
  NodeKind initial_kind = n->kind;
  #endif

  n = resolve(&r, n);

  array_free(&r.funstack);
  asserteq(initial_kind, n->kind); // since we typecast the result (see header file)
  return n;
}
