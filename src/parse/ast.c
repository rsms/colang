#include "../coimpl.h"
#include "parse.h"


Node* NodeInit(Node* n, NodeKind kind) {
  n->kind = kind;
  switch (kind) {
    case NPkg:
    case NFile: {
      auto N = as_CUnitNode(n);
      array_init(&N->a, N->a_storage, sizeof(N->a_storage));
      break;
    }
    case NBlock: {
      auto N = (BlockNode*)n;
      array_init(&N->a, N->a_storage, sizeof(N->a_storage));
      break;
    }
    case NArray:
    case NTuple: {
      auto N = as_ListExprNode(n);
      array_init(&N->a, N->a_storage, sizeof(N->a_storage));
      break;
    }
    case NSelector: {
      auto N = (SelectorNode*)n;
      array_init(&N->indices, N->indices_storage, sizeof(N->indices_storage));
      break;
    }
    case NTupleType: {
      auto N = (TupleTypeNode*)n;
      array_init(&N->a, N->a_storage, sizeof(N->a_storage));
      break;
    }
    case NStructType: {
      auto N = (StructTypeNode*)n;
      array_init(&N->fields, N->fields_storage, sizeof(N->fields_storage));
      break;
    }
    default:
      break;
  }
  return n;
}


PosSpan _NodePosSpan(const Node* n) {
  assertnotnull(n);
  PosSpan span = { n->pos, n->endpos };
  // dlog("-- NodePosSpan %s %u:%u",
  //   NodeKindName(n->kind), pos_line(n->endpos), pos_col(n->endpos));
  if (!pos_isknown(span.end))
    span.end = span.start;

  switch (n->kind) {
    case NBinOp: {
      auto op = (BinOpNode*)n;
      span.start = op->left->pos;
      span.end = op->right->pos;
      break;
    }
    case NCall: {
      auto call = (CallNode*)n;
      span.start = NodePosSpan(call->receiver).start;
      if (call->args)
        span.end = NodePosSpan(call->args).end;
      break;
    }
    case NTuple: {
      span.start = pos_with_adjusted_start(span.start, -1);
      break;
    }
    case NNamedArg: {
      auto namedarg = (NamedArgNode*)n;
      span.end = NodePosSpan(namedarg->value).end;
      break;
    }
    default:
      break;
  }

  return span;
}


Scope* ScopeNew(Mem mem, const Scope* parent) {
  Scope* s = mem_alloczt(mem, Scope);
  if (!s)
    return NULL;
  //assertf(IS_ALIGN2((uintptr)s, sizeof(void*)), "%p not a pointer aligned address", s);
  s->parent = parent;
  symmap_init(&s->bindings, mem, 1);
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
    void** vp = symmap_find(&scope->bindings, s);
    if (vp != NULL) {
      n = *vp;
      break;
    }
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
