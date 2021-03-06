#pragma once
#include "../util/symmap.h"
#include "../util/array.h"

ASSUME_NONNULL_BEGIN

// NodeClass classifies AST Node kinds
typedef enum {
  NodeClassNone = 0,

  // category
  NodeClassLit,  // literals like 123, true, nil.
  NodeClassExpr, // e.g. (+ x y)
  NodeClassType, // e.g. i32
  NodeClassMeta, // e.g. TypeType
} NodeClass;

// DEF_NODE_KINDS defines primary node kinds which are either expressions or start of expressions
#define DEF_NODE_KINDS(_) \
  /* N<kind>     classification */ \
  _(None,        NodeClassNone) \
  _(Bad,         NodeClassNone) /* substitute "filler node" for invalid syntax */ \
  _(Pkg,         NodeClassMeta) \
  _(File,        NodeClassMeta) \
  _(BoolLit,     NodeClassLit) /* boolean literal */ \
  _(IntLit,      NodeClassLit) /* integer literal */ \
  _(FloatLit,    NodeClassLit) /* floating-point literal */ \
  _(StrLit,      NodeClassLit) /* string literal */ \
  _(Nil,         NodeClassLit) /* the nil atom */ \
  _(Assign,      NodeClassExpr) \
  _(Block,       NodeClassExpr) \
  _(Call,        NodeClassExpr) \
  _(Field,       NodeClassExpr) \
  _(Selector,    NodeClassExpr) \
  _(Index,       NodeClassExpr) \
  _(Slice,       NodeClassExpr) \
  _(Fun,         NodeClassExpr) \
  _(Id,          NodeClassExpr) \
  _(If,          NodeClassExpr) \
  _(Var,         NodeClassExpr) \
  _(Ref,         NodeClassExpr) \
  _(NamedVal,    NodeClassExpr) \
  _(BinOp,       NodeClassExpr) \
  _(PrefixOp,    NodeClassExpr) \
  _(PostfixOp,   NodeClassExpr) \
  _(Return,      NodeClassExpr) \
  _(Array,       NodeClassExpr) \
  _(Tuple,       NodeClassExpr) \
  _(TypeCast,    NodeClassExpr) \
  _(Macro,       NodeClassExpr) /* TODO: different NodeClass */ \
  /* types */ \
  _(BasicType,   NodeClassType) /* int, bool, ... */ \
  _(RefType,     NodeClassType) /* &T */ \
  _(ArrayType,   NodeClassType) /* [4]int, []int */ \
  _(TupleType,   NodeClassType) /* (float,bool,int) */ \
  _(StructType,  NodeClassType) /* struct{foo float; y bool} */ \
  _(FunType,     NodeClassType) /* fun(int,int)(float,bool) */ \
  _(TypeType,    NodeClassMeta) /* type of a type */ \
/*END DEF_NODE_KINDS*/

// NodeKind { NNone, NBad, NBoolLit, ... ,_NodeKindMax }
typedef enum {
  #define I_ENUM(name, _cls) N##name,
  DEF_NODE_KINDS(I_ENUM)
  #undef I_ENUM
  _NodeKindMax
} NodeKind;

// AST node types
typedef struct Node Node;
typedef Node Type;

// Scope represents a lexical namespace which may be chained.
typedef struct Scope Scope;
typedef struct Scope {
  const Scope* parent;
  SymMap       bindings;
} Scope;

Scope* ScopeNew(const Scope* nullable parent, Mem mem);
void ScopeFree(Scope*, Mem mem);
const Node* ScopeAssoc(Scope*, Sym, const Node* value); // Returns replaced value or NULL
const Node* ScopeLookup(const Scope*, Sym);
const Scope* GetGlobalScope();


typedef Array NodeArray;

// NVal contains the value of basic types of literal nodes
typedef struct NVal {
  CType ct;
  union {
    u64    i;  // BoolLit, IntLit
    double f;  // FloatLit
    Str    s;  // StrLit
  };
} NVal;

// TypeKind
typedef enum {
  TypeKindVoid,     // type with no size
  TypeKindF16,      // 16 bit floating point type
  TypeKindF32,      // 32 bit floating point type
  TypeKindF64,      // 64 bit floating point type
  TypeKindInteger,  // Arbitrary bit width integers
  TypeKindFunction, // Functions
  TypeKindStruct,   // Structures
  TypeKindArray,    // Arrays
  TypeKindPointer,  // Pointers
  TypeKindVector,   // Fixed width SIMD vector type
} TypeKind; // similar to LLVMTypeKind

// NodeFlags
typedef enum {
  NodeFlagsNone       = 0,
  NodeFlagUnresolved  = 1 << 0,  // contains unresolved references. MUST BE VALUE 1!
  NodeFlagConst       = 1 << 1,  // constant; value known at compile time (comptime)
  NodeFlagBase        = 1 << 2,  // [struct field] the field is a base of the struct
  NodeFlagRValue      = 1 << 4,  // resolved as rvalue
  NodeFlagParam       = 1 << 5,  // [Var] function parameter
  NodeFlagMacroParam  = 1 << 6,  // [Var] macro parameter
  NodeFlagCustomInit  = 1 << 7,  // [StructType] has fields w/ non-zero initializer
  NodeFlagUnused      = 1 << 8,  // [Var] never referenced
  NodeFlagPublic      = 1 << 9,  // [Var|Fun] public visibility (aka published, exported)
  NodeFlagNamed       = 1 << 11, // [Tuple when used as args] has named argument
  NodeFlagPartialType = 1 << 12, // Type resolver should visit even if the node is typed
} NodeFlags; // remember to update NodeFlagsStr impl

typedef struct Node {
  NodeKind       kind;   // kind of node (e.g. NId)
  NodeFlags      flags;  // flags describe meta attributes of the node
  Pos            pos;    // source origin & position
  Pos            endpos; // Used by compount types like tuple. NoPos means "only use pos".
  Node* nullable type;   // value type. NULL if unknown.
  void* nullable irval;  // used by IR builders for temporary storage
  union {
    void* _never; // for initializers
    NVal val; // BoolLit, IntLit, FloatLit, StrLit
    /* str */ struct { // Comment
      const u8* ptr;
      size_t    len;
    } str;
    /* id */ struct { // Id
      Sym   name;
      Node* target;
    } id;
    /* op */ struct { // BinOp, PrefixOp, PostfixOp, Return, Assign
      Node*          left;
      Node* nullable right;  // NULL for PrefixOp & PostfixOp
      Tok            op;
    } op;
    /* cunit */ struct { // File, Pkg
      ConstStr        name;         // reference to str in corresponding Source/Pkg struct
      Scope* nullable scope;
      NodeArray       a;            // array of nodes
      Node*           a_storage[4]; // in-struct storage for the first few entries of a
    } cunit;
    /* array */ struct { // Tuple, Block, Array
      NodeArray a;            // array of nodes
      Node*     a_storage[6]; // in-struct storage for the first few entries of a
    } array;
    /* fun */ struct { // Fun
      Node* nullable params;  // input params (NTuple or NULL if none)
      Node* nullable result;  // output results (NTuple | NExpr)
      Sym   nullable name;    // NULL for lambda
      Node* nullable body;    // NULL for fun-declaration
    } fun;
    /* macro */ struct { // Macro
      Node* nullable params;  // input params (NTuple or NULL if none)
      Sym   nullable name;
      Node*          template;
    } macro;
    /* call */ struct { // Call, TypeCast
      Node* receiver;      // Fun, Id or type
      Node* nullable args; // NULL if there are no args, else a NTuple
    } call;
    /* field */ struct { // Field
      Sym            name;
      Node* nullable init;  // initial value (may be NULL)
      u32            nrefs; // reference count
      u32            index; // argument index or struct index
    } field;
    /* var */ struct { // Var
      Sym            name;
      Node* nullable init;    // initial/default value
      u32            nrefs;   // reference count
      u32            index;   // argument index (used by function parameters)
      bool           isconst; // immutable storage? (true for "const x" vars)
    } var;
    /* ref */ struct { // Ref
      Node* target;
    } ref;
    /* namedval */ struct { // NamedVal
      Sym   name;
      Node* value;
    } namedval;
    /* sel */ struct { // Selector = Expr "." ( Ident | Selector )
      Node*               operand;
      Sym                 member;  // id
      TARRAY_TYPE(u32,10) indices; // GEP index path
    } sel;
    /* index */ struct { // Index = Expr "[" Expr "]"
      Node* operand;
      Node* indexexpr;
      u32   index; // 0xffffffff if indexexpr is not a compile-time constant
    } index;
    /* slice */ struct { // Slice = Expr "[" Expr? ":" Expr? "]"
      Node*          operand;
      Node* nullable start;
      Node* nullable end;
    } slice;
    /* cond */ struct { // If
      Node*          cond;
      Node*          thenb;
      Node* nullable elseb; // NULL or expr
    } cond;

    // Type
    /* t */ struct {
      Sym nullable id; // lazy; initially NULL. Computed from Node.
      TypeKind     kind;
      union {
        /* basic */ struct { // BasicType (int, bool, auto, etc)
          TypeCode typeCode;
          Sym      name;
        } basic;
        /* array */ struct { // ArrayType
          Node* nullable sizeexpr; // NULL for inferred types
          u32            size;     // used for array. 0 until sizeexpr is resolved
          Node*          subtype;
        } array;
        /* tuple */ struct { // TupleType
          NodeArray a;            // Node[]
          Node*     a_storage[4]; // in-struct storage for the first few elements
        } tuple;
        /* struc */ struct { // StructType
          Sym nullable name;         // NULL for anonymous structs
          NodeArray    a;            // NField[]
          Node*        a_storage[3]; // in-struct storage for the first few fields
        } struc;
        /* fun */ struct { // FunType
          Node* nullable params; // NTuple of NVar or null if no params
          Type* nullable result; // NTupleType of types or single type
        } fun;
        Type* ref;  // RefType element
        Type* type; // TypeType type
      };
    } t;

  }; // union
} Node;

static_assert(sizeof(Node) <= 112, "Node struct grew. Update this or revert change.");

// NodeReprFlags changes behavior of NodeRepr
typedef enum {
  NodeReprDefault  = 0,
  NodeReprNoColor  = 1 << 0, // disable ANSI terminal styling
  NodeReprColor    = 1 << 1, // enable ANSI terminal styling (even if stderr is not a TTY)
  NodeReprTypes    = 1 << 2, // include types in the output
  NodeReprUseCount = 1 << 3, // include information about uses (ie for Var)
  NodeReprRefs     = 1 << 4, // include "#N" reference indicators
  NodeReprAttrs    = 1 << 5, // include "@attr" attributes
} NodeReprFlags;

// NodeRepr formats an AST as a printable text representation
Str NodeRepr(const Node* nullable n, Str s, NodeReprFlags fl);

// NodeReprFlagsParse parses named flags.
// All characters except a-zA-Z0-9 are ignored and treated as separators.
// Names are the tail end of constants, e.g. "UseCount" == NodeReprUseCount.
// The names of flags are case-insensitive, i.e. "UseCount" == "USECOUNT" == "usecount".
// Names which are not recognized are ignored.
NodeReprFlags NodeReprFlagsParse(const char* str, u32 len);

// NodeStr appends a short representation of an AST node to s.
Str NodeStr(Str s, const Node* nullable n);

// fmtast returns an exhaustive representation of n using NodeRepr with NodeReprPretty.
// This function is not suitable for high-frequency use as it uses temporary buffers in TLS.
ConstStr fmtast(const Node* nullable n);

// fmtnode returns a short representation of n using NodeStr, suitable for error messages.
// This function is not suitable for high-frequency use as it uses temporary buffers in TLS.
ConstStr fmtnode(const Node* nullable n);

// NodeKindName returns the name of node kind constant
const char* NodeKindName(NodeKind);

// TypeKindName returns the name of type kind constant
const char* TypeKindName(TypeKind);

// NodeClassStr returns a printable representation of NodeClass
const char* NodeClassStr(NodeClass);

// NodeKindClass returns NodeClass for kind. It's a fast inline table lookup.
static NodeClass NodeKindClass(NodeKind kind);

// NodeKindIs{Type|Expr} returns true if kind is of class Type or Expr.
inline static bool NodeKindIsType(NodeKind kind) {
  return NodeKindClass(kind) == NodeClassType; }
inline static bool NodeKindIsExpr(NodeKind kind) {
  return NodeKindClass(kind) == NodeClassExpr; }

// NodeIs{Type|Expr} calls NodeKindIs{Type|Expr}(n->kind)
static bool NodeIsType(const Node* n);
static bool NodeIsExpr(const Node* n);
static bool NodeIsPrimitiveConst(const Node* n); // NNil, NBasicType, NBoolLit

static bool NodeHasNVal(const Node* n); // true if n uses n->val

// Node{Is,Set,Clear}Unresolved controls the "unresolved" flag of a node
inline static bool NodeIsUnresolved(const Node* n) { return (n->flags & NodeFlagUnresolved) != 0; }
inline static void NodeSetUnresolved(Node* n) { n->flags |= NodeFlagUnresolved; }
inline static void NodeClearUnresolved(Node* n) { n->flags &= ~NodeFlagUnresolved; }
inline static void NodeTransferUnresolved(Node* parent, Node* child) {
  parent->flags |= child->flags & NodeFlagUnresolved;
}

// Node{Is,Set,Clear}Const controls the "constant value" flag of a node
inline static bool NodeIsConst(const Node* n) { return (n->flags & NodeFlagConst) != 0; }
inline static void NodeSetConst(Node* n) { n->flags |= NodeFlagConst; }
inline static void NodeClearConst(Node* n) { n->flags &= ~NodeFlagConst; }
inline static void NodeTransferConst(Node* parent, Node* child) {
  // parent is mutable if n OR child is NOT const, else parent is marked const.
  parent->flags |= child->flags & NodeFlagConst;
}
inline static void NodeTransferMut(Node* parent, Node* child) {
  // parent is const if n AND child is const, else parent is marked mutable.
  parent->flags = (parent->flags & ~NodeFlagConst) | (
    (parent->flags & NodeFlagConst) &
    (child->flags & NodeFlagConst)
  );
}
inline static void NodeTransferMut2(Node* parent, Node* child1, Node* child2) {
  // parent is const if n AND child1 AND child2 is const, else parent is marked mutable.
  parent->flags = (parent->flags & ~NodeFlagConst) | (
    (parent->flags & NodeFlagConst) &
    (child1->flags & NodeFlagConst) &
    (child2->flags & NodeFlagConst)
  );
}

// Node{Is,Set,Clear}Param controls the "is function parameter" flag of a node
inline static bool NodeIsParam(const Node* n) { return (n->flags & NodeFlagParam) != 0; }
inline static void NodeSetParam(Node* n) { n->flags |= NodeFlagParam; }
inline static void NodeClearParam(Node* n) { n->flags &= ~NodeFlagParam; }

// Node{Is,Set,Clear}MacroParam controls the "is function parameter" flag of a node
inline static bool NodeIsMacroParam(const Node* n) {
  return (n->flags & NodeFlagMacroParam) != 0; }
inline static void NodeSetMacroParam(Node* n) { n->flags |= NodeFlagMacroParam; }
inline static void NodeClearMacroParam(Node* n) { n->flags &= ~NodeFlagMacroParam; }

// Node{Is,Set,Clear}Unused controls the "is unused" flag of a node
inline static bool NodeIsUnused(const Node* n) { return (n->flags & NodeFlagUnused) != 0; }
inline static void NodeSetUnused(Node* n) { n->flags |= NodeFlagUnused; }
inline static void NodeClearUnused(Node* n) { n->flags &= ~NodeFlagUnused; }

// Node{Is,Set,Clear}Public controls the "is public" flag of a node
inline static bool NodeIsPublic(const Node* n) { return (n->flags & NodeFlagPublic) != 0; }
inline static void NodeSetPublic(Node* n) { n->flags |= NodeFlagPublic; }
inline static void NodeClearPublic(Node* n) { n->flags &= ~NodeFlagPublic; }

// Node{Is,Set,Clear}RValue controls the "is rvalue" flag of a node
inline static bool NodeIsRValue(const Node* n) { return (n->flags & NodeFlagRValue) != 0; }
inline static void NodeSetRValue(Node* n) { n->flags |= NodeFlagRValue; }
inline static void NodeClearRValue(Node* n) { n->flags &= ~NodeFlagRValue; }

inline static void NodeTransferCustomInit(Node* parent, Node* child) {
  parent->flags |= child->flags & NodeFlagCustomInit;
}
inline static void NodeTransferPartialType2(Node* parent, Node* c1, Node* c2) {
  parent->flags |= (c1->flags & NodeFlagPartialType) |
                   (c2->flags & NodeFlagPartialType);
}

// NodeRefVar increments the reference counter of a Var node. Returns n as a convenience.
static Node* NodeRefVar(Node* n); // NVar
static Node* NodeRefAny(Node* n); // any

// NodeUnrefVar decrements the reference counter of a Var node.
// Returns the value of n->var.nrefs after the subtraction.
static u32 NodeUnrefVar(Node* n); // NVar

// NodeUnbox returns the effective value of n by unboxing NId to its target
// and immutable variables to their initializer.
// If unrefVars is true, NodeUnrefVar is called on each constant var that's unboxed.
Node* NodeUnbox(Node* n, bool unrefVars);

// NodeFlagsStr appends a printable description of fl to s
Str NodeFlagsStr(NodeFlags fl, Str s);

// Retrieve the effective "printable" type of a node.
// For nodes which are lazily typed, like IntLit, this returns the default type of the constant.
const Type* NodeEffectiveType(const Node* n);

// NodeIdealCType returns a type for an arbitrary "ideal" (untyped constant) expression like "3".
CType NodeIdealCType(const Node* n);

// IdealType returns the constant type node for a ctype
Node* nullable IdealType(CType ct);

// NodeIsUntyped returns true for untyped constants, like for example "x = 123"
inline static bool NodeIsUntyped(const Node* n) { return n->type == Type_ideal; }

// ast_opt_ifcond attempts to optimize an NIf node with constant expression conditions
Node* ast_opt_ifcond(Node* n);

// Format an NVal
Str NValFmt(Str s, const NVal v);


// NodePosSpan returns the Pos span representing the logical span of the node.
// For example, for a tuple that is the pos of the first to last element, inclusive.
PosSpan NodePosSpan(const Node* n);

static void NodeArrayAppend(Mem mem, Array* a, Node* n);
static void NodeArrayClear(Array* a);

// NodeArrayLast returns the last element of a or NULL if empty
static Node* nullable NodeArrayLast(Array* a);


extern const Node* NodeBad; // kind==NBad

// NewNode allocates a node in mem
Node* NewNode(Mem mem, NodeKind kind);

// NewTypeType allocates a NTypeType for type tn in mem
Type* NewTypeType(Mem mem, Type* tn);

// NodeCopy creates a shallow copy of n in mem
static Node* NodeCopy(Mem mem, const Node* n);

// node_diag_trail calls b->diagh zero or more times with contextual information that forms a
// trail to the provided node n. For example, if n is a call the trail will report on the
// function that is called along with any identifier indirections.
// Note: The output does NOT include n itself.
void node_diag_trailn(Build* b, DiagLevel dlevel, Node* n, u32 limit);
inline static void node_diag_trail(Build* b, DiagLevel dlevel, Node* n) {
  return node_diag_trailn(b, dlevel, n, 0xffffffff);
}

// -----------------------
// AST visitor

// NodeList is a linked list of nodes
typedef struct NodeList NodeList;
struct NodeList {
  const Node*          n;
  NodeList* nullable   parent;
  u32                  index;     // index in parent (valid when parent is a kind of list)
  const char* nullable fieldname; // name in parent (NULL if it does not apply)
};

// NodeVisitor is used with NodeVisit to traverse an AST.
// To visit the node's children, call NodeVisitChildren(n,data).
// If fieldname is non-null it is the symbolic name of nl->n in nl->parent.
// Return false to stop iteration.
typedef bool(*NodeVisitor)(NodeList* nl, void* nullable data);

// NodeVisit calls f for each child of n, passing along data to f.
// Returns true if all calls to f returns true.
// Example:
//   static bool visit(NodeList* nl, void* data) {
//     dlog("%s", NodeKindName(nl->n->kind));
//     return NodeVisitChildren(nl, visit, data);
//   }
//   NodeVisit(n, visit, NULL);
static bool NodeVisit(const Node* n, void* nullable data, NodeVisitor f);
static bool NodeVisitp(
  NodeList* nullable parent, const Node* n, void* nullable data, NodeVisitor f);

// NodeVisitChildren calls for each child of n, passing along n and data to f.
bool NodeVisitChildren(NodeList* parent, void* nullable data, NodeVisitor f);

// NodeValidateFlags changes behavior of NodeValidate
typedef enum {
  NodeValidateDefault      = 0,
  NodeValidateMissingTypes = 1 << 0, // all types must be resolved
} NodeValidateFlags;

// NodeValidate checks an AST for inconsistencies. Useful for debugging and development.
bool NodeValidate(Build* b, Node* n, NodeValidateFlags fl);



// -----------------------------------------------------------------------------------------------
// implementations

extern const NodeClass _NodeClassTable[_NodeKindMax];

inline static NodeClass NodeKindClass(NodeKind kind) {
  return _NodeClassTable[kind];
}

inline static bool NodeHasNVal(const Node* n) {
  assertnotnull_debug(n);
  switch (n->kind) {
    case NBoolLit:
    case NIntLit:
    case NFloatLit:
    case NStrLit:
      return true;
    default:
      return false;
  }
}

inline static bool NodeIsType(const Node* n) {
  assertnotnull_debug(n);
  return NodeKindIsType(n->kind);
}

inline static bool NodeIsExpr(const Node* n) {
  assertnotnull_debug(n);
  return NodeKindIsExpr(n->kind);
}

inline static bool NodeIsPrimitiveConst(const Node* n) {
  assertnotnull_debug(n);
  switch (n->kind) {
    case NNil:
    case NBasicType:
    case NBoolLit:
      return true;
    default:
      return false;
  }
}

inline static Node* NodeCopy(Mem mem, const Node* n) {
  Node* n2 = (Node*)memalloc(mem, sizeof(Node));
  memcpy(n2, n, sizeof(Node));
  return n2;
}

inline static void NodeArrayAppend(Mem mem, Array* a, Node* n) {
  ArrayPush(a, n, mem);
}

inline static void NodeArrayClear(Array* a) {
  ArrayClear(a);
}

inline static Node* nullable NodeArrayLast(Array* a) {
  return a->len == 0 ? NULL : a->v[a->len - 1];
}

inline static bool NodeVisit(const Node* n, void* nullable data, NodeVisitor f) {
  NodeList parent = { .n = n };
  return f(&parent, data);
}

inline static bool NodeVisitp(
  NodeList* nullable p, const Node* n, void* nullable data, NodeVisitor f)
{
  NodeList parent = { .n = n, .parent = p };
  return f(&parent, data);
}

inline static Node* NodeRefVar(Node* n) {
  asserteq_debug(n->kind, NVar);
  n->var.nrefs++;
  return n;
}

inline static u32 NodeUnrefVar(Node* n) {
  asserteq_debug(n->kind, NVar);
  assertgt_debug(n->var.nrefs, 0);
  return --n->var.nrefs;
}

inline static Node* NodeRefAny(Node* n) {
  if (n->kind == NVar)
    n->var.nrefs++;
  return n;
}

ASSUME_NONNULL_END
