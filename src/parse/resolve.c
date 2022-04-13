// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

// CO_PARSE_RESOLVE_DEBUG: define to enable trace logging
//#define CO_PARSE_RESOLVE_DEBUG

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

  // if the target is a primitive constant, use that instead of the id node
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
  RF_ExplicitTypeCast = 1 << 0,
  RF_ResolveIdeal     = 1 << 1,  // set when resolving ideal types
  RF_Eager            = 1 << 2,  // set when resolving eagerly

  RF_INVALID          = 0xffffffff, // specific flags value that is invalid
  _RF_UNUSED_         = 1 << 31,    // make sure we can never compose RF_INVALID
};

// resolver state
typedef struct R {
  BuildCtx* build;
  rflag     flags;
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


// mknode allocates a new ast node
// T* nullable mknode(R*, KIND)
#define mknode(r, KIND, pos) b_mknode((r)->build, KIND, (pos))


#define FMTNODE(n,bufno) \
  fmtnode(n, r->build->tmpbuf[bufno], sizeof(r->build->tmpbuf[bufno]))


#define check_memalloc(r,n,ok) _check_memalloc((r),as_Node(n),(ok))
inline static bool _check_memalloc(R* r, Node* n, bool ok) {
  if UNLIKELY(!ok)
    b_errf(r->build, NodePosSpan(n), "failed to allocate memory");
  return ok;
}


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
    if (is_Expr(n))
      fmtnode(((Expr*)n2)->type, tmpbuf, sizeof(tmpbuf));

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

static void resolve_sym_array(R* r, const NodeArray* a) {
  for (u32 i = 0; i < a->len; i++)
    a->v[i] = resolve_sym(r, a->v[i]);
}

static Node* _resolve_sym1(R* r, Node* np) {
  NodeClearUnresolved(np); // do this up front to allow tail calls

  // resolve type first
  if (is_Expr(np) && ((Expr*)np)->type && NodeIsUnresolved(((Expr*)np)->type))
    ((Expr*)np)->type = as_Type(resolve_sym(r, ((Expr*)np)->type));

  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Comment) break; // unexpected

  GNCASE(CUnit)
    Scope* lookupscope = r->lookupscope; // save
    if (n->scope)
      r->lookupscope = n->scope;
    resolve_sym_array(r, &n->a);
    r->lookupscope = lookupscope; // restore
    return np;

  NCASE(Field)      panic("TODO %s", nodename(n));

  NCASE(Nil)        panic("TODO %s", nodename(n));
  NCASE(BoolLit)    panic("TODO %s", nodename(n));
  NCASE(IntLit)     panic("TODO %s", nodename(n));
  NCASE(FloatLit)   panic("TODO %s", nodename(n));
  NCASE(StrLit)     panic("TODO %s", nodename(n));

  NCASE(Id)
    Node* target = ScopeLookup(r->lookupscope, n->name);
    if UNLIKELY(target == NULL) {
      dlog2("LOOKUP %s FAILED", n->name);
      b_errf(r->build, NodePosSpan(n), "undefined identifier %s", n->name);
      n->target = kNode_bad;
      return np;
    }
    if (NodeIsUnused(target)) // must check to avoid editing universe
      NodeClearUnused(target);
    return resolve_id(n, target);

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
    resolve_sym_array(r, as_NodeArray(&n->a));
    return np;

  NCASE(Block)      panic("TODO %s", nodename(n));

  NCASE(Fun)
    if (n->params)
      n->params = as_TupleNode(resolve_sym(r, n->params));
    if (n->result)
      n->result = as_Type(resolve_sym(r, n->result));
    // Note: Don't update lookupscope as a function's parameters should always be resolved
    if (n->body)
      n->body = as_Expr(resolve_sym(r, n->body));
    return np;

  NCASE(Macro)      panic("TODO %s", nodename(n));
  NCASE(Call)       panic("TODO %s", nodename(n));
  NCASE(TypeCast)   panic("TODO %s", nodename(n));

  GNCASE(Local)
    if (LocalInitField(n))
      SetLocalInitField(n, as_Expr(resolve_sym(r, LocalInitField(n))));
    return np;

  NCASE(Ref)        panic("TODO %s", nodename(n));
  NCASE(NamedArg)   panic("TODO %s", nodename(n));
  NCASE(Selector)   panic("TODO %s", nodename(n));
  NCASE(Index)      panic("TODO %s", nodename(n));
  NCASE(Slice)      panic("TODO %s", nodename(n));
  NCASE(If)         panic("TODO %s", nodename(n));

  NCASE(TypeType)   panic("TODO %s", nodename(n));
  NCASE(NamedType)  panic("TODO %s", nodename(n));
  NCASE(AliasType)  panic("TODO %s", nodename(n));
  NCASE(RefType)    panic("TODO %s", nodename(n));
  NCASE(BasicType)  panic("TODO %s", nodename(n));
  NCASE(ArrayType)  panic("TODO %s", nodename(n));
  NCASE(TupleType)  panic("TODO %s", nodename(n));
  NCASE(StructType) panic("TODO %s", nodename(n));
  NCASE(FunType)    panic("TODO %s", nodename(n));

  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  UNREACHABLE;
}

// end resolve_sym
//———————————————————————————————————————————————————————————————————————————————————————
// begin resolve_type


#define resolve_type(r,n) _resolve_type((r),as_Node(n))
static Node* _resolve_type(R* r, Node* n);

// // returns old type
// static Type* nullable typecontext_set(R* r, Type* nullable newtype) {
//   if (newtype) {
//     assert(is_Type(newtype) || is_MacroParamNode(newtype));
//     assertne(newtype, kType_ideal);
//   }
//   dlog2("typecontext_set %s", FMTNODE(newtype,0));
//   auto oldtype = r->typecontext;
//   r->typecontext = newtype;
//   return oldtype;
// }

//static rflag addflag(R* r, rflag flag) { rflag f = r->flags; r->flags |= flag;  return f; }
//static rflag rmflag(R* r, rflag flag)  { rflag f = r->flags; r->flags &= ~flag; return f; }
//static rflag setflags(R* r, rflag flags) { rflag f = r->flags; r->flags = flags; return f;}
#define flags_scope(FLAGS) \
  for (rflag prevfl__ = r->flags, tmp1__ = RF_INVALID; \
       tmp1__ && (r->flags = (FLAGS), 1); \
       tmp1__ = 0, r->flags = prevfl__)

#define typecontext_scope(TYPECONTEXT) \
  for (  Type* new__ = (TYPECONTEXT), \
         *prev__ = (assert(new__ != kType_ideal), r->typecontext), \
         *tmp1__ = kType_nil; \
       tmp1__ && (r->typecontext = new__, 1); \
       tmp1__ = NULL, r->typecontext = prev__ )


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
        n = assertnotnull( ((IdNode*)n)->target );
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
    NDEFAULTCASE
      return (n->flags & NF_PartialType) == 0;
  }}
}

//——————————————————————————————————————
// node-specific type resolver functions

static Node* restype_cunit(R* r, CUnitNode* n) {
  // File and Pkg are special in that types do not propagate
  // Note: Instead of setting n->type=Type_nil, leave as NULL and return early
  // to avoid check for null types.
  for (u32 i = 0; i < n->a.len; i++)
    n->a.v[i] = resolve(r, n->a.v[i]);
  return as_Node(n);
}


static Node* restype_fun(R* r, FunNode* n) {
  auto t = mknode(r, FunType, n->pos);
  if UNLIKELY(t == NULL)
    return as_Node(n);
  n->type = as_Type(t);

  if (n->params) {
    n->params = as_TupleNode(resolve(r, n->params));
    t->params = n->params;
  }

  if (n->result) {
    n->result = as_Type(resolve(r, n->result));
    t->result = n->result;
  }

  if (n->body) {
    typecontext_scope(n->result) {
      n->body = as_Expr(resolve(r, n->body));
    }
    if UNLIKELY(
      n->result && n->result != kType_nil &&
      !b_typeeq(r->build, n->result, n->body->type) &&
      r->build->errcount == 0)
    {
      // TODO: focus the message on the first return expression of n->body
      // which is of a different type than n->result
      b_errf(r->build, NodePosSpan(n->body),
        "incompatible result type %s for function returning %s",
        FMTNODE(n->body->type,0), FMTNODE(n->result,1));
    }
  }

  return as_Node(n);
}


static Node* restype_call_type(R* r, CallNode* n) {
  Type* recvt = (
    is_Expr(n->receiver) ? ((Expr*)n->receiver)->type :
    is_Type(n->receiver) ? (Type*)n->receiver :
    kType_nil
  );
  dlog("TODO type of TypeType");
  n->type = recvt;
  return as_Node(n);
}


static Node* restype_call_fun(R* r, CallNode* n) {
  FunTypeNode* ft = (FunTypeNode*)((Expr*)n->receiver)->type;
  n->type = ft->result;
  if (!n->type)
    n->type = kType_nil;

  // check argument arity
  u32 nargs = n->args ? n->args->a.len : 0;
  u32 nparams = ft->params ? ft->params->a.len : 0;
  if (nargs != nparams) {
    b_errf(r->build, NodePosSpan(n->args ? (Node*)n->args : (Node*)n),
      "wrong number of arguments: %u; expecting %u", nargs, nparams);
    return as_Node(n);
  }

  if (!n->args)
    return as_Node(n);

  // resolve arguments in the context of the function's parameters
  flags_scope(r->flags | RF_ResolveIdeal) {
    typecontext_scope(ft->params->type) {
      n->args = as_TupleNode(resolve(r, n->args));
    }
  }

  // check argument types against paramter types
  if UNLIKELY(!b_typeeq(r->build, ft->params->type, n->args->type)) {
    char tmpbuf[128];
    b_errf(r->build, NodePosSpan(n->args),
      "incompatible argument types %s, expecting %s in call to %s",
      FMTNODE(n->args->type,0), FMTNODE(ft->params->type,1),
      fmtnode(n->receiver, tmpbuf, sizeof(tmpbuf)) );
    return as_Node(n);
  }

  return as_Node(n);
}


static Node* restype_call(R* r, CallNode* n) {
  n->receiver = resolve(r, n->receiver);

  Type* recvt = (
    is_Expr(n->receiver) ? ((Expr*)n->receiver)->type :
    is_Type(n->receiver) ? kType_type :
    kType_nil
  );

  if (recvt == kType_type)
    return restype_call_type(r, n);

  assert(is_FunTypeNode(recvt));
  return restype_call_fun(r, n);
}


static Node* restype_tuple(R* r, TupleNode* n) {
  Type* typecontext = r->typecontext; // save typecontext
  const TypeArray* ctlist = NULL;
  u32 ctindex = 0;

  if (typecontext) {
    if (typecontext->kind == NTupleType) {
      ctlist = &((TupleTypeNode*)typecontext)->a;
      assert(ctlist->len > 0); // tuples should never be empty
    } else {
      b_errf(r->build, NodePosSpan(typecontext),
        "unexpected context type %s", FMTNODE(typecontext,1));
      n->type = kType_nil;
      return as_Node(n);
    }
    if UNLIKELY(ctlist->len != n->a.len) {
      b_errf(r->build, NodePosSpan(n),
        "%u expressions where %u expressions are expected %s",
        n->a.len, ctlist->len, FMTNODE(typecontext,1));
      n->type = kType_nil;
      return as_Node(n);
    }
  }

  auto t = mknode(r, TupleType, n->pos);
  if (UNLIKELY(t == NULL) || !check_memalloc(r, n, array_reserve(&t->a, n->a.len)))
    return as_Node(n);

  for (u32 i = 0; i < n->a.len; i++) {
    if (ctlist)
      r->typecontext = ctlist->v[ctindex++];

    Expr* cn = as_Expr(resolve(r, n->a.v[i]));
    n->a.v[i] = cn;
    array_push(&t->a, cn->type);
  }

  n->type = as_Type(t);
  r->typecontext = typecontext; // restore typecontext
  return as_Node(n);
}


static Node* restype_array(R* r, ArrayNode* n) {
  panic("TODO (impl probably almost identical to restype_tuple)");
  return as_Node(n);
}


static Node* restype_block(R* r, BlockNode* n) {
  // The type of a block is the type of the last expression

  if (n->a.len == 0) {
    // empty block has no type (void)
    n->type = kType_nil;
    return as_Node(n);
  }

  // resolve all but the last expression without requiring ideal-type resolution
  u32 lasti = n->a.len - 1;
  for (u32 i = 0; i < lasti; i++)
    n->a.v[i] = as_Expr(resolve(r, n->a.v[i]));

  // Last node, in which case we set the flag to resolve literals so that implicit return
  // values gets properly typed.
  flags_scope(r->flags | RF_ResolveIdeal) {
    n->a.v[lasti] = as_Expr(resolve(r, n->a.v[lasti]));
  }

  n->type = n->a.v[lasti]->type;

  return as_Node(n);
}


static Node* restype_field(R* r, FieldNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}


static Node* restype_intlit(R* r, IntLitNode* n) {
  Type* t = r->typecontext ? r->typecontext : kType_int;
  Expr* n2 = ctypecast_implicit(r->build, n, t, NULL, n);
  return as_Node(n2);
}


static Node* restype_floatlit(R* r, FloatLitNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_strlit(R* r, StrLitNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_id(R* r, IdNode* n) {
  assertnotnull(n->target);
  n->target = resolve(r, n->target);
  if (is_Expr(n->target)) {
    n->type = ((Expr*)n->target)->type;
  } else if (is_Type(n->target)) {
    n->type = kType_type;
  } else {
    n->type = kType_nil;
  }
  return as_Node(n);
}


static Node* restype_binop(R* r, BinOpNode* n) {
  Expr* x = n->left;
  Expr* y = n->right;
  bool prefer_y = false;

  typecontext_scope(NULL) {
    if (x->type && x->type != kType_ideal) {
      r->typecontext = x->type;
    } else if (y->type && y->type != kType_ideal) {
      r->typecontext = y->type;
      prefer_y = true;
    }
    x = as_Expr(resolve(r, x));
    y = as_Expr(resolve(r, y));
  }

  // if the types differ, attempt an implicit cast
  if UNLIKELY(x->type != y->type) {
    // note: ctypecast_implicit does a full b_typeeq check before attempting cast
    Expr** xp = prefer_y ? &y : &x;
    Expr** yp = prefer_y ? &x : &y;
    *yp = ctypecast_implicit(r->build, *xp, (*yp)->type, NULL, n);
  }

  n->left = x;
  n->right = y;
  n->type = x->type;

  return as_Node(n);
}


static Node* restype_prefixop(R* r, PrefixOpNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_postfixop(R* r, PostfixOpNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_return(R* r, ReturnNode* n) {
  n->expr = as_Expr(resolve(r, n->expr));
  n->type = n->expr->type;
  return as_Node(n);
}


static Node* restype_assign(R* r, AssignNode* n) {
  // 1. resolve destination (lvalue) and
  // 2. resolve value (rvalue) witin the type context of destination
  flags_scope(r->flags & ~RF_ResolveIdeal) {
    auto typecontext = r->typecontext; // save typecontext
    n->dst = as_Expr(resolve(r, n->dst));
    r->typecontext = (n->dst->type != kType_ideal) ? n->dst->type : NULL;
    n->val = as_Expr(resolve(r, n->val));
    r->typecontext = typecontext; // restore typecontext
  }

  // clear n's NF_Const flag since storing to var upgrades it to mutable.
  Node* leaf = mark_local_mutable(r, n->dst);
  if UNLIKELY(leaf->kind == NConst) {
    Sym name = as_ConstNode(leaf)->name;
    b_errf(r->build, NodePosSpan(n->dst), "cannot store to constant %s", name);
    if (leaf->pos != NoPos)
      b_notef(r->build, NodePosSpan(leaf), "%s defined here", name);
  }

  // the type of the assignment expression is the type of the destination (var/field)
  n->type = n->dst->type;

  // check & convert rvalue type
  if UNLIKELY(n->type->kind == NArrayType) {
    // storing to a local or field of array type is not allowed
    b_errf(r->build, NodePosSpan(n), "array type %s is not assignable", FMTNODE(n->type,0));
  } else if (!b_typelteq(r->build, n->type, n->val->type)) {
    // convert rvalue (if it's a different type than dst)
    n->val = ctypecast_implicit(r->build, n->val, n->type, NULL, n);
  }

  return as_Node(n);
}


static Node* restype_macro(R* r, MacroNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}


static Node* restype_typecast(R* r, TypeCastNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_const(R* r, ConstNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_var(R* r, VarNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_param(R* r, ParamNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_macroparam(R* r, MacroParamNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_ref(R* r, RefNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_namedarg(R* r, NamedArgNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_selector(R* r, SelectorNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_index(R* r, IndexNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_slice(R* r, SliceNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_if(R* r, IfNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); n->type = kType_nil;
  return as_Node(n);
}

static Node* restype_typetype(R* r, TypeTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_namedtype(R* r, NamedTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_aliastype(R* r, AliasTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_reftype(R* r, RefTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_basictype(R* r, BasicTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_arraytype(R* r, ArrayTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_tupletype(R* r, TupleTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_structtype(R* r, StructTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
  return as_Node(n);
}

static Node* restype_funtype(R* r, FunTypeNode* n) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
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
        n->type = as_Type(resolve(r, n->type));
      return np;
    }
  }

  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Field)      return restype_field(r, n);
  GNCASE(CUnit)     return restype_cunit(r, n);
  NCASE(Comment)    // not possible

  NCASE(Nil)        // not possible
  NCASE(BoolLit)    // not possible
  NCASE(IntLit)     return restype_intlit(r, n);
  NCASE(FloatLit)   return restype_floatlit(r, n);
  NCASE(StrLit)     return restype_strlit(r, n);
  NCASE(Id)         return restype_id(r, n);
  NCASE(BinOp)      return restype_binop(r, n);
  NCASE(PrefixOp)   return restype_prefixop(r, n);
  NCASE(PostfixOp)  return restype_postfixop(r, n);
  NCASE(Return)     return restype_return(r, n);
  NCASE(Assign)     return restype_assign(r, n);
  NCASE(Tuple)      return restype_tuple(r, n);
  NCASE(Array)      return restype_array(r, n);
  NCASE(Block)      return restype_block(r, n);
  NCASE(Fun)        return restype_fun(r, n);
  NCASE(Macro)      return restype_macro(r, n);
  NCASE(Call)       return restype_call(r, n);
  NCASE(TypeCast)   return restype_typecast(r, n);
  NCASE(Const)      return restype_const(r, n);
  NCASE(Var)        return restype_var(r, n);
  NCASE(Param)      return restype_param(r, n);
  NCASE(MacroParam) return restype_macroparam(r, n);
  NCASE(Ref)        return restype_ref(r, n);
  NCASE(NamedArg)   return restype_namedarg(r, n);
  NCASE(Selector)   return restype_selector(r, n);
  NCASE(Index)      return restype_index(r, n);
  NCASE(Slice)      return restype_slice(r, n);
  NCASE(If)         return restype_if(r, n);

  NCASE(TypeType)   return restype_typetype(r, n);
  NCASE(NamedType)  return restype_namedtype(r, n);
  NCASE(AliasType)  return restype_aliastype(r, n);
  NCASE(RefType)    return restype_reftype(r, n);
  NCASE(BasicType)  return restype_basictype(r, n);
  NCASE(ArrayType)  return restype_arraytype(r, n);
  NCASE(TupleType)  return restype_tupletype(r, n);
  NCASE(StructType) return restype_structtype(r, n);
  NCASE(FunType)    return restype_funtype(r, n);

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
