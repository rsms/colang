// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"


PosSpan _NodePosSpan(const Node* np) {
  assertnotnull(np);
  PosSpan span = { np->pos, np->endpos };
  // dlog("-- NodePosSpan %s %u:%u",
  //   NodeKindName(np->kind), pos_line(np->endpos), pos_col(np->endpos));
  if (!pos_isknown(span.end))
    span.end = span.start;

  switch (np->kind) {
    case NBinOp: {
      auto n = (BinOpNode*)np;
      span.start = pos_min(span.start, n->left->pos);
      span.end = pos_max(span.end, n->right->pos);
      break;
    }
    case NCall: {
      auto n = (CallNode*)np;
      span.start = pos_min(span.start, NodePosSpan(n->receiver).start);
      if (n->args.len > 0) {
        Pos last_arg_end = NodePosSpan(n->args.v[n->args.len - 1]).end;
        span.end = pos_union(span.end, last_arg_end);
      }
      break;
    }
    case NTuple: {
      span.start = pos_with_adjusted_start(span.start, -1);
      break;
    }
    case NNamedArg: {
      auto n = (NamedArgNode*)np;
      span.end = pos_max(span.end, NodePosSpan(n->value).end);
      break;
    }
    // case NAssign: {
    //   // NOTE: for this to work, we would need to stop using simplify_id
    //   auto n = (AssignNode*)np;
    //   span.start = n->dst->pos;
    //   span.end = n->val->pos;
    //   break;
    // }
    default:
      break;
  }

  return span;
}


Type* unbox_id_type1(IdTypeNode* t) {
  if (t->target)
    return unbox_id_type(t->target);
  return (Type*)t;
}


bool ScopeInit(Scope* s, Mem mem, const Scope* nullable parent) {
  s->parent = parent;
  if (hmap_isvalid(&s->bindings)) {
    symmap_clear(&s->bindings);
    return true;
  }
  return symmap_init(&s->bindings, mem, 0) != NULL;
}


Scope* nullable ScopeNew(Mem mem, const Scope* nullable parent) {
  Scope* s = mem_alloczt(mem, Scope);
  if (!s)
    return NULL;
  if (!ScopeInit(s, mem, parent)) {
    mem_free(mem, s, sizeof(*s));
    return NULL;
  }
  return s;
}

void ScopeFree(Scope* s, Mem mem) {
  symmap_free(&s->bindings);
  mem_free(mem, s, sizeof(Scope));
}

error ScopeAssign(Scope* s, Sym key, Node* n, Mem mem) {
  void** valp = symmap_assign(&s->bindings, key);
  if (UNLIKELY(valp == NULL))
    return err_nomem;
  *valp = n;
  return 0;
}

Node* ScopeLookup(const Scope* nullable scope, Sym s) {
  Node* n = NULL;
  while (scope) {
    //dlog("[lookup] %s in scope %p(len=%zu)", s, scope, map_len(&scope->bindings));
    n = symmap_find(&scope->bindings, s);
    if (n)
      break;
    scope = scope->parent;
  }
  #ifdef DEBUG_LOOKUP
  if (n == NULL) {
    dlog("ScopeLookup(%p) %s => (null)", scope, s);
  } else {
    dlog("ScopeLookup(%p) %s => node of kind %s", scope, s, NodeKindName(n->kind));
  }
  #endif
  return n;
}


//———————————————————————————————————————————————————————————————————————————————————————
// ASTVisit
#ifdef ASTVisit

static void visit_nodearray(
  ASTVisitor* v,
  usize       flags,
  ASTParent*  parent,
  Node*       n,
  NodeArray*  a,
  const char* field_name)
{
  parent->field_name = field_name;
  for (u32 i = 0; i < a->len; i++) {
    parent->child_ptr = &a->v[i];
    ASTVisit(v, flags, parent, a->v[i]);
  }
}


static void visit_nodefield(
  ASTVisitor* v, usize flags, ASTParent* parent, Node** np, const char* field_name)
{
  if (!*np)
    return;
  parent->field_name = field_name;
  parent->child_ptr = np;
  ASTVisit(v, flags, parent, *np);
}


int _ASTVisitChildren(ASTVisitor* v, usize flags, const ASTParent* parent_of_n, Node* np) {
  #define N(FIELD) \
    visit_nodefield(v, flags, &parent, (Node**)&n->FIELD, #FIELD)

  #define A(ARRAY_FIELD) \
    visit_nodearray(v, flags, &parent, np, as_NodeArray(&n->ARRAY_FIELD), #ARRAY_FIELD)

  #define AP(ARRAY_FIELD) \
    visit_nodearray(v, flags, &parent, np, as_NodeArray(n->ARRAY_FIELD), #ARRAY_FIELD)

  // break cycles
  uintptr* vp = pmap_assign(&v->seenmap, np);
  if (!vp || *vp) // out of memory or already visited
    return 0;

  ASTParent parent = {
    .parent = parent_of_n,
    .n = np,
  };

  //dlog("visit children of %s", nodename(np));

  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Field)   N(type); N(init);
  GNCASE(CUnit)  A(a);
  NCASE(Comment)

  GNCASE(LitExpr)
  NCASE(Id)               N(target);
  NCASE(BinOp)            N(left); N(right);
  GNCASE(UnaryOp)         N(expr);
  NCASE(Return)           N(expr);
  NCASE(Assign)           N(val); N(dst);
  GNCASE(ListExpr)        A(a);
  NCASE(Fun)              A(params); N(result); N(body);
  NCASE(Template)         A(params); N(body);
  NCASE(TemplateInstance) N(tpl); A(args);
  NCASE(Call)             N(receiver); A(args);
  NCASE(TypeCast)         N(expr);
  NCASE(Const)            N(value);
  NCASE(Var)              N(init);
  NCASE(Param)            N(init);
  NCASE(TemplateParam)    N(init);
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
  NCASE(FunType)       AP(params); N(result);
  NCASE(TemplateType)
  NCASE(TemplateParamType) N(param);

  }}

  // register as visited
  *vp = 1;
  if (parent.n != np) {
    // node was replaced; also register original node
    vp = pmap_assign(&v->seenmap, np);
    if (vp)
      *vp = 1;
  }

  // visit type of expression
  if (is_Expr(np) && ((Expr*)np)->type)
    visit_nodefield(v, flags, &parent, (Node**)&((Expr*)np)->type, "type");

  return 1;
}


#endif // ASTVisit
