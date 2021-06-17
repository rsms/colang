#include "../common.h"
#include "parse.h"
#include <ctype.h> // tolower

//#define DEBUG_LOOKUP


static const Node _NodeBad = {NBad,0,NoPos,NoPos,NULL,{0}};
const Node* NodeBad = &_NodeBad;


// Lookup table N<kind> => name
static const char* const NodeKindNameTable[] = {
  #define I_ENUM(name, _cls) #name,
  DEF_NODE_KINDS(I_ENUM)
  #undef  I_ENUM
};

// Lookup table N<kind> => NClass<class>
const NodeClassFlags _NodeClassTable[_NodeKindMax] = {
  #define I_ENUM(_name, flags) flags,
  DEF_NODE_KINDS(I_ENUM)
  #undef  I_ENUM
};


Node* NewNode(Mem mem, NodeKind kind) {
  Node* n = (Node*)memalloc(mem, sizeof(Node));
  n->kind = kind;
  auto cfl = NodeKindClass(kind);
  if (R_UNLIKELY((cfl & NodeClassArray) != 0)) {
    if (cfl & NodeClassType) {
      ArrayInitWithStorage(&n->t.list.a, n->t.list.a_storage, countof(n->t.list.a_storage));
    } else {
      ArrayInitWithStorage(&n->array.a, n->array.a_storage, countof(n->array.a_storage));
    }
  }
  return n;
}


const char* NodeKindName(NodeKind t) {
  return NodeKindNameTable[t];
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


#ifdef DEBUG
const char* _DebugNodeClassStr(NodeClassFlags fl, u32 lineno) {
  if (fl == 0)
    return "invalid";
  // select a temporary buffer to use
  static char bufs[4][256];
  static u32 bufsn = 0;
  char* buf = bufs[(lineno + bufsn++) % countof(bufs)];
  u32 len = 0;

  #define APPEND(cstr) ({       \
    size_t z = strlen(cstr);    \
    if (len > 0)                \
      buf[len++] = '|';         \
    memcpy(buf+len, (cstr), z); \
    len += z;                   \
  })

  // category
  if (fl & NodeClassLit)  APPEND("lit");
  if (fl & NodeClassExpr) APPEND("expr");
  if (fl & NodeClassType) APPEND("type");

  // data attributes
  if (fl & NodeClassArray) APPEND("array");

  #undef APPEND
  buf[len] = 0;
  return buf;
}
#endif


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


Node* nullable ArrayNodeLast(Node* n) {
  assert(NodeKindClass(n->kind) & NodeClassArray);
  if (n->array.a.len == 0)
    return NULL;
  return n->array.a.v[n->array.a.len - 1];
}


PosSpan NodePosSpan(const Node* n) {
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

    case NLet:
      if (n->let.init)
        span.end = n->let.init->pos;
      break;

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
        *msg = n->ref.name;
        n = n->ref.target;
        break;

      case NCall:
        n = n->call.receiver;
        break;

      case NLet:
        // *msg = n->let.name;
        n = n->let.init;
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


void node_diag_trail(Build* b, DiagLevel dlevel, Node* n) {
  const char* msg = NULL;
  while ((n = diag_trail_next(n, &msg))) {
    diag_trail(b, dlevel, msg, n, 1);
  }
}


Str NodeFlagsStr(NodeFlags fl, Str s) {
  if (fl == NodeFlagsNone)
    return str_appendcstr(s, "0");
  if (fl & NodeFlagUnresolved)
    s = str_appendcstr(s, "unresolved");
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
