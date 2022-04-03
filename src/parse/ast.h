// AST nodes
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef struct Scope     Scope;     // lexical namespace (may be chained)
typedef struct Node      Node;      // AST node, basis for Stmt, Expr and Type
typedef struct Stmt      Stmt;      // AST statement
typedef struct Expr      Expr;      // AST expression
typedef struct LitExpr   LitExpr;   // AST constant literal expression
typedef struct Type      Type;      // AST type
typedef struct FieldNode FieldNode; // AST struct field
typedef struct TupleNode TupleNode;
typedef struct LocalNode LocalNode; // Const | Var | Param
typedef struct CUnitNode CUnitNode;
typedef struct ListExprNode ListExprNode;
typedef struct UnaryOpNode UnaryOpNode;

typedef u8  NodeKind;  // AST node kind (NNone, NBad, NBoolLit ...)
typedef u16 NodeFlags; // NF_* constants; AST node flags (Unresolved, Const ...)

typedef Array(Node*)      NodeArray;
typedef Array(Expr*)      ExprArray;
typedef Array(Type*)      TypeArray;
typedef Array(FieldNode*) FieldArray;

#define as_NodeArray(n) _Generic((n), \
  const ExprArray*:(const NodeArray*)(n), ExprArray*:(NodeArray*)(n), \
  const TypeArray*:(const NodeArray*)(n), TypeArray*:(NodeArray*)(n), \
  const FieldArray*:(const NodeArray*)(n), FieldArray*:(NodeArray*)(n), \
  const NodeArray*:(n), NodeArray*:(n) )

struct Node {
  void* nullable irval;  // used by IR builders for temporary storage
  Pos            pos;    // source origin & position
  Pos            endpos; // NoPos means "only use pos".
  NodeFlags      flags;  // flags describe meta attributes of the node
  NodeKind       kind;   // kind of node (e.g. NId)
} ATTR_PACKED;

struct BadNode { Node; }; // substitute "filler" for invalid syntax

struct FieldNode { Node;
  Type* nullable type;  // TODO: can type really be null?
  Sym            name;
  Expr* nullable init;  // initial value (may be NULL)
  u32            nrefs; // reference count
  u32            index; // argument index or struct index
};

// statements
struct Stmt { Node; };
struct CUnitNode { Stmt;
  const char*     name;         // reference to str in corresponding BuildCtx
  Scope* nullable scope;
  NodeArray       a;            // array of nodes
  Node*           a_storage[4]; // in-struct storage for the first few entries of a
};
struct PkgNode { CUnitNode; };
struct FileNode { CUnitNode; };
struct CommentNode { Stmt;
  u32       len;
  const u8* ptr;
};

// expressions
struct Expr { Node;
  Type* nullable type; // value type. NULL if unknown.
};

// literal constant expressions
struct LitExpr { Expr; };
struct NilNode      { LitExpr; };           // the nil atom
struct BoolLitNode  { LitExpr; u64 ival; }; // boolean literal
struct IntLitNode   { LitExpr; u64 ival; }; // integer literal
struct FloatLitNode { LitExpr; f64 fval; }; // floating-point literal
struct StrLitNode   { LitExpr;
  const char* sp;
  u32         len;
};

struct IdNode { Expr;
  Sym            name;
  Node* nullable target; // TODO: change type to Expr
};
struct BinOpNode { Expr;
  Tok   op;
  Expr* left;
  Expr* right;
};
struct UnaryOpNode { Expr;
  Tok   op;
  Expr* expr;
};
struct PrefixOpNode { UnaryOpNode; };
struct PostfixOpNode { UnaryOpNode; };
struct ReturnNode { Expr;
  Expr* expr;
};
struct AssignNode { Expr;
  Expr* dst; // assignment target (Local | Tuple | Index)
  Expr* val; // value
};
struct ListExprNode { Expr;
  ExprArray a;            // array of nodes
  Expr*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct TupleNode { ListExprNode; };
struct ArrayNode { ListExprNode; };
struct BlockNode { Expr;
  ExprArray a;            // array of nodes
  Expr*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct FunNode { Expr;
  TupleNode* nullable params; // input params (NULL if none)
  Type* nullable      result; // output results (TupleType for multiple results)
  Sym   nullable      name;   // NULL for lambda
  Expr* nullable      body;   // NULL for fun-declaration
};
struct MacroNode { Expr;
  Sym nullable        name;
  TupleNode* nullable params;  // input params (LocalNodes)
  Node*               template;
};
struct CallNode { Expr;
  Node*               receiver; // Type | Fun | Id
  TupleNode* nullable args;     // NULL if there are no args
};
struct TypeCastNode { Expr;
  Expr* expr;
  // Note: destination type in Expr.type
};
struct LocalNode { Expr;
  u32 nrefs; // reference count
  u32 index; // argument index (used by function parameters)
  Sym name;
};
struct ConstNode { struct LocalNode;
  Expr* value; // value
};
struct VarNode { struct LocalNode;
  Expr* nullable init; // initial/default value
};
struct ParamNode { struct LocalNode;
  Expr* nullable init; // initial/default value
};
struct MacroParamNode { struct LocalNode;
  Node* nullable init; // initial/default value
};
struct RefNode { Expr;
  Expr* target;
};
struct NamedArgNode { Expr;
  Sym   name;
  Expr* value;
};
struct SelectorNode { Expr; // Selector = Expr "." ( Ident | Selector )
  u32      indices_storage[7]; // indices storage
  U32Array indices; // GEP index path
  Expr*    operand;
  Sym      member;  // id
};
struct IndexNode { Expr; // Index = Expr "[" Expr "]"
  u32   index; // 0xffffffff if indexexpr is not a compile-time constant
  Expr* operand;
  Expr* indexexpr;
};
struct SliceNode { Expr; // Slice = Expr "[" Expr? ":" Expr? "]"
  Expr*          operand;
  Expr* nullable start;
  Expr* nullable end;
};
struct IfNode { Expr;
  Expr*          cond;
  Expr*          thenb;
  Expr* nullable elseb; // NULL or expr
};

// types
struct Type { Node ;
  TypeFlags    tflags; // u16 (Note: used to be TypeKind kind)
  Sym nullable tid;    // initially NULL for user-defined types, computed as needed
};
struct TypeTypeNode { Type; }; // typeof(int) => type
struct NamedTypeNode { Type; // used for named types which are not yet resolved, like Id
  Sym name;
};
struct AliasTypeNode { Type; // like a const var but for types, eg "type foo int"
  Sym   name;
  Type* type;
};
struct RefTypeNode { Type;
  Type* elem; // e.g. for RefType "&int", elem is "int" (a BasicType)
};
struct BasicTypeNode { Type;
  TypeCode typecode;
  Sym      name;
};
struct ArrayTypeNode { Type;
  u32            size;     // used for array. 0 until sizeexpr is resolved
  Expr* nullable sizeexpr; // NULL for inferred types
  Type*          elem;
};
struct TupleTypeNode { Type;
  TypeArray a;            // Type[]
  Type*     a_storage[5]; // in-struct storage for the first few elements
};
struct StructTypeNode { Type;
  Sym nullable name;              // NULL for anonymous structs
  FieldArray   fields;            // FieldNode[]
  FieldNode*   fields_storage[4]; // in-struct storage for the first few fields
};
struct FunTypeNode { Type;
  TupleNode* nullable params; // == FunNode.params
  Type* nullable      result; // == FunNode.result (TupleType or single type)
};
// struct FunTypeNode { Type;
//   FieldArray     params;            // FieldNode[]
//   FieldNode*     params_storage[4]; // in-struct storage for the first few parameters
//   Type* nullable result;            // TupleType or single type
// };


// forward decl of things defined in universe but referenced by ast.h
extern Type* kType_type;

struct Scope {
  const Scope* nullable parent;
  SymMap bindings;
};
static_assert(offsetof(Scope,bindings) == sizeof(Scope)-sizeof(((Scope*)0)->bindings),
  "bindings is not last member of Scope");


enum NodeFlags {
  NF_Unresolved  = 1 << 0, // contains unresolved references. MUST BE VALUE 1!
  NF_Const       = 1 << 1, // constant (value known at comptime, or immutable ref)
  NF_Base        = 1 << 2, // [struct field] the field is a base of the struct
  NF_RValue      = 1 << 3, // resolved as rvalue
  NF_Unused      = 1 << 4, // [Local] never referenced
  NF_Public      = 1 << 5, // [Local|Fun] public visibility (aka published, exported)
  NF_Named       = 1 << 6, // [Tuple when used as args] has named argument
  NF_PartialType = 1 << 7, // Type resolver should visit even if the node is typed
  NF_CustomInit  = 1 << 8, // struct has fields w/ non-zero initializer
  // Changing this? Remember to update NodeFlagsStr impl
} END_ENUM(NodeFlags)

#define NodeIsUnresolved(n) _NodeIsUnresolved(as_Node(n))
#define NodeSetUnresolved(n) _NodeSetUnresolved(as_Node(n))
#define NodeClearUnresolved(n) _NodeClearUnresolved(as_Node(n))
#define NodeTransferUnresolved(parent, child) \
  _NodeTransferUnresolved(as_Node(parent), as_Node(child))
#define NodeTransferUnresolved2(parent, c1, c2) \
  _NodeTransferUnresolved2(as_Node(parent), as_Node(c1), as_Node(c2))

#define NodeIsConst(n) _NodeIsConst(as_Node(n))
#define NodeSetConst(n) _NodeSetConst(as_Node(n))
#define NodeSetConstCond(n,on) _NodeSetConstCond(as_Node(n),(on))
#define NodeClearConst(n) _NodeClearConst(as_Node(n))
#define NodeTransferConst(parent, child) _NodeTransferConst(as_Node(parent), as_Node(child))
#define NodeTransferMut(parent, child) _NodeTransferMut(as_Node(parent), as_Node(child))
#define NodeTransferMut2(parent, c1, c2) \
  _NodeTransferMut2(as_Node(parent), as_Node(c1), as_Node(c2))

#define NodeIsUnused(n) _NodeIsUnused(as_Node(n))
#define NodeSetUnused(n) _NodeSetUnused(as_Node(n))
#define NodeClearUnused(n) _NodeClearUnused(as_Node(n))

#define NodeIsPublic(n) _NodeIsPublic(as_Node(n))
#define NodeSetPublic(n,on) _NodeSetPublic(as_Node(n),(on))

#define NodeIsNamed(n) _NodeIsNamed(as_Node(n))
#define NodeSetNamed(n,on) _NodeSetNamed(as_Node(n),(on))

#define NodeIsRValue(n) _NodeIsRValue(as_Node(n))
#define NodeSetRValue(n) _NodeSetRValue(as_Node(n))
#define NodeSetRValueCond(n,on) _NodeSetRValueCond(as_Node(n),(on))
#define NodeClearRValue(n) _NodeClearRValue(as_Node(n))

#define NodeTransferCustomInit(parent, child) \
  _NodeTransferCustomInit(as_Node(parent), as_Node(child))

#define NodeTransferPartialType2(parent, c1, c2) \
  _NodeTransferPartialType2(as_Node(parent), as_Node(c1), as_Node(c2))

// Node{Is,Set,Clear}Unresolved controls the "unresolved" flag of a node
inline static bool _NodeIsUnresolved(const Node* n) { return (n->flags & NF_Unresolved) != 0; }
inline static void _NodeSetUnresolved(Node* n) { n->flags |= NF_Unresolved; }
inline static void _NodeClearUnresolved(Node* n) { n->flags &= ~NF_Unresolved; }
inline static void _NodeTransferUnresolved(Node* parent, Node* child) {
  parent->flags |= child->flags & NF_Unresolved;
}
// nodeTransferUnresolved2 is like NodeTransferUnresolved but takes two inputs which flags
// are combined as a set union.
inline static void _NodeTransferUnresolved2(Node* parent, Node* child1, Node* child2) {
  // parent is unresolved if child1 OR child2 is unresolved
  parent->flags |= (child1->flags & NF_Unresolved) | (child2->flags & NF_Unresolved);
}

// Node{Is,Set,Clear}Const controls the "constant value" flag of a node
inline static bool _NodeIsConst(const Node* n) { return (n->flags & NF_Const) != 0; }
inline static void _NodeSetConstCond(Node* n, bool on) { SET_FLAG(n->flags, NF_Const, on); }
inline static void _NodeSetConst(Node* n) { n->flags |= NF_Const; }
inline static void _NodeClearConst(Node* n) { n->flags &= ~NF_Const; }
inline static void _NodeTransferConst(Node* parent, Node* child) {
  // parent is mutable if n OR child is NOT const, else parent is marked const.
  parent->flags |= child->flags & NF_Const;
}
inline static void _NodeTransferMut(Node* parent, Node* child) {
  // parent is const if n AND child is const, else parent is marked mutable.
  parent->flags = (parent->flags & ~NF_Const) | (
    (parent->flags & NF_Const) &
    (child->flags & NF_Const)
  );
}
inline static void _NodeTransferMut2(Node* parent, Node* child1, Node* child2) {
  // parent is const if n AND child1 AND child2 is const, else parent is marked mutable.
  parent->flags = (parent->flags & ~NF_Const) | (
    (parent->flags & NF_Const) &
    (child1->flags & NF_Const) &
    (child2->flags & NF_Const)
  );
}

// Node{Is,Set,Clear}Unused controls the NF_Unused flag of a node
inline static bool _NodeIsUnused(const Node* n) { return (n->flags & NF_Unused) != 0; }
inline static void _NodeSetUnused(Node* n) { n->flags |= NF_Unused; }
inline static void _NodeClearUnused(Node* n) { n->flags &= ~NF_Unused; }

// Node{Is,Set}Public controls the NF_Public flag of a node
inline static bool _NodeIsPublic(const Node* n) { return (n->flags & NF_Public) != 0; }
inline static void _NodeSetPublic(Node* n, bool on) { SET_FLAG(n->flags, NF_Public, on); }

// Node{Is,Set}Named controls the NF_Named flag of a node
inline static bool _NodeIsNamed(const Node* n) { return (n->flags & NF_Named) != 0; }
inline static void _NodeSetNamed(Node* n, bool on) { SET_FLAG(n->flags, NF_Named, on); }

// Node{Is,Set,Clear}RValue controls the NF_RValue flag of a node
inline static bool _NodeIsRValue(const Node* n) { return (n->flags & NF_RValue) != 0; }
inline static void _NodeSetRValue(Node* n) { n->flags |= NF_RValue; }
inline static void _NodeSetRValueCond(Node* n, bool on) {SET_FLAG(n->flags,NF_RValue,on);}
inline static void _NodeClearRValue(Node* n) { n->flags &= ~NF_RValue; }

inline static void _NodeTransferCustomInit(Node* parent, Node* child) {
  parent->flags |= child->flags & NF_CustomInit;
}
inline static void _NodeTransferPartialType2(Node* parent, Node* c1, Node* c2) {
  parent->flags |= (c1->flags & NF_PartialType) |
                   (c2->flags & NF_PartialType);
}

// include code generated by ast_gen.py from the above structs
END_INTERFACE
#include "ast_gen.h"
BEGIN_INTERFACE

// keep the size of nodes in check. Update this if needed.
static_assert(sizeof(union NodeUnion) >= 104, "AST size shrunk");
static_assert(sizeof(union NodeUnion) <= 104, "AST size grew");

// all subtypes of LocalNode must have compatible init fields

#define LocalInitField(n)      ( ((VarNode*)as_LocalNode((Node*)n))->init )
#define SetLocalInitField(n,v) ( ((VarNode*)as_LocalNode((Node*)n))->init = (v) )
static_assert(offsetof(ConstNode,value) == offsetof(VarNode,init), "");
static_assert(offsetof(ConstNode,value) == offsetof(ParamNode,init), "");
static_assert(offsetof(ConstNode,value) == offsetof(MacroParamNode,init), "");
static_assert(sizeof(((ConstNode*)0)->value) == sizeof(((VarNode*)0)->init), "");
static_assert(sizeof(((ConstNode*)0)->value) == sizeof(((ParamNode*)0)->init), "");
static_assert(sizeof(((ConstNode*)0)->value) == sizeof(((MacroParamNode*)0)->init), "");

// NodeKindName returns a printable name. E.g. NBad => "Bad"
const char* NodeKindName(NodeKind);

inline static bool NodeIsPrimitiveConst(const Node* n) {
  // used by resolve_id
  return n->kind == NNil || n->kind == NBasicType || n->kind == NBoolLit;
}

inline static Node* nullable NodeAlloc(Mem mem) {
  return (Node*)mem_alloc(mem, sizeof(union NodeUnion));
}

Node* NodeInit(Node* n, NodeKind kind);

#define NodePosSpan(n) _NodePosSpan(as_Node(n))
PosSpan _NodePosSpan(const Node* n);

// returns NULL if copying an array caused memory allocation to fail
Node* nullable NodeCopy(Node* dst, const Node* src);

// NodeRefLocal increments the reference counter of a Local node.
// Returns n as a convenience.
inline static LocalNode* NodeRefLocal(LocalNode* n) {
  n->nrefs++;
  return n;
}
// NodeUnrefLocal decrements the reference counter of a Local node.
// Returns the value of n->var.nrefs after the subtraction.
inline static u32 NodeUnrefLocal(LocalNode* n) {
  assertgt(n->nrefs, 0);
  return --n->nrefs;
}

// --------------------------------------------------------------------------------------
// repr

// NodeFmtFlag changes behavior of NodeRepr
typedef u32 NodeFmtFlag;
enum NodeFmtFlag {
  NODE_FMT_NOCOLOR  = 1 << 0, // disable ANSI terminal styling
  NODE_FMT_COLOR    = 1 << 1, // enable ANSI terminal styling (even if stderr is not a TTY)
  NODE_FMT_TYPES    = 1 << 2, // include types in the output
  NODE_FMT_USECOUNT = 1 << 3, // include information about uses (ie for Local)
  NODE_FMT_REFS     = 1 << 4, // include "#N" reference indicators
  NODE_FMT_ATTRS    = 1 << 5, // include "@attr" attributes
} END_ENUM(NodeFmtFlag)

// nodename returns a node's type name. E.g. "Tuple"
// const char* nodename(const Node* n)
#define nodename(n) NodeKindName(as_Node(assertnotnull(n))->kind)

// fmtnode returns a short representation of n to buf. Returns buf.
#define fmtnode(n, buf, bufcap) _fmtnode(as_Node(n),(buf),(bufcap))
char* _fmtnode(const Node* nullable n, char* buf, usize bufcap);

// fmtast writes an exhaustive representation of n to buf.
// It writes at most bufcap-1 of the characters to the output buf (the bufcap'th
// character then gets the terminating '\0'). If the return value is greater than or
// equal to the bufcap argument, buf was too short and some of the characters were
// discarded. The output is always null-terminated, unless size is 0.
// Returns the number of characters that would have been printed if bufcap was
// unlimited (not including the final `\0').
#define fmtast(n, str, fl) _fmtast(as_Node(n),(str),(fl))
bool _fmtast(const Node* nullable n, Str* dst, NodeFmtFlag fl);

// --------------------------------------------------------------------------------------

Scope* nullable ScopeNew(Mem mem, const Scope* nullable parent);
bool ScopeInit(Scope*, Mem mem, const Scope* nullable parent);
void ScopeFree(Scope*, Mem mem);
Node* nullable ScopeLookup(const Scope* nullable, Sym);

// ScopeAssign associates key with *valuep_inout.
// Returns an error if memory allocation failed during growth of the hash table.
error ScopeAssign(Scope* s, Sym key, Node* n, Mem) WARN_UNUSED_RESULT;

// // ScopeAssoc associates key with *valuep_inout.
// // On return, sets *valuep_inout to a replaced node or NULL if no existing node was found.
// // Returns an error if memory allocation failed during growth of the hash table.
// WARN_UNUSED_RESULT
// inline static error ScopeAssoc(Scope* s, Sym key, Node** valuep_inout) {
//   return SymMapSet(&s->bindings, key, (void**)valuep_inout);
// }


// --------------------------------------------------------------------------------------
// node switch

#define NCASE(NAME)  break; } case N##NAME: { \
  UNUSED auto n = (NAME##Node*)np;
#define GNCASE(NAME) break; } case N##NAME##_BEG ... N##NAME##_END: { \
  UNUSED auto n = (struct NAME##Node*)np;
#define NDEFAULTCASE break; } default: { \
  UNUSED auto n = np;

/*
switch template:

static Node* example(Node* np) {
  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Field)      panic("TODO %s", nodename(n));
  NCASE(Pkg)        panic("TODO %s", nodename(n));
  NCASE(File)       panic("TODO %s", nodename(n));
  NCASE(Comment)    panic("TODO %s", nodename(n));

  NCASE(Nil)        panic("TODO %s", nodename(n));
  NCASE(BoolLit)    panic("TODO %s", nodename(n));
  NCASE(IntLit)     panic("TODO %s", nodename(n));
  NCASE(FloatLit)   panic("TODO %s", nodename(n));
  NCASE(StrLit)     panic("TODO %s", nodename(n));
  NCASE(Id)         panic("TODO %s", nodename(n));
  NCASE(BinOp)      panic("TODO %s", nodename(n));
  NCASE(PrefixOp)   panic("TODO %s", nodename(n));
  NCASE(PostfixOp)  panic("TODO %s", nodename(n));
  NCASE(Return)     panic("TODO %s", nodename(n));
  NCASE(Assign)     panic("TODO %s", nodename(n));
  NCASE(Tuple)      panic("TODO %s", nodename(n));
  NCASE(Array)      panic("TODO %s", nodename(n));
  NCASE(Block)      panic("TODO %s", nodename(n));
  NCASE(Fun)        panic("TODO %s", nodename(n));
  NCASE(Macro)      panic("TODO %s", nodename(n));
  NCASE(Call)       panic("TODO %s", nodename(n));
  NCASE(TypeCast)   panic("TODO %s", nodename(n));
  NCASE(Const)      panic("TODO %s", nodename(n));
  NCASE(Var)        panic("TODO %s", nodename(n));
  NCASE(Param)      panic("TODO %s", nodename(n));
  NCASE(MacroParam) panic("TODO %s", nodename(n));
  NCASE(Ref)        panic("TODO %s", nodename(n));
  NCASE(NamedArg)   panic("TODO %s", nodename(n));
  NCASE(Selector)   panic("TODO %s", nodename(n));
  NCASE(Index)      panic("TODO %s", nodename(n));
  NCASE(Slice)      panic("TODO %s", nodename(n));
  NCASE(If)         panic("TODO %s", nodename(n));

  NCASE(TypeType)   panic("TODO %s", nodename(n));
  NCASE(NamedType)  panic("TODO %s", nodename(n));
  NCASE(AliasType)  panic("TODO %s", nodename(n));
  NCASE(RefType)    panic("TODO %s", nodename(n));
  NCASE(BasicType)  panic("TODO %s", nodename(n));
  NCASE(ArrayType)  panic("TODO %s", nodename(n));
  NCASE(TupleType)  panic("TODO %s", nodename(n));
  NCASE(StructType) panic("TODO %s", nodename(n));
  NCASE(FunType)    panic("TODO %s", nodename(n));

  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  UNREACHABLE;
}
*/


END_INTERFACE
