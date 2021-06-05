#pragma once
#include "../util/symmap.h"
#include "../util/array.h"

ASSUME_NONNULL_BEGIN

// NodeClassFlags classifies AST Node kinds
typedef enum {
  NodeClassInvalid = 0,

  // category
  NodeClassConst   = 1 << 0, // literals like 123, true, nil.
  NodeClassExpr    = 1 << 1, // e.g. (+ x y)
  NodeClassType    = 1 << 2, // e.g. i32

  // data attributes
  NodeClassArray   = 1 << 3, // uses Node.array, or Node.t.array if NodeClassType
} NodeClassFlags;

// DEF_NODE_KINDS defines primary node kinds which are either expressions or start of expressions
#define DEF_NODE_KINDS(_) \
  /* N<kind>     classification */ \
  _(None,        NodeClassInvalid) \
  _(Bad,         NodeClassInvalid) /* substitute "filler node" for invalid syntax */ \
  _(BoolLit,     NodeClassConst) /* boolean literal */ \
  _(IntLit,      NodeClassConst) /* integer literal */ \
  _(FloatLit,    NodeClassConst) /* floating-point literal */ \
  _(StrLit,      NodeClassConst) /* string literal */ \
  _(Nil,         NodeClassConst) /* the nil atom */ \
  _(Assign,      NodeClassExpr) \
  _(Arg,         NodeClassExpr) \
  _(Block,       NodeClassExpr|NodeClassArray) \
  _(Call,        NodeClassExpr) \
  _(Field,       NodeClassExpr) \
  _(Pkg,         NodeClassExpr|NodeClassArray) \
  _(File,        NodeClassExpr|NodeClassArray) \
  _(Fun,         NodeClassExpr) \
  _(Id,          NodeClassExpr) \
  _(If,          NodeClassExpr) \
  _(Let,         NodeClassExpr) \
  _(BinOp,       NodeClassExpr) \
  _(PrefixOp,    NodeClassExpr) \
  _(PostfixOp,   NodeClassExpr) \
  _(Return,      NodeClassExpr) \
  _(Tuple,       NodeClassExpr|NodeClassArray) \
  _(TypeCast,    NodeClassExpr) \
  _(BasicType,   NodeClassType) /* int, bool, ... */ \
  _(TupleType,   NodeClassType|NodeClassArray) /* (float,bool,int) */ \
  _(ArrayType,   NodeClassType) /* [4]int, []int */ \
  _(FunType,     NodeClassType) /* (int,int)->(float,bool) */ \
/*END DEF_NODE_KINDS*/

// NodeKind { NNone, NBad, NBoolLit, ... ,_NodeKindMax }
typedef enum {
  #define I_ENUM(name, _cls) N##name,
  DEF_NODE_KINDS(I_ENUM)
  #undef I_ENUM
  _NodeKindMax
} NodeKind;

// NodeKindName returns the name of node kind constant
const char* NodeKindName(NodeKind);

// DebugNodeClassStr returns a printable representation of NodeClassFlags. Not thread safe!
#ifdef DEBUG
  #define DebugNodeClassStr(fl) _DebugNodeClassStr((fl), __LINE__)
  const char* _DebugNodeClassStr(NodeClassFlags, u32 lineno);
#else
  #define DebugNodeClassStr(fl) ("NodeClassFlags")
#endif

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

// AST_SIZE_UNKNOWN used for Node.t.array.size when the size is unresolved
#define AST_SIZE_UNKNOWN 0xFFFFFFFFFFFFFFFF

// NodeFlags
typedef enum {
  NodeFlagsNone      = 0,
  NodeFlagUnresolved = 1 << 0, // contains unresolved references. MUST BE VALUE 1!
} NodeFlags;

typedef struct Node {
  NodeKind       kind;   // kind of node (e.g. NId)
  NodeFlags      flags;  // flags describe meta attributes of the node
  Pos            pos;    // source origin & position
  Pos            endpos; // Used by compount types like tuple. NoPos means "only use pos".
  Node* nullable type;   // value type. NULL if unknown.
  union {
    void* _never; // for initializers
    NVal val; // BoolLit, IntLit, FloatLit, StrLit
    /* str */ struct { // Comment
      const u8* ptr;
      size_t    len;
    } str;
    /* ref */ struct { // Id
      Sym   name;
      Node* target;
    } ref;
    /* op */ struct { // BinOp, PrefixOp, PostfixOp, Return, Assign
      Node*          left;
      Node* nullable right;  // NULL for PrefixOp & PostfixOp
      Tok            op;
    } op;
    /* array */ struct { // Tuple, Block, File, Pkg
      Scope* nullable scope;        // used for Pkg and File
      NodeArray       a;            // array of nodes
      Node*           a_storage[4]; // in-struct storage for the first few entries of a
    } array;
    /* fun */ struct { // Fun
      Node* nullable tparams; // template params (NTuple)
      Node* nullable params;  // input params (NTuple or NULL if none)
      Node* nullable result;  // output results (NTuple | NExpr)
      Sym   nullable name;    // NULL for lambda
      Node* nullable body;    // NULL for fun-declaration
    } fun;
    /* call */ struct { // Call, TypeCast
      Node* receiver;      // Fun, Id or type
      Node* nullable args; // NULL if there are no args, else a NTuple
    } call;
    /* field */ struct { // Arg, Field, Let
      Sym            name;
      Node* nullable init;  // Field: initial value (may be NULL). Let: final value (never NULL).
      u32            index; // Arg: argument index.
      u32            nrefs; // Let: reference count
    } field;
    /* cond */ struct { // If
      Node*          cond;
      Node*          thenb;
      Node* nullable elseb; // NULL or expr
    } cond;

    // Type
    /* t */ struct {
      Sym nullable id; // lazy; initially NULL. Computed from Node.
      union {
        /* basic */ struct { // BasicType
          TypeCode typeCode;
          Sym      name;
        } basic;
        /* list */ struct { // TupleType
          NodeArray a;            // array of nodes
          Node*     a_storage[4]; // in-struct storage for the first few entries of a
        } list;
        /* array */ struct { // ArrayType
          Node* nullable sizeExpr; // NULL==slice (language type: usize)
          u64            size; // only used for array, not slice. 0 until sizeExpr is resolved.
          Node*          subtype;
        } array;
        /* fun */ struct { // FunType
          Node* nullable params; // kind==NTupleType
          Node* nullable result; // tuple or single type
        } fun;
      };
    } t;

  }; // union
} Node;

// NodeReprFlags changes behavior of NodeRepr
typedef enum {
  NodeReprDefault = 0,
  NodeReprNoColor = 1 << 0, // disable ANSI terminal styling
  NodeReprColor   = 1 << 1, // enable ANSI terminal styling (even if stderr is not a TTY)
  NodeReprTypes   = 1 << 2, // include types in the output
  NodeReprLetRefs = 1 << 3, // include information about Let references / uses
} NodeReprFlags;

// NodeRepr formats an AST as a printable text representation
Str NodeRepr(const Node* nullable n, Str s, NodeReprFlags fl);

// NodeStr appends a short representation of an AST node to s.
Str NodeStr(Str s, const Node* nullable n);

// fmtast returns an exhaustive representation of n using NodeRepr with NodeReprPretty.
// This function is not suitable for high-frequency use as it uses temporary buffers in TLS.
ConstStr fmtast(const Node* nullable n);

// fmtnode returns a short representation of n using NodeStr, suitable for error messages.
// This function is not suitable for high-frequency use as it uses temporary buffers in TLS.
ConstStr fmtnode(const Node* nullable n);

// NodeKindClass returns NodeClassFlags for kind. It's a fast inline table lookup.
static NodeClassFlags NodeKindClass(NodeKind kind);

// NodeKindIs{Type|Const|Expr} returns true if kind is of class Type, Const or Expr.
inline static bool NodeKindIsType(NodeKind kind) { return NodeKindClass(kind) & NodeClassType; }
inline static bool NodeKindIsConst(NodeKind kind) { return NodeKindClass(kind) & NodeClassConst;}
inline static bool NodeKindIsExpr(NodeKind kind) { return NodeKindClass(kind) & NodeClassExpr; }

// NodeIs{Type|Const|Expr} calls NodeKindIs{Type|Const|Expr}(n->kind)
inline static bool NodeIsType(const Node* n) { return NodeKindIsType(n->kind); }
inline static bool NodeIsConst(const Node* n) { return NodeKindIsConst(n->kind); }
inline static bool NodeIsExpr(const Node* n) { return NodeKindIsExpr(n->kind); }

// Node{Is,Set,Clear}Unresolved controls the "unresolved" flag on a node
inline static bool NodeIsUnresolved(const Node* n) { return (n->flags & NodeFlagUnresolved) != 0; }
inline static void NodeSetUnresolved(Node* n) { n->flags |= NodeFlagUnresolved; }
inline static void NodeClearUnresolved(Node* n) { n->flags &= ~NodeFlagUnresolved; }
inline static void NodeTransferUnresolved(Node* parent, Node* child) {
  parent->flags |= child->flags & NodeFlagUnresolved;
}

// NodeRefLet increments the reference counter of a Let node. Returns n as a convenience.
static Node* NodeRefLet(Node* n);

// NodeUnrefLet decrements the reference counter of a Let node.
// Returns the value of n->field.nrefs after the subtraction.
static u32 NodeUnrefLet(Node* n);

// NodeFlagsStr appends a printable description of fl to s
Str NodeFlagsStr(NodeFlags fl, Str s);

// Retrieve the effective "printable" type of a node.
// For nodes which are lazily typed, like IntLit, this returns the default type of the constant.
const Node* NodeEffectiveType(const Node* n);

// NodeIdealCType returns a type for an arbitrary "ideal" (untyped constant) expression like "3".
CType NodeIdealCType(const Node* n);

// IdealType returns the constant type node for a ctype
Node* nullable IdealType(CType ct);

// NodeIsUntyped returns true for untyped constants, like for example "x = 123"
inline static bool NodeIsUntyped(const Node* n) {
  return n->type == Type_ideal;
}

// ast_opt_ifcond attempts to optimize an NIf node with constant expression conditions
Node* ast_opt_ifcond(Node* n);

// Format an NVal
Str NValFmt(Str s, const NVal v);

// ArrayNodeLast returns the last element of array node n or NULL if empty
Node* nullable ArrayNodeLast(Node* n);


// NodePosSpan returns the Pos span representing the logical span of the node.
// For example, for a tuple that is the pos of the first to last element, inclusive.
PosSpan NodePosSpan(const Node* n);

static void NodeArrayAppend(Mem mem, Array* a, Node* n);
static void NodeArrayClear(Array* a);


extern const Node* NodeBad; // kind==NBad

// NewNode allocates a node in mem
Node* NewNode(Mem mem, NodeKind kind);

// NodeCopy creates a shallow copy of n in mem
static Node* NodeCopy(Mem mem, const Node* n);

// node_diag_trail calls b->diagh zero or more times with contextual information that forms a
// trail to the provided node n. For example, if n is a call the trail will report on the
// function that is called along with any identifier indirections.
// Note: The output does NOT include n itself.
void node_diag_trail(Build* b, DiagLevel dlevel, Node* n);

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

// NodeVisitChildren calls for each child of n, passing along n and data to f.
bool NodeVisitChildren(NodeList* parent, void* nullable data, NodeVisitor f);

// NodeValidate checks an AST for inconsistencies. Useful for debugging and development.
bool NodeValidate(Build* b, Node* n);



// -----------------------------------------------------------------------------------------------
// implementations

extern const NodeClassFlags _NodeClassTable[_NodeKindMax];

inline static NodeClassFlags NodeKindClass(NodeKind kind) {
  return _NodeClassTable[kind];
}

inline static Node* NodeCopy(Mem mem, const Node* n) {
  assert((NodeKindClass(n->kind) & NodeClassArray) == 0); // no support for copying these yet
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

inline static bool NodeVisit(const Node* n, void* nullable data, NodeVisitor f) {
  NodeList parent = { .n = n };
  return f(&parent, data);
}

inline static Node* NodeRefLet(Node* n) {
  asserteq_debug(n->kind, NLet);
  n->field.nrefs++;
  return n;
}

inline static u32 NodeUnrefLet(Node* n) {
  asserteq_debug(n->kind, NLet);
  assertgt_debug(n->field.nrefs, 0);
  return --n->field.nrefs;
}

ASSUME_NONNULL_END
