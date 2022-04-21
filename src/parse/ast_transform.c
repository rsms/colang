// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"
#include "ast_transform.h"

// CO_PARSE_ATR_DEBUG: define to enable trace logging
//#define CO_PARSE_ATR_DEBUG
//———————————————————————————————————————————————————————————————————————————————————
#if defined(CO_PARSE_ATR_DEBUG) && defined(DEBUG)
  #ifndef CO_NO_LIBC
    #include <unistd.h> // isatty
  #else
    #define isatty(fd) false
  #endif
  #define atr_dlog(fmt, args...) ({                                      \
    if (isatty(2)) log("\e[1;34m▍atr│ \e[0m%*s" fmt, a->depth*2, "", ##args); \
    else           log("[atr] %*s"         fmt, a->depth*2, "", ##args); \
    fflush(stderr); })
#else
  #undef CO_PARSE_ATR_DEBUG
  #define atr_dlog(fmt, args...) ((void)0)
#endif
//———————————————————————————————————————————————————————————————————————————————————

enum ATRFlag {
  ATR_MUTABLE = 1 << 0,
};

typedef struct ATR {
  BuildCtx*     build;
  TemplateNode* tpl;
  NodeArray     tplvals; // indexed by TemplateParamNode.index
  int           depth;
  PMap          trmap; // oldnode => newnode
} ATR;


#define VISIT_PARAMS  ATR* a, usize flags
#define VISIT_ARGS    a, flags

// #define VISIT_PARAMS  ATR* a, usize flags, FunNode* nullable fn
// #define VISIT_ARGS    a, flags, fn

static Node* atr_visit1(VISIT_PARAMS, Node* n);


static usize atr_copy_node(VISIT_PARAMS, Node** p1, Node** p2, Node** pn) {
  assertf((flags & ATR_MUTABLE) == 0, "trying to copy our own copy");
  assertf(((*pn)->flags & NF_Shared) == 0, "trying to copy a shared node");
  *pn = b_copy_node(a->build, *pn);
  *p1 = *pn;
  *p2 = *pn;
  atr_dlog("~ copy %s node => %p", nodename(*pn), *pn);
  return flags | ATR_MUTABLE;
}


static usize atr_visit_field(
  VISIT_PARAMS, Node** p1, Node** p2, Node** pn, usize noffs, Node* n1)
{
  Node* n2 = atr_visit1(VISIT_ARGS, n1);
  if (n1 == n2)
    return flags;

  if ((flags & ATR_MUTABLE) == 0)
    flags = atr_copy_node(VISIT_ARGS, p1, p2, pn);

  // update field in new node copy
  assertf(noffs == ALIGN2(noffs,sizeof(void*)), "misaligned pointer offset");
  *((void**)&((u8*)*pn)[noffs]) = n2;

  return flags;
}


static usize atr_visit_array(
  VISIT_PARAMS, Node** p1, Node** p2, Node** pn, usize noffs, NodeArray* na)
{
  u32 i = 0;
  if (flags & ATR_MUTABLE)
    goto mut;

  for (; i < na->len; i++) {
    Node* cn2 = atr_visit1(VISIT_ARGS, na->v[i]);
    if (cn2 == na->v[i])
      continue;

    asserteq(noffs, (usize)(uintptr)((void*)na - (void*)*pn));

    // copy node
    flags = atr_copy_node(VISIT_ARGS, p1, p2, pn);

    // update node array pointer and set new value
    na = (void*)(*pn) + noffs;
    na->v[i] = cn2;

    // transition to mutable state
    flags |= ATR_MUTABLE;
    i++;
    goto mut;
  }
  return flags;

mut:
  for (; i < na->len; i++) {
    Node* cn2 = atr_visit1(VISIT_ARGS, na->v[i]);
    if (cn2 != na->v[i])
      na->v[i] = cn2;
  }
  return flags;
}


static Node* atr_visit1(VISIT_PARAMS, Node* np) {
  // short circuit visited and replaced nodes
  uintptr* vp = pmap_assign(&a->trmap, np);
  if UNLIKELY(!vp) {
    b_err_nomem(a->build, NodePosSpan(np));
    return np;
  }
  if (*vp) {
    atr_dlog("skip  %-*s %p => %p", (int)(25 - (a->depth*2)), nodename(np), np, (void*)*vp);
    return (Node*)*vp;
  }
  *vp = (uintptr)np;
  Node* np_orig = np;

  atr_dlog("enter %-*s %p (parent: %s)",
    (int)(25 - (a->depth*2)), nodename(np), np,
    (flags & ATR_MUTABLE) ? "mut" : "const");

  a->depth++;

  // Clear "mutable" state from parent.
  // Then, if we see ATR_MUTABLE in flags we know that np is our copy to edit as we wish.
  flags &= ~ATR_MUTABLE;

  #define N(FIELD) if (n->FIELD) { \
    flags = atr_visit_field( \
      VISIT_ARGS, (Node**)vp, &np, (Node**)&n, \
      offsetof(__typeof__(*n),FIELD), (Node*)n->FIELD); \
  }

  #define A(ARRAY_FIELD) { \
    flags = atr_visit_array( \
      VISIT_ARGS, (Node**)vp, &np, (Node**)&n, \
      offsetof(__typeof__(*n),ARRAY_FIELD), as_NodeArray(&n->ARRAY_FIELD)); \
  }

  switch ((enum NodeKind)np->kind) { case NBad: {

  // ——— common cases, simply visiting fields ———

  NCASE(Field)   N(type); N(init);
  GNCASE(CUnit)  A(a);
  NCASE(Comment)

  GNCASE(LitExpr)
  NCASE(BinOp)            N(left); N(right);
  GNCASE(UnaryOp)         N(expr);
  NCASE(Return)           N(expr);
  NCASE(Assign)           N(val); N(dst);
  GNCASE(ListExpr)        A(a);
  NCASE(Template)         A(params); N(body);
  NCASE(TemplateInstance) N(tpl); A(args);
  NCASE(Call)             N(receiver); A(args);
  NCASE(TypeCast)         N(expr);
  NCASE(Const)            N(value);
  NCASE(Var)              N(init);
  NCASE(Param)            N(init);
  NCASE(Ref)              N(target);
  NCASE(NamedArg)         N(value);
  NCASE(Selector)         N(operand);
  NCASE(Index)            N(operand); N(indexexpr);
  NCASE(Slice)            N(operand); N(start); N(end);
  NCASE(If)               N(cond); N(thenb); N(elseb);
  NCASE(TypeExpr)         N(elem);

  NCASE(TypeType)
  NCASE(IdType)        N(target);
  NCASE(AliasType)     N(elem);
  NCASE(RefType)       N(elem);
  NCASE(BasicType)
  NCASE(ArrayType)     N(elem);
  NCASE(TupleType)     A(a);
  NCASE(StructType)    A(fields);
  NCASE(TemplateType)
  NCASE(FunType)       N(result);

  // ——— special cases ———

  NCASE(Fun)
    A(params); N(result); N(body);
    // fn = n; // fn in VISIT_ARGS
    if (flags & ATR_MUTABLE) // function changed
      n->type = NULL;        // make analyzer recreate its type

  NCASE(Id)
    if (!is_TemplateParamNode(n->target)) {
      N(target);
      break;
    }
    // when T is a type Y:
    //   (Id T (TemplateParam T)) => (TypeExpr Y)
    // else when T is an expression S:
    //   (Id T (TemplateParam T)) => (Id T S)
    TemplateParamNode* tp = (TemplateParamNode*)n->target;
    Node* target = a->tplvals.v[tp->index];
    if (is_Type(target)) {
      TypeExprNode* n2 = b_mknode(a->build, TypeExpr, np->pos);
      np = (Node*)n2;
      *vp = (uintptr)n2;
      n2->elem = (Type*)target;
      n2->type = kType_type;
    } else {
      flags = atr_copy_node(VISIT_ARGS, (Node**)vp, &np, (Node**)&n);
      n->target = as_Expr(target);
    }

  NCASE(TemplateParam)
    assert(n->index < a->tplvals.len);
    Node* value = a->tplvals.v[n->index];
    np = value;
    *vp = (uintptr)np;
    atr_dlog("replaced TemplateParam with %s", nodename(np));

  NCASE(TemplateParamType)
    assert(n->param->index < a->tplvals.len);
    Node* value = a->tplvals.v[n->param->index];
    np = value;
    *vp = (uintptr)np;
    atr_dlog("replaced TemplateParamType with %s", nodename(np));

  }}

  // visit type of expression
  if (is_Expr(np) && ((Expr*)np)->type) {
    Expr* n = (Expr*)np;
    N(type);
  }

  // mark node as shared if it did not change and the parent is not a copy
  if ((np->flags & NF_Shared) == 0)
    SET_FLAG(np->flags, NF_Shared, np_orig == np);

  a->depth--;
  atr_dlog("leave %-*s %p", (int)(25 - (a->depth*2)), nodename(np), np);

  return np;
}


Node* atr_visit_template(BuildCtx* build, TemplateNode* tpl, NodeArray* tplvals) {
  ATR a = {
    .build = build,
    .tpl = tpl,
    .tplvals = *tplvals,
  };

  if UNLIKELY(!pmap_init(&a.trmap, mem_ctx(), 64, MAPLF_1))
    return b_err_nomem(build, NodePosSpan(tpl));

  return atr_visit1(&a, 0, tpl->body);
  // return atr_visit1(&a, 0, NULL, tpl->body);
}
