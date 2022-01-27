#include "parse.h"


Scope* scope_new(Mem mem, const Scope* parent) {
  Scope* s = memalloct(mem, Scope);
  if (!s)
    return NULL;
  s->parent = parent;
  // SymMapInit(&s->bindings, mem, 8); // TODO FIXME new SymMapInit w storage
  return s;
}

void scope_free(Scope* s, Mem mem) {
  SymMapDispose(&s->bindings);
  memfree(mem, s);
}

const Node* scope_lookup(const Scope* scope, Sym s) {
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
  static const char* const kNodeNameTable[30] = {
    "Bad", "Pkg", "File", "Comment", "BoolLit", "IntLit", "FloatLit", "StrLit",
    "Nil", "Id", "BinOp", "UnaryOp", "Array", "Fun", "Macro", "Call",
    "TypeCast", "Field", "Var", "Ref", "NamedVal", "Selector", "Index", "Slice",
    "If", "BasicType", "ArrayType", "TupleType", "StructType", "FunType",
  };
  return k < 30 ? kNodeNameTable[k] : "?";
}

//END GENERATED CODE
