// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"


Node* NodeInit(Node* np, NodeKind kind) {
  np->kind = kind;
  switch ((enum NodeKind)kind) { case NBad: {
  GNCASE(CUnit)
    array_init(&n->a, n->a_storage, sizeof(n->a_storage));
  NCASE(Block)
    array_init(&n->a, n->a_storage, sizeof(n->a_storage));
  GNCASE(ListExpr)
    array_init(&n->a, n->a_storage, sizeof(n->a_storage));
  NCASE(Selector)
    array_init(&n->indices, n->indices_storage, sizeof(n->indices_storage));
  NCASE(TupleType)
    array_init(&n->a, n->a_storage, sizeof(n->a_storage));
  NCASE(StructType)
    array_init(&n->fields, n->fields_storage, sizeof(n->fields_storage));
  NDEFAULTCASE
    break;
  }}
  return np;
}

#define COPY_ARRAY(dst, src) \
  if UNLIKELY(!array_append((dst), (src)->v, (src)->len)) \
    return NULL

Node* nullable NodeCopy(Node* np, const Node* src) {
  memcpy(np, src, sizeof(union NodeUnion));
  NodeInit(np, np->kind);
  switch ((enum NodeKind)np->kind) { case NBad: {
  GNCASE(CUnit)
    COPY_ARRAY(&n->a, &((CUnitNode*)src)->a);
  NCASE(Block)
    COPY_ARRAY(&n->a, &((BlockNode*)src)->a);
  GNCASE(ListExpr)
    COPY_ARRAY(&n->a, &((ListExprNode*)src)->a);
  NCASE(Selector)
    COPY_ARRAY(&n->indices, &((SelectorNode*)src)->indices);
  NCASE(TupleType)
    COPY_ARRAY(&n->a, &((TupleTypeNode*)src)->a);
  NCASE(StructType)
    COPY_ARRAY(&n->fields, &((StructTypeNode*)src)->fields);
  NDEFAULTCASE
    break;
  }}
  return np;
}


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
      span.start = n->left->pos;
      span.end = n->right->pos;
      break;
    }
    case NCall: {
      auto n = (CallNode*)np;
      span.start = NodePosSpan(n->receiver).start;
      if (n->args)
        span.end = NodePosSpan(n->args).end;
      break;
    }
    case NTuple: {
      span.start = pos_with_adjusted_start(span.start, -1);
      break;
    }
    case NNamedArg: {
      auto n = (NamedArgNode*)np;
      span.end = NodePosSpan(n->value).end;
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
