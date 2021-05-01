#include <rbase/rbase.h>
#include "parse.h"

//#include "../build/source.h"
//#include "../common/array.h"
//#include "../common/defs.h"
//#include "../common/memory.h"
//#include "../common/ptrmap.h"
//#include "../common/tstyle.h"
//#include "../sym.h"

// #define DEBUG_LOOKUP


// Lookup table N<kind> => name
static const char* const NodeKindNameTable[] = {
  #define I_ENUM(name, _cls) #name,
  DEF_NODE_KINDS(I_ENUM)
  #undef  I_ENUM
};

// Lookup table N<kind> => NClass<class>
const NodeClass NodeClassTable[_NodeKindMax] = {
  #define I_ENUM(_name, cls) NodeClass##cls,
  DEF_NODE_KINDS(I_ENUM)
  #undef  I_ENUM
};


const char* NodeKindName(NodeKind t) {
  return NodeKindNameTable[t];
}

const char* NodeClassName(NodeClass c) {
  switch (c) {
    case NodeClassInvalid: return "Invalid";
    case NodeClassConst:   return "Const";
    case NodeClassExpr:    return "Expr";
    case NodeClassType:    return "Type";
  }
  return "NodeClass?";
}


const Node* NodeEffectiveType(const Node* n) {
  if (!n->type)
    return Type_nil;
  if (NodeIsUntyped(n))
    return IdealType(NodeIdealCType(n));
  return n->type;
}


Node* IdealType(CType ct) {
  switch (ct) {
  case CType_int:   return Type_int;
  case CType_float: return Type_float64;
  case CType_str:   return Type_str;
  case CType_bool:  return Type_bool;
  case CType_nil:   return Type_nil;

  case CType_rune:
  case CType_INVALID: break;
  }
  dlog("err: unexpected CType %d", ct);
  assert(0 && "unexpected CType");
  return NULL;
}


// NodeIdealCType returns a type for an arbitrary "ideal" (untyped constant) expression like "3".
CType NodeIdealCType(const Node* n) {
  if (n == NULL || !NodeIsUntyped(n)) {
    return CType_INVALID;
  }
  dlog("NodeIdealCType n->kind = %s", NodeKindName(n->kind));

  switch (n->kind) {
  default:
    return CType_nil;

  case NIntLit:
  case NFloatLit:
    // Note: NBoolLit is always typed
    return n->val.ct;

  case NPrefixOp:
  case NPostfixOp:
    return NodeIdealCType(n->op.left);

  case NId:
    return NodeIdealCType(n->ref.target);

  case NBinOp:
    switch (n->op.op) {
      case TEq:       // "=="
      case TNEq:      // "!="
      case TLt:       // "<"
      case TLEq:      // "<="
      case TGt:       // ">"
      case TGEq:      // ">="
      case TAndAnd:   // "&&
      case TPipePipe: // "||
        return CType_bool;

      case TShl:
      case TShr:
        // shifts are always of left (receiver) type
        return NodeIdealCType(n->op.left);

      default: {
        auto L = NodeIdealCType(n->op.left);
        auto R = NodeIdealCType(n->op.right);
        return MAX(L, R); // pick the dominant type
      }
    }

  }
}


// NBad node
static const Node _NodeBad = {NBad,NoPos,NULL,{0}};
const Node* NodeBad = &_NodeBad;


// void NodeListAppend(Mem mem, NodeList* a, Node* n) {
//   auto l = (NodeListLink*)memalloc(mem, sizeof(NodeListLink));
//   l->node = n;
//   if (a->tail == NULL) {
//     a->head = l;
//   } else {
//     a->tail->next = l;
//   }
//   a->tail = l;
//   a->len++;
// }

Node* ast_opt_ifcond(Node* n) {
  assert(n->kind == NIf);
  if (n->cond.cond == Const_true) {
    // [optimization] "then" branch always taken
    return n->cond.thenb;
  }
  if (n->cond.cond == Const_false) {
    // [optimization] "then" branch is never taken
    return n->cond.elseb != NULL ? n->cond.elseb : Const_nil;
  }
  return n;
}


// -----------------------------------------------------------------------------------------------
// Scope


Scope* ScopeNew(const Scope* parent, Mem mem) {
  auto s = (Scope*)memalloc(mem, sizeof(Scope));
  s->parent = parent;
  s->childcount = 0;
  SymMapInit(&s->bindings, 8, mem);
  return s;
}


static const Scope* globalScope = NULL;


void ScopeFree(Scope* s, Mem mem) {
  SymMapDispose(&s->bindings);
  memfree(mem, s);
}


const Scope* GetGlobalScope() {
  if (globalScope == NULL) {
    auto s = ScopeNew(NULL, NULL);

    #define X(name) SymMapSet(&s->bindings, sym_##name, (void*)Type_##name);
    TYPE_SYMS(X)
    #undef X

    #define X(name, _typ, _val) SymMapSet(&s->bindings, sym_##name, (void*)Const_##name);
    PREDEFINED_CONSTANTS(X)
    #undef X

    globalScope = s;
  }
  return globalScope;
}


const Node* ScopeAssoc(Scope* s, Sym key, const Node* value) {
  return SymMapSet(&s->bindings, key, (Node*)value);
}


const Node* ScopeLookup(const Scope* scope, Sym s) {
  const Node* n = NULL;
  while (scope && n == NULL) {
    // dlog("[lookup] %s in scope %p(len=%u)", s, scope, scope->bindings.len);
    n = SymMapGet(&scope->bindings, s);
    scope = scope->parent;
  }
  #ifdef DEBUG_LOOKUP
  if (n == NULL) {
    dlog("lookup %s => (null)", s);
  } else {
    dlog("lookup %s => node of kind %s", s, NodeKindName(n->kind));
  }
  #endif
  return n;
}
