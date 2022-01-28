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
    case NBlock:
    case NArray:
    case NTuple: {
      auto N = as_ListExpr(n);
      NodeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NTupleType: {
      auto N = as_TupleTypeNode(n);
      TypeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NStructType: {
      auto N = as_StructTypeNode(n);
      NodeArrayInitStorage(&N->fields, N->fields_storage, countof(N->fields_storage));
      break;
    }
    default:
      break;
  }
  return n;
}


Scope* ScopeNew(Mem mem, const Scope* parent) {
  Scope* s = memalloct(mem, Scope);
  if (!s)
    return NULL;
  s->parent = parent;
  // SymMapInit(&s->bindings, mem, 8); // TODO FIXME new SymMapInit w storage
  return s;
}

void ScopeFree(Scope* s, Mem mem) {
  SymMapDispose(&s->bindings);
  memfree(mem, s);
}

const Node* ScopeLookup(const Scope* scope, Sym s) {
  const Node* n = NULL;
  while (scope && n == NULL) {
    //dlog("[lookup] %s in scope %p(len=%u)", s, scope, scope->bindings.len);
    n = SymMapGet(&scope->bindings, s);
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


//BEGIN GENERATED CODE by ast_gen.py

const char* NodeKindName(NodeKind k) {
  // kNodeNameTable[NodeKind] => const char* name
  static const char* const kNodeNameTable[32] = {
    "Bad", "Pkg", "File", "Comment", "BoolLit", "IntLit", "FloatLit", "StrLit",
    "Nil", "Id", "BinOp", "UnaryOp", "Tuple", "Array", "Block", "Fun", "Macro",
    "Call", "TypeCast", "Field", "Var", "Ref", "NamedVal", "Selector", "Index",
    "Slice", "If", "BasicType", "ArrayType", "TupleType", "StructType",
    "FunType",
  };
  return k < 32 ? kNodeNameTable[k] : "?";
}

//END GENERATED CODE
