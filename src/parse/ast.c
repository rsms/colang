#include "../coimpl.h"
#include "parse.h"


Node* NodeInit(Node* n, NodeKind kind) {
  n->kind = kind;
  switch (kind) {
    case NPkg:
    case NFile: {
      auto N = as_CUnitNode(n);
      NodeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NBlock: {
      auto N = (BlockNode*)n;
      NodeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NArray:
    case NTuple: {
      auto N = as_ListExprNode(n);
      ExprArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NSelector: {
      auto N = (SelectorNode*)n;
      U32ArrayInitStorage(&N->indices, N->indices_storage, countof(N->indices_storage));
      break;
    }
    case NTupleType: {
      auto N = (TupleTypeNode*)n;
      TypeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NStructType: {
      auto N = (StructTypeNode*)n;
      FieldArrayInitStorage(&N->fields, N->fields_storage, countof(N->fields_storage));
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
  Scope* s = memalloczt(mem, Scope);
  if (!s)
    return NULL;
  //assertf(IS_ALIGN2((uintptr)s, sizeof(void*)), "%p not a pointer aligned address", s);
  s->parent = parent;
  map_init_small(&s->bindings);
  return s;
}

void ScopeFree(Scope* s, Mem mem) {
  symmap_free(&s->bindings, mem);
  memfree(mem, s);
}

error ScopeAssign(Scope* s, Sym key, Node* n, Mem mem) {
  void** valp = symmap_assign(&s->bindings, key, mem);
  if (UNLIKELY(valp == NULL))
    return err_nomem;
  *valp = n;
  return 0;
}

Node* ScopeLookup(const Scope* nullable scope, Sym s) {
  Node* n = NULL;
  while (scope) {
    //dlog("[lookup] %s in scope %p(len=%zu)", s, scope, map_len(&scope->bindings));
    void** vp = symmap_access(&scope->bindings, s);
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
