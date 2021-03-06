#include "../common.h"
#include "parse.h"
#include <ctype.h> // tolower

//#define DEBUG_LOOKUP


static const Node _NodeBad = {NBad,0,NoPos,NoPos,NULL,NULL,{0}};
const Node* NodeBad = &_NodeBad;


// Lookup table N<kind> => name
static const char* const NodeKindNameTable[] = {
  #define I_ENUM(name, _cls) #name,
  DEF_NODE_KINDS(I_ENUM)
  #undef  I_ENUM
};

// Lookup table N<kind> => NClass<class>
const NodeClass _NodeClassTable[_NodeKindMax] = {
  #define I_ENUM(_name, flags) flags,
  DEF_NODE_KINDS(I_ENUM)
  #undef  I_ENUM
};


const char* NodeKindName(NodeKind nk) {
  return NodeKindNameTable[nk];
}


const char* TypeKindName(TypeKind tk) {
  switch (tk) {
    case TypeKindVoid:     return "void";
    case TypeKindF16:      return "16-bit floating-point number";
    case TypeKindF32:      return "32-bit floating-point number";
    case TypeKindF64:      return "64-bit floating-point number";
    case TypeKindInteger:  return "integer";
    case TypeKindFunction: return "function";
    case TypeKindStruct:   return "struct";
    case TypeKindArray:    return "array";
    case TypeKindPointer:  return "pointer";
    case TypeKindVector:   return "vector";
  }
  return "?";
}


Node* NewNode(Mem mem, NodeKind kind) {
  Node* n = (Node*)memalloc(mem, sizeof(Node));
  n->kind = kind;
  switch (kind) {

  case NPkg:
  case NFile:
    ArrayInitWithStorage(&n->cunit.a, n->cunit.a_storage, countof(n->cunit.a_storage));
    break;

  case NBlock:
  case NArray:
  case NTuple:
    ArrayInitWithStorage(&n->array.a, n->array.a_storage, countof(n->array.a_storage));
    break;

  case NTupleType:
    ArrayInitWithStorage(&n->t.tuple.a, n->t.tuple.a_storage, countof(n->t.tuple.a_storage));
    break;

  case NStructType:
    ArrayInitWithStorage(&n->t.struc.a, n->t.struc.a_storage, countof(n->t.struc.a_storage));
    break;

  default:
    break;
  }
  return n;
}


Type* NewTypeType(Mem mem, Type* tn) {
  Type* n = NewNode(mem, NTypeType);
  n->t.type = tn;
  return n;
}


Node* NodeUnbox(Node* n, bool unrefVars) {
  assertnotnull_debug(n);
  while (1) switch (n->kind) {
    case NVar:
      if (!NodeIsConst(n) || !n->var.init)
        return n;
      if (unrefVars)
        NodeUnrefVar(n);
      n = n->var.init;
      break;

    case NId:
      n = assertnotnull_debug(n->id.target);
      break;

    default:
      return n;
  }
}


static bool _token_eq(char const* ref, char const* input, u32 len) {
  for (; len--; ref++, input++) {
    int d = (unsigned char)*ref - tolower((unsigned char)*input);
    if (d != 0)
      return false;
  }
  return true;
}

inline static bool token_eq(char const* ref, char const* input, u32 inputlen) {
  u32 reflen = (size_t)strlen(ref);
  if (inputlen != reflen)
    return false;
  return _token_eq(ref, input, inputlen);
}


static NodeReprFlags parse_repr_flag(const char* str, u32 len) {
  if (len > 0) {
    switch (tolower(str[0])) {
      case 'n':
        if (token_eq("nocolor", str, len))
          return NodeReprNoColor;
        break;
      case 'c':
        if (token_eq("color", str, len))
          return NodeReprColor;
        break;
      case 't':
        if (token_eq("types", str, len))
          return NodeReprTypes;
        break;
      case 'u':
        if (token_eq("usecount", str, len))
          return NodeReprUseCount;
        break;
      case 'r':
        if (token_eq("refs", str, len))
          return NodeReprRefs;
        break;
      case 'a':
        if (token_eq("attrs", str, len))
          return NodeReprAttrs;
        break;
    }
  }
  return 0;
}


NodeReprFlags NodeReprFlagsParse(const char* str, u32 len) {
  NodeReprFlags fl = 0;
  const char* start = NULL;
  const char* end = str + len;
  while (str < end) {
    u8 b = *(const u8*)str;
    switch (b) {
      // ignore whitespace
      case '0'...'9':
      case 'A'...'Z':
      case 'a'...'z':
        if (!start)
          start = str;
        break;

      default:
        if (start) {
          fl |= parse_repr_flag(start, (u32)(str - start));
          start = NULL;
        }
        break;
    }
    str++;
  }
  if (start)
    fl |= parse_repr_flag(start, (u32)(str - start));
  return fl;
}


R_TEST(parse_node_repr_flags) {
  #define PARSE_FLAGS(cstr) NodeReprFlagsParse((cstr), strlen(cstr))

  // no flags
  auto fl = PARSE_FLAGS(" adsfknsdf slm;dfkm\ngarbage");
  asserteq(fl, 0);

  // some flags
  fl = PARSE_FLAGS(" bla types");
  asserteq(fl, NodeReprTypes);

  // all flags
  fl = PARSE_FLAGS("nocolor color types usecount refs attrs");
  asserteq(fl, NodeReprNoColor
             | NodeReprColor
             | NodeReprTypes
             | NodeReprUseCount
             | NodeReprRefs
             | NodeReprAttrs
  );

  #undef PARSE_FLAGS
}


const char* NodeClassStr(NodeClass nc) {
  switch (nc) {
    case NodeClassNone: return "none";
    case NodeClassLit:  return "lit";
    case NodeClassExpr: return "expr";
    case NodeClassType: return "type";
    case NodeClassMeta: return "meta";
  }
  return "?";
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
  case CType_float: return Type_f64;
  case CType_str:   return Type_str;
  case CType_bool:  return Type_bool;
  case CType_nil:   return Type_nil;

  case CType_rune:
  case CType_INVALID: break;
  }
  assertf(0, "unexpected CType %d", ct);
  return NULL;
}


// NodeIdealCType returns a type for an arbitrary "ideal" (untyped constant) expression like "3".
CType NodeIdealCType(const Node* n) {
  if (n == NULL || !NodeIsUntyped(n)) {
    return CType_INVALID;
  }
  //dlog("NodeIdealCType n->kind = %s", NodeKindName(n->kind));

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
    return NodeIdealCType(n->id.target);

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


PosSpan NodePosSpan(const Node* n) {
  assertnotnull_debug(n);
  PosSpan span = { n->pos, n->endpos };
  // dlog("-- NodePosSpan %s %u:%u",
  //   NodeKindName(n->kind), pos_line(n->endpos), pos_col(n->endpos));
  if (!pos_isknown(span.end))
    span.end = span.start;

  switch (n->kind) {
    case NBinOp:
      span.start = n->op.left->pos;
      span.end = n->op.right->pos;
      break;

    case NCall:
      span.start = NodePosSpan(n->call.receiver).start;
      if (n->call.args)
        span.end = NodePosSpan(n->call.args).end;
      break;

    case NTuple:
      span.start = pos_with_adjusted_start(span.start, -1);
      break;

    case NNamedVal:
      span.end = NodePosSpan(n->namedval.value).end;
      break;

    // case NVar:
    //   if (n->var.init)
    //     span.end = n->var.init->pos;
    //   break;

    default:
      break;
  }

  return span;
}


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


// err_trail_node returns the child node of n, if any, which should be included in error trails
//
// Example:
//   fun main() nil {
//     addfn = add
//     return addfn(1, 2)
//   }
//   fun add(x, y int) int {
//     x + y
//   }
//
// Output:
//   example/hello.co:3:10: error: cannot use result from call (type int) as return type nil
//     return addfn(1, 2)
//            ~~~~~~~~~~~
//
//   example/hello.co:2:3: info: addfn defined here
//     addfn = add
//     ~~~~~
//
//   example/hello.co:5:1: info: fun add defined here
//   fun add(x int, y uint) int {
//   ~~~
//
static Node* nullable diag_trail_next(Node* n, const char** msg) {
  *msg = NULL;
  while (1) {
    switch (n->kind) {

      case NId:
        *msg = n->id.name;
        n = n->id.target;
        break;

      case NCall:
        n = n->call.receiver;
        break;

      case NVar:
        // *msg = n->var.name;
        n = n->var.init;
        break;

      // TODO: more node kinds
      default:
        //dlog(">> %s", NodeKindName(n->kind));
        return NULL;
    }
    if (!n || n->kind != NId)
      break;
  }
  return n;
}


static void diag_trail(Build* b, DiagLevel dlevel, const char* nullable msg, Node* n, int depth) {
  build_diagf(b, dlevel, NodePosSpan(n), "%s defined here", msg ? msg : fmtnode(n));
}


void node_diag_trailn(Build* b, DiagLevel dlevel, Node* n, u32 limit) {
  const char* msg = NULL;
  while ((n = diag_trail_next(n, &msg)) && limit != 0) {
    diag_trail(b, dlevel, msg, n, 1);
    limit--;
  }
}


Str NodeFlagsStr(NodeFlags fl, Str s) {
  if (fl == NodeFlagsNone)
    return str_appendcstr(s, "0");

  if (fl & NodeFlagUnresolved)  { s = str_appendcstr(s, "Unresolved"); }
  if (fl & NodeFlagConst)       { s = str_appendcstr(s, "Const"); }
  if (fl & NodeFlagBase)        { s = str_appendcstr(s, "Base"); }
  if (fl & NodeFlagRValue)      { s = str_appendcstr(s, "RValue"); }
  if (fl & NodeFlagParam)       { s = str_appendcstr(s, "Param"); }
  if (fl & NodeFlagMacroParam)  { s = str_appendcstr(s, "MacroParam"); }
  if (fl & NodeFlagCustomInit)  { s = str_appendcstr(s, "CustomInit"); }
  if (fl & NodeFlagUnused)      { s = str_appendcstr(s, "Unused"); }
  if (fl & NodeFlagPublic)      { s = str_appendcstr(s, "Public"); }
  if (fl & NodeFlagNamed)       { s = str_appendcstr(s, "Named"); }

  return s;
}


// -----------------------------------------------------------------------------------------------
// Scope


Scope* ScopeNew(const Scope* parent, Mem mem) {
  auto s = (Scope*)memalloc(mem, sizeof(Scope));
  s->parent = parent;
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
    auto s = ScopeNew(NULL, MemHeap);

    #define X(name, ...) SymMapSet(&s->bindings, sym_##name, (void*)Type_##name);
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
