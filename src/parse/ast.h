// AST nodes
#pragma once
#include "../array.h"
#include "../map.h"
#include "../sym.h"
#include "token.h"
#include "type.h"
#include "pos.h"
ASSUME_NONNULL_BEGIN

typedef struct Scope     Scope;     // lexical namespace (may be chained)
typedef struct Node      Node;      // AST node, basis for Stmt, Expr and Type
typedef struct Stmt      Stmt;      // AST statement
typedef struct Expr      Expr;      // AST expression
typedef struct LitExpr   LitExpr;   // AST constant literal expression
typedef struct Type      Type;      // AST type
typedef struct FieldNode FieldNode; // AST struct field
typedef struct TupleNode TupleNode;

typedef u8  NodeKind;  // AST node kind (NNone, NBad, NBoolLit ...)
typedef u16 NodeFlags; // NF_* constants; AST node flags (Unresolved, Const ...)

// forward decl of things defined in universe but referenced by ast.h
extern Type* kType_type;

DEF_TYPED_PTR_ARRAY(NodeArray, Node*)
DEF_TYPED_PTR_ARRAY(ExprArray, Expr*)
DEF_TYPED_PTR_ARRAY(TypeArray, Type*)
DEF_TYPED_PTR_ARRAY(FieldArray, FieldNode*)
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
struct PkgNode { struct CUnitNode; };
struct FileNode { struct CUnitNode; };
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
  Sym   name;
  Node* target; // TODO: change type to Expr
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
struct PrefixOpNode { struct UnaryOpNode; };
struct PostfixOpNode { struct UnaryOpNode; };
struct ReturnNode { Expr;
  Expr* expr;
};
struct AssignNode { Expr;
  Expr* dst; // assignment target (Var | Tuple | Index)
  Expr* val; // value
};
struct ListExpr { Expr;
  ExprArray a;            // array of nodes
  Expr*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct TupleNode { struct ListExpr; };
struct ArrayNode { struct ListExpr; };
struct BlockNode { Expr;
  NodeArray a;            // array of nodes
  Node*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct FunNode { Expr;
  TupleNode* nullable params; // input params (NULL if none)
  Type* nullable      result; // output results (TupleType for multiple results)
  Sym   nullable      name;   // NULL for lambda
  Expr* nullable      body;   // NULL for fun-declaration
};
struct MacroNode { Expr;
  Sym nullable        name;
  TupleNode* nullable params;  // input params (VarNodes)
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
struct VarNode { Expr;
  bool           isconst; // immutable storage? (true for "const x" vars) TODO: use NF_*
  u32            nrefs;   // reference count
  u32            index;   // argument index (used by function parameters)
  Sym            name;
  Expr* nullable init;    // initial/default value
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
  TupleNode* nullable params; // NTuple (of NVar) or null if no params
  Type* nullable      result; // NTupleType of types or single type
};


struct Scope {
  const Scope* parent;
  // bindings must be the last member as composing structs places initial storage after
  // SymMap bindings;
  HMap bindings;
};
static_assert(offsetof(Scope,bindings) == sizeof(Scope)-sizeof(((Scope*)0)->bindings),
  "bindings is not last member of Scope");


enum NodeFlags {
  NF_Unresolved  = 1 <<  0, // contains unresolved references. MUST BE VALUE 1!
  NF_Const       = 1 <<  1, // constant; value known at compile time (comptime)
  NF_Base        = 1 <<  2, // [struct field] the field is a base of the struct
  NF_RValue      = 1 <<  3, // resolved as rvalue
  NF_Param       = 1 <<  4, // [Var] function parameter
  NF_MacroParam  = 1 <<  5, // [Var] macro parameter
  NF_Unused      = 1 <<  6, // [Var] never referenced
  NF_Public      = 1 <<  7, // [Var|Fun] public visibility (aka published, exported)
  NF_Named       = 1 <<  8, // [Tuple when used as args] has named argument
  NF_PartialType = 1 <<  9, // Type resolver should visit even if the node is typed
  NF_CustomInit  = 1 << 10, // struct has fields w/ non-zero initializer
  // Changing this? Remember to update NodeFlagsStr impl
} END_TYPED_ENUM(NodeFlags)

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
#define NodeIsParam(n) _NodeIsParam(as_Node(n))
#define NodeSetParam(n) _NodeSetParam(as_Node(n))
#define NodeClearParam(n) _NodeClearParam(as_Node(n))
#define NodeIsMacroParam(n) _NodeIsMacroParam(as_Node(n))
#define NodeSetMacroParam(n) _NodeSetMacroParam(as_Node(n))
#define NodeClearMacroParam(n) _NodeClearMacroParam(as_Node(n))
#define NodeIsUnused(n) _NodeIsUnused(as_Node(n))
#define NodeSetUnused(n) _NodeSetUnused(as_Node(n))
#define NodeClearUnused(n) _NodeClearUnused(as_Node(n))
#define NodeIsPublic(n) _NodeIsPublic(as_Node(n))
#define NodeSetPublic(n,on) _NodeSetPublic(as_Node(n),(on))
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

// Node{Is,Set,Clear}Param controls the "is function parameter" flag of a node
inline static bool _NodeIsParam(const Node* n) { return (n->flags & NF_Param) != 0; }
inline static void _NodeSetParam(Node* n) { n->flags |= NF_Param; }
inline static void _NodeClearParam(Node* n) { n->flags &= ~NF_Param; }

// Node{Is,Set,Clear}MacroParam controls the "is function parameter" flag of a node
inline static bool _NodeIsMacroParam(const Node* n) { return (n->flags & NF_MacroParam) != 0; }
inline static void _NodeSetMacroParam(Node* n) { n->flags |= NF_MacroParam; }
inline static void _NodeClearMacroParam(Node* n) { n->flags &= ~NF_MacroParam; }

// Node{Is,Set,Clear}Unused controls the "is unused" flag of a node
inline static bool _NodeIsUnused(const Node* n) { return (n->flags & NF_Unused) != 0; }
inline static void _NodeSetUnused(Node* n) { n->flags |= NF_Unused; }
inline static void _NodeClearUnused(Node* n) { n->flags &= ~NF_Unused; }

// Node{Is,Set,Clear}Public controls the "is public" flag of a node
inline static bool _NodeIsPublic(const Node* n) { return (n->flags & NF_Public) != 0; }
inline static void _NodeSetPublic(Node* n, bool on) { SET_FLAG(n->flags, NF_Public, on); }

// Node{Is,Set,Clear}RValue controls the "is rvalue" flag of a node
inline static bool _NodeIsRValue(const Node* n) { return (n->flags & NF_RValue) != 0; }
inline static void _NodeSetRValue(Node* n) { n->flags |= NF_RValue; }
inline static void _NodeSetRValueCond(Node* n, bool on) { SET_FLAG(n->flags, NF_RValue, on); }
inline static void _NodeClearRValue(Node* n) { n->flags &= ~NF_RValue; }

inline static void _NodeTransferCustomInit(Node* parent, Node* child) {
  parent->flags |= child->flags & NF_CustomInit;
}
inline static void _NodeTransferPartialType2(Node* parent, Node* c1, Node* c2) {
  parent->flags |= (c1->flags & NF_PartialType) |
                   (c2->flags & NF_PartialType);
}


//BEGIN GENERATED CODE by ast_gen.py

enum NodeKind {
  NBad            =  0, // struct BadNode
  NField          =  1, // struct FieldNode
  NStmt_BEG       =  2,
    NCUnit_BEG    =  2,
      NPkg        =  2, // struct PkgNode
      NFile       =  3, // struct FileNode
    NCUnit_END    =  3,
    NComment      =  4, // struct CommentNode
  NStmt_END       =  4,
  NExpr_BEG       =  5,
    NLitExpr_BEG  =  5,
      NNil        =  5, // struct NilNode
      NBoolLit    =  6, // struct BoolLitNode
      NIntLit     =  7, // struct IntLitNode
      NFloatLit   =  8, // struct FloatLitNode
      NStrLit     =  9, // struct StrLitNode
    NLitExpr_END  =  9,
    NId           = 10, // struct IdNode
    NBinOp        = 11, // struct BinOpNode
    NUnaryOp_BEG  = 12,
      NPrefixOp   = 12, // struct PrefixOpNode
      NPostfixOp  = 13, // struct PostfixOpNode
    NUnaryOp_END  = 13,
    NReturn       = 14, // struct ReturnNode
    NAssign       = 15, // struct AssignNode
    NListExpr_BEG = 16,
      NTuple      = 16, // struct TupleNode
      NArray      = 17, // struct ArrayNode
    NListExpr_END = 17,
    NBlock        = 18, // struct BlockNode
    NFun          = 19, // struct FunNode
    NMacro        = 20, // struct MacroNode
    NCall         = 21, // struct CallNode
    NTypeCast     = 22, // struct TypeCastNode
    NVar          = 23, // struct VarNode
    NRef          = 24, // struct RefNode
    NNamedArg     = 25, // struct NamedArgNode
    NSelector     = 26, // struct SelectorNode
    NIndex        = 27, // struct IndexNode
    NSlice        = 28, // struct SliceNode
    NIf           = 29, // struct IfNode
  NExpr_END       = 29,
  NType_BEG       = 30,
    NTypeType     = 30, // struct TypeTypeNode
    NNamedType    = 31, // struct NamedTypeNode
    NAliasType    = 32, // struct AliasTypeNode
    NRefType      = 33, // struct RefTypeNode
    NBasicType    = 34, // struct BasicTypeNode
    NArrayType    = 35, // struct ArrayTypeNode
    NTupleType    = 36, // struct TupleTypeNode
    NStructType   = 37, // struct StructTypeNode
    NFunType      = 38, // struct FunTypeNode
  NType_END       = 38,
  NodeKind_MAX    = 38,
} END_TYPED_ENUM(NodeKind)

// NodeKindName returns a printable name. E.g. NBad => "Bad"
const char* NodeKindName(NodeKind);

typedef struct BadNode BadNode;
typedef struct FieldNode FieldNode;
typedef struct PkgNode PkgNode;
typedef struct FileNode FileNode;
typedef struct CommentNode CommentNode;
typedef struct NilNode NilNode;
typedef struct BoolLitNode BoolLitNode;
typedef struct IntLitNode IntLitNode;
typedef struct FloatLitNode FloatLitNode;
typedef struct StrLitNode StrLitNode;
typedef struct IdNode IdNode;
typedef struct BinOpNode BinOpNode;
typedef struct PrefixOpNode PrefixOpNode;
typedef struct PostfixOpNode PostfixOpNode;
typedef struct ReturnNode ReturnNode;
typedef struct AssignNode AssignNode;
typedef struct TupleNode TupleNode;
typedef struct ArrayNode ArrayNode;
typedef struct BlockNode BlockNode;
typedef struct FunNode FunNode;
typedef struct MacroNode MacroNode;
typedef struct CallNode CallNode;
typedef struct TypeCastNode TypeCastNode;
typedef struct VarNode VarNode;
typedef struct RefNode RefNode;
typedef struct NamedArgNode NamedArgNode;
typedef struct SelectorNode SelectorNode;
typedef struct IndexNode IndexNode;
typedef struct SliceNode SliceNode;
typedef struct IfNode IfNode;
typedef struct TypeTypeNode TypeTypeNode;
typedef struct NamedTypeNode NamedTypeNode;
typedef struct AliasTypeNode AliasTypeNode;
typedef struct RefTypeNode RefTypeNode;
typedef struct BasicTypeNode BasicTypeNode;
typedef struct ArrayTypeNode ArrayTypeNode;
typedef struct TupleTypeNode TupleTypeNode;
typedef struct StructTypeNode StructTypeNode;
typedef struct FunTypeNode FunTypeNode;

// bool NodeKindIs<kind>(NodeKind)
#define NodeKindIsStmt(k) (NStmt_BEG <= (k) && (k) <= NStmt_END)
#define NodeKindIsCUnit(k) (NCUnit_BEG <= (k) && (k) <= NCUnit_END)
#define NodeKindIsExpr(k) (NExpr_BEG <= (k) && (k) <= NExpr_END)
#define NodeKindIsLitExpr(k) (NLitExpr_BEG <= (k) && (k) <= NLitExpr_END)
#define NodeKindIsUnaryOp(k) (NUnaryOp_BEG <= (k) && (k) <= NUnaryOp_END)
#define NodeKindIsListExpr(k) (NListExpr_BEG <= (k) && (k) <= NListExpr_END)
#define NodeKindIsType(k) (NType_BEG <= (k) && (k) <= NType_END)

// bool is_<kind>(const Node*)
#define is_BadNode(n) ((n)->kind==NBad)
#define is_FieldNode(n) ((n)->kind==NField)
#define is_Stmt(n) NodeKindIsStmt((n)->kind)
#define is_CUnitNode(n) NodeKindIsCUnit((n)->kind)
#define is_PkgNode(n) ((n)->kind==NPkg)
#define is_FileNode(n) ((n)->kind==NFile)
#define is_CommentNode(n) ((n)->kind==NComment)
#define is_Expr(n) NodeKindIsExpr((n)->kind)
#define is_LitExpr(n) NodeKindIsLitExpr((n)->kind)
#define is_NilNode(n) ((n)->kind==NNil)
#define is_BoolLitNode(n) ((n)->kind==NBoolLit)
#define is_IntLitNode(n) ((n)->kind==NIntLit)
#define is_FloatLitNode(n) ((n)->kind==NFloatLit)
#define is_StrLitNode(n) ((n)->kind==NStrLit)
#define is_IdNode(n) ((n)->kind==NId)
#define is_BinOpNode(n) ((n)->kind==NBinOp)
#define is_UnaryOpNode(n) NodeKindIsUnaryOp((n)->kind)
#define is_PrefixOpNode(n) ((n)->kind==NPrefixOp)
#define is_PostfixOpNode(n) ((n)->kind==NPostfixOp)
#define is_ReturnNode(n) ((n)->kind==NReturn)
#define is_AssignNode(n) ((n)->kind==NAssign)
#define is_ListExpr(n) NodeKindIsListExpr((n)->kind)
#define is_TupleNode(n) ((n)->kind==NTuple)
#define is_ArrayNode(n) ((n)->kind==NArray)
#define is_BlockNode(n) ((n)->kind==NBlock)
#define is_FunNode(n) ((n)->kind==NFun)
#define is_MacroNode(n) ((n)->kind==NMacro)
#define is_CallNode(n) ((n)->kind==NCall)
#define is_TypeCastNode(n) ((n)->kind==NTypeCast)
#define is_VarNode(n) ((n)->kind==NVar)
#define is_RefNode(n) ((n)->kind==NRef)
#define is_NamedArgNode(n) ((n)->kind==NNamedArg)
#define is_SelectorNode(n) ((n)->kind==NSelector)
#define is_IndexNode(n) ((n)->kind==NIndex)
#define is_SliceNode(n) ((n)->kind==NSlice)
#define is_IfNode(n) ((n)->kind==NIf)
#define is_Type(n) NodeKindIsType((n)->kind)
#define is_TypeTypeNode(n) ((n)->kind==NTypeType)
#define is_NamedTypeNode(n) ((n)->kind==NNamedType)
#define is_AliasTypeNode(n) ((n)->kind==NAliasType)
#define is_RefTypeNode(n) ((n)->kind==NRefType)
#define is_BasicTypeNode(n) ((n)->kind==NBasicType)
#define is_ArrayTypeNode(n) ((n)->kind==NArrayType)
#define is_TupleTypeNode(n) ((n)->kind==NTupleType)
#define is_StructTypeNode(n) ((n)->kind==NStructType)
#define is_FunTypeNode(n) ((n)->kind==NFunType)

// void assert_is_<kind>(const Node*)
#ifdef DEBUG
#define _assert_is1(NAME,n) ({ \
  NodeKind nk__ = assertnotnull(n)->kind; \
  assertf(NodeKindIs##NAME(nk__), "expected N%s; got N%s #%d", \
          #NAME, NodeKindName(nk__), nk__); \
})
#else
#define _assert_is1(NAME,n) ((void)0)
#endif
#define assert_is_BadNode(n) asserteq(assertnotnull(n)->kind,NBad)
#define assert_is_FieldNode(n) asserteq(assertnotnull(n)->kind,NField)
#define assert_is_Stmt(n) _assert_is1(Stmt,(n))
#define assert_is_CUnitNode(n) _assert_is1(CUnit,(n))
#define assert_is_PkgNode(n) asserteq(assertnotnull(n)->kind,NPkg)
#define assert_is_FileNode(n) asserteq(assertnotnull(n)->kind,NFile)
#define assert_is_CommentNode(n) asserteq(assertnotnull(n)->kind,NComment)
#define assert_is_Expr(n) _assert_is1(Expr,(n))
#define assert_is_LitExpr(n) _assert_is1(LitExpr,(n))
#define assert_is_NilNode(n) asserteq(assertnotnull(n)->kind,NNil)
#define assert_is_BoolLitNode(n) asserteq(assertnotnull(n)->kind,NBoolLit)
#define assert_is_IntLitNode(n) asserteq(assertnotnull(n)->kind,NIntLit)
#define assert_is_FloatLitNode(n) asserteq(assertnotnull(n)->kind,NFloatLit)
#define assert_is_StrLitNode(n) asserteq(assertnotnull(n)->kind,NStrLit)
#define assert_is_IdNode(n) asserteq(assertnotnull(n)->kind,NId)
#define assert_is_BinOpNode(n) asserteq(assertnotnull(n)->kind,NBinOp)
#define assert_is_UnaryOpNode(n) _assert_is1(UnaryOp,(n))
#define assert_is_PrefixOpNode(n) asserteq(assertnotnull(n)->kind,NPrefixOp)
#define assert_is_PostfixOpNode(n) asserteq(assertnotnull(n)->kind,NPostfixOp)
#define assert_is_ReturnNode(n) asserteq(assertnotnull(n)->kind,NReturn)
#define assert_is_AssignNode(n) asserteq(assertnotnull(n)->kind,NAssign)
#define assert_is_ListExpr(n) _assert_is1(ListExpr,(n))
#define assert_is_TupleNode(n) asserteq(assertnotnull(n)->kind,NTuple)
#define assert_is_ArrayNode(n) asserteq(assertnotnull(n)->kind,NArray)
#define assert_is_BlockNode(n) asserteq(assertnotnull(n)->kind,NBlock)
#define assert_is_FunNode(n) asserteq(assertnotnull(n)->kind,NFun)
#define assert_is_MacroNode(n) asserteq(assertnotnull(n)->kind,NMacro)
#define assert_is_CallNode(n) asserteq(assertnotnull(n)->kind,NCall)
#define assert_is_TypeCastNode(n) asserteq(assertnotnull(n)->kind,NTypeCast)
#define assert_is_VarNode(n) asserteq(assertnotnull(n)->kind,NVar)
#define assert_is_RefNode(n) asserteq(assertnotnull(n)->kind,NRef)
#define assert_is_NamedArgNode(n) asserteq(assertnotnull(n)->kind,NNamedArg)
#define assert_is_SelectorNode(n) asserteq(assertnotnull(n)->kind,NSelector)
#define assert_is_IndexNode(n) asserteq(assertnotnull(n)->kind,NIndex)
#define assert_is_SliceNode(n) asserteq(assertnotnull(n)->kind,NSlice)
#define assert_is_IfNode(n) asserteq(assertnotnull(n)->kind,NIf)
#define assert_is_Type(n) _assert_is1(Type,(n))
#define assert_is_TypeTypeNode(n) asserteq(assertnotnull(n)->kind,NTypeType)
#define assert_is_NamedTypeNode(n) asserteq(assertnotnull(n)->kind,NNamedType)
#define assert_is_AliasTypeNode(n) asserteq(assertnotnull(n)->kind,NAliasType)
#define assert_is_RefTypeNode(n) asserteq(assertnotnull(n)->kind,NRefType)
#define assert_is_BasicTypeNode(n) asserteq(assertnotnull(n)->kind,NBasicType)
#define assert_is_ArrayTypeNode(n) asserteq(assertnotnull(n)->kind,NArrayType)
#define assert_is_TupleTypeNode(n) asserteq(assertnotnull(n)->kind,NTupleType)
#define assert_is_StructTypeNode(n) asserteq(assertnotnull(n)->kind,NStructType)
#define assert_is_FunTypeNode(n) asserteq(assertnotnull(n)->kind,NFunType)

// <type>* as_<type>(Node* n)
// const <type>* as_<type>(const Node* n)
#define as_BadNode(n) ({ assert_is_BadNode(n); (BadNode*)(n); })
#define as_FieldNode(n) ({ assert_is_FieldNode(n); (FieldNode*)(n); })
#define as_PkgNode(n) ({ assert_is_PkgNode(n); (PkgNode*)(n); })
#define as_FileNode(n) ({ assert_is_FileNode(n); (FileNode*)(n); })
#define as_CommentNode(n) ({ assert_is_CommentNode(n); (CommentNode*)(n); })
#define as_NilNode(n) ({ assert_is_NilNode(n); (NilNode*)(n); })
#define as_BoolLitNode(n) ({ assert_is_BoolLitNode(n); (BoolLitNode*)(n); })
#define as_IntLitNode(n) ({ assert_is_IntLitNode(n); (IntLitNode*)(n); })
#define as_FloatLitNode(n) ({ assert_is_FloatLitNode(n); (FloatLitNode*)(n); })
#define as_StrLitNode(n) ({ assert_is_StrLitNode(n); (StrLitNode*)(n); })
#define as_IdNode(n) ({ assert_is_IdNode(n); (IdNode*)(n); })
#define as_BinOpNode(n) ({ assert_is_BinOpNode(n); (BinOpNode*)(n); })
#define as_PrefixOpNode(n) ({ assert_is_PrefixOpNode(n); (PrefixOpNode*)(n); })
#define as_PostfixOpNode(n) ({ assert_is_PostfixOpNode(n); (PostfixOpNode*)(n); })
#define as_ReturnNode(n) ({ assert_is_ReturnNode(n); (ReturnNode*)(n); })
#define as_AssignNode(n) ({ assert_is_AssignNode(n); (AssignNode*)(n); })
#define as_TupleNode(n) ({ assert_is_TupleNode(n); (TupleNode*)(n); })
#define as_ArrayNode(n) ({ assert_is_ArrayNode(n); (ArrayNode*)(n); })
#define as_BlockNode(n) ({ assert_is_BlockNode(n); (BlockNode*)(n); })
#define as_FunNode(n) ({ assert_is_FunNode(n); (FunNode*)(n); })
#define as_MacroNode(n) ({ assert_is_MacroNode(n); (MacroNode*)(n); })
#define as_CallNode(n) ({ assert_is_CallNode(n); (CallNode*)(n); })
#define as_TypeCastNode(n) ({ assert_is_TypeCastNode(n); (TypeCastNode*)(n); })
#define as_VarNode(n) ({ assert_is_VarNode(n); (VarNode*)(n); })
#define as_RefNode(n) ({ assert_is_RefNode(n); (RefNode*)(n); })
#define as_NamedArgNode(n) ({ assert_is_NamedArgNode(n); (NamedArgNode*)(n); })
#define as_SelectorNode(n) ({ assert_is_SelectorNode(n); (SelectorNode*)(n); })
#define as_IndexNode(n) ({ assert_is_IndexNode(n); (IndexNode*)(n); })
#define as_SliceNode(n) ({ assert_is_SliceNode(n); (SliceNode*)(n); })
#define as_IfNode(n) ({ assert_is_IfNode(n); (IfNode*)(n); })
#define as_TypeTypeNode(n) ({ assert_is_TypeTypeNode(n); (TypeTypeNode*)(n); })
#define as_NamedTypeNode(n) ({ assert_is_NamedTypeNode(n); (NamedTypeNode*)(n); })
#define as_AliasTypeNode(n) ({ assert_is_AliasTypeNode(n); (AliasTypeNode*)(n); })
#define as_RefTypeNode(n) ({ assert_is_RefTypeNode(n); (RefTypeNode*)(n); })
#define as_BasicTypeNode(n) ({ assert_is_BasicTypeNode(n); (BasicTypeNode*)(n); })
#define as_ArrayTypeNode(n) ({ assert_is_ArrayTypeNode(n); (ArrayTypeNode*)(n); })
#define as_TupleTypeNode(n) ({ assert_is_TupleTypeNode(n); (TupleTypeNode*)(n); })
#define as_StructTypeNode(n) ({ assert_is_StructTypeNode(n); (StructTypeNode*)(n); })
#define as_FunTypeNode(n) ({ assert_is_FunTypeNode(n); (FunTypeNode*)(n); })
#define as_Node(n) _Generic((n), const BadNode*:(const Node*)(n), BadNode*:(Node*)(n), \
  const FieldNode*:(const Node*)(n), FieldNode*:(Node*)(n), \
  const PkgNode*:(const Node*)(n), PkgNode*:(Node*)(n), \
  const FileNode*:(const Node*)(n), FileNode*:(Node*)(n), \
  const struct CUnitNode*:(const Node*)(n), struct CUnitNode*:(Node*)(n), \
  const CommentNode*:(const Node*)(n), CommentNode*:(Node*)(n), \
  const Stmt*:(const Node*)(n), Stmt*:(Node*)(n), const NilNode*:(const Node*)(n), \
  NilNode*:(Node*)(n), const BoolLitNode*:(const Node*)(n), BoolLitNode*:(Node*)(n), \
  const IntLitNode*:(const Node*)(n), IntLitNode*:(Node*)(n), \
  const FloatLitNode*:(const Node*)(n), FloatLitNode*:(Node*)(n), \
  const StrLitNode*:(const Node*)(n), StrLitNode*:(Node*)(n), \
  const struct LitExpr*:(const Node*)(n), struct LitExpr*:(Node*)(n), \
  const IdNode*:(const Node*)(n), IdNode*:(Node*)(n), const BinOpNode*:(const Node*)(n), \
  BinOpNode*:(Node*)(n), const PrefixOpNode*:(const Node*)(n), PrefixOpNode*:(Node*)(n), \
  const PostfixOpNode*:(const Node*)(n), PostfixOpNode*:(Node*)(n), \
  const struct UnaryOpNode*:(const Node*)(n), struct UnaryOpNode*:(Node*)(n), \
  const ReturnNode*:(const Node*)(n), ReturnNode*:(Node*)(n), \
  const AssignNode*:(const Node*)(n), AssignNode*:(Node*)(n), \
  const TupleNode*:(const Node*)(n), TupleNode*:(Node*)(n), \
  const ArrayNode*:(const Node*)(n), ArrayNode*:(Node*)(n), \
  const struct ListExpr*:(const Node*)(n), struct ListExpr*:(Node*)(n), \
  const BlockNode*:(const Node*)(n), BlockNode*:(Node*)(n), \
  const FunNode*:(const Node*)(n), FunNode*:(Node*)(n), \
  const MacroNode*:(const Node*)(n), MacroNode*:(Node*)(n), \
  const CallNode*:(const Node*)(n), CallNode*:(Node*)(n), \
  const TypeCastNode*:(const Node*)(n), TypeCastNode*:(Node*)(n), \
  const VarNode*:(const Node*)(n), VarNode*:(Node*)(n), const RefNode*:(const Node*)(n), \
  RefNode*:(Node*)(n), const NamedArgNode*:(const Node*)(n), NamedArgNode*:(Node*)(n), \
  const SelectorNode*:(const Node*)(n), SelectorNode*:(Node*)(n), \
  const IndexNode*:(const Node*)(n), IndexNode*:(Node*)(n), \
  const SliceNode*:(const Node*)(n), SliceNode*:(Node*)(n), \
  const IfNode*:(const Node*)(n), IfNode*:(Node*)(n), const Expr*:(const Node*)(n), \
  Expr*:(Node*)(n), const TypeTypeNode*:(const Node*)(n), TypeTypeNode*:(Node*)(n), \
  const NamedTypeNode*:(const Node*)(n), NamedTypeNode*:(Node*)(n), \
  const AliasTypeNode*:(const Node*)(n), AliasTypeNode*:(Node*)(n), \
  const RefTypeNode*:(const Node*)(n), RefTypeNode*:(Node*)(n), \
  const BasicTypeNode*:(const Node*)(n), BasicTypeNode*:(Node*)(n), \
  const ArrayTypeNode*:(const Node*)(n), ArrayTypeNode*:(Node*)(n), \
  const TupleTypeNode*:(const Node*)(n), TupleTypeNode*:(Node*)(n), \
  const StructTypeNode*:(const Node*)(n), StructTypeNode*:(Node*)(n), \
  const FunTypeNode*:(const Node*)(n), FunTypeNode*:(Node*)(n), \
  const Type*:(const Node*)(n), Type*:(Node*)(n), const Node*:(const Node*)(n), \
  Node*:(Node*)(n))

#define as_Stmt(n) _Generic((n), const PkgNode*:(const Stmt*)(n), PkgNode*:(Stmt*)(n), \
  const FileNode*:(const Stmt*)(n), FileNode*:(Stmt*)(n), \
  const struct CUnitNode*:(const Stmt*)(n), struct CUnitNode*:(Stmt*)(n), \
  const CommentNode*:(const Stmt*)(n), CommentNode*:(Stmt*)(n), \
  const Stmt*:(const Stmt*)(n), Stmt*:(Stmt*)(n), \
  const Node*: ({ assert_is_Stmt(n); (const Stmt*)(n); }), \
  Node*: ({ assert_is_Stmt(n); (Stmt*)(n); }))

#define as_CUnitNode(n) _Generic((n), const PkgNode*:(const struct CUnitNode*)(n), \
  PkgNode*:(struct CUnitNode*)(n), const FileNode*:(const struct CUnitNode*)(n), \
  FileNode*:(struct CUnitNode*)(n), \
  const struct CUnitNode*:(const struct CUnitNode*)(n), \
  struct CUnitNode*:(struct CUnitNode*)(n), \
  const Node*: ({ assert_is_CUnitNode(n); (const struct CUnitNode*)(n); }), \
  Node*: ({ assert_is_CUnitNode(n); (struct CUnitNode*)(n); }))

#define as_Expr(n) _Generic((n), const NilNode*:(const Expr*)(n), NilNode*:(Expr*)(n), \
  const BoolLitNode*:(const Expr*)(n), BoolLitNode*:(Expr*)(n), \
  const IntLitNode*:(const Expr*)(n), IntLitNode*:(Expr*)(n), \
  const FloatLitNode*:(const Expr*)(n), FloatLitNode*:(Expr*)(n), \
  const StrLitNode*:(const Expr*)(n), StrLitNode*:(Expr*)(n), \
  const struct LitExpr*:(const Expr*)(n), struct LitExpr*:(Expr*)(n), \
  const IdNode*:(const Expr*)(n), IdNode*:(Expr*)(n), const BinOpNode*:(const Expr*)(n), \
  BinOpNode*:(Expr*)(n), const PrefixOpNode*:(const Expr*)(n), PrefixOpNode*:(Expr*)(n), \
  const PostfixOpNode*:(const Expr*)(n), PostfixOpNode*:(Expr*)(n), \
  const struct UnaryOpNode*:(const Expr*)(n), struct UnaryOpNode*:(Expr*)(n), \
  const ReturnNode*:(const Expr*)(n), ReturnNode*:(Expr*)(n), \
  const AssignNode*:(const Expr*)(n), AssignNode*:(Expr*)(n), \
  const TupleNode*:(const Expr*)(n), TupleNode*:(Expr*)(n), \
  const ArrayNode*:(const Expr*)(n), ArrayNode*:(Expr*)(n), \
  const struct ListExpr*:(const Expr*)(n), struct ListExpr*:(Expr*)(n), \
  const BlockNode*:(const Expr*)(n), BlockNode*:(Expr*)(n), \
  const FunNode*:(const Expr*)(n), FunNode*:(Expr*)(n), \
  const MacroNode*:(const Expr*)(n), MacroNode*:(Expr*)(n), \
  const CallNode*:(const Expr*)(n), CallNode*:(Expr*)(n), \
  const TypeCastNode*:(const Expr*)(n), TypeCastNode*:(Expr*)(n), \
  const VarNode*:(const Expr*)(n), VarNode*:(Expr*)(n), const RefNode*:(const Expr*)(n), \
  RefNode*:(Expr*)(n), const NamedArgNode*:(const Expr*)(n), NamedArgNode*:(Expr*)(n), \
  const SelectorNode*:(const Expr*)(n), SelectorNode*:(Expr*)(n), \
  const IndexNode*:(const Expr*)(n), IndexNode*:(Expr*)(n), \
  const SliceNode*:(const Expr*)(n), SliceNode*:(Expr*)(n), \
  const IfNode*:(const Expr*)(n), IfNode*:(Expr*)(n), const Expr*:(const Expr*)(n), \
  Expr*:(Expr*)(n), const Node*: ({ assert_is_Expr(n); (const Expr*)(n); }), \
  Node*: ({ assert_is_Expr(n); (Expr*)(n); }))

#define as_LitExpr(n) _Generic((n), const NilNode*:(const struct LitExpr*)(n), \
  NilNode*:(struct LitExpr*)(n), const BoolLitNode*:(const struct LitExpr*)(n), \
  BoolLitNode*:(struct LitExpr*)(n), const IntLitNode*:(const struct LitExpr*)(n), \
  IntLitNode*:(struct LitExpr*)(n), const FloatLitNode*:(const struct LitExpr*)(n), \
  FloatLitNode*:(struct LitExpr*)(n), const StrLitNode*:(const struct LitExpr*)(n), \
  StrLitNode*:(struct LitExpr*)(n), const struct LitExpr*:(const struct LitExpr*)(n), \
  struct LitExpr*:(struct LitExpr*)(n), \
  const Node*: ({ assert_is_LitExpr(n); (const struct LitExpr*)(n); }), \
  Node*: ({ assert_is_LitExpr(n); (struct LitExpr*)(n); }))

#define as_UnaryOpNode(n) _Generic((n), \
  const PrefixOpNode*:(const struct UnaryOpNode*)(n), \
  PrefixOpNode*:(struct UnaryOpNode*)(n), \
  const PostfixOpNode*:(const struct UnaryOpNode*)(n), \
  PostfixOpNode*:(struct UnaryOpNode*)(n), \
  const struct UnaryOpNode*:(const struct UnaryOpNode*)(n), \
  struct UnaryOpNode*:(struct UnaryOpNode*)(n), \
  const Node*: ({ assert_is_UnaryOpNode(n); (const struct UnaryOpNode*)(n); }), \
  Node*: ({ assert_is_UnaryOpNode(n); (struct UnaryOpNode*)(n); }))

#define as_ListExpr(n) _Generic((n), const TupleNode*:(const struct ListExpr*)(n), \
  TupleNode*:(struct ListExpr*)(n), const ArrayNode*:(const struct ListExpr*)(n), \
  ArrayNode*:(struct ListExpr*)(n), const struct ListExpr*:(const struct ListExpr*)(n), \
  struct ListExpr*:(struct ListExpr*)(n), \
  const Node*: ({ assert_is_ListExpr(n); (const struct ListExpr*)(n); }), \
  Node*: ({ assert_is_ListExpr(n); (struct ListExpr*)(n); }))

#define as_Type(n) _Generic((n), const TypeTypeNode*:(const Type*)(n), \
  TypeTypeNode*:(Type*)(n), const NamedTypeNode*:(const Type*)(n), \
  NamedTypeNode*:(Type*)(n), const AliasTypeNode*:(const Type*)(n), \
  AliasTypeNode*:(Type*)(n), const RefTypeNode*:(const Type*)(n), \
  RefTypeNode*:(Type*)(n), const BasicTypeNode*:(const Type*)(n), \
  BasicTypeNode*:(Type*)(n), const ArrayTypeNode*:(const Type*)(n), \
  ArrayTypeNode*:(Type*)(n), const TupleTypeNode*:(const Type*)(n), \
  TupleTypeNode*:(Type*)(n), const StructTypeNode*:(const Type*)(n), \
  StructTypeNode*:(Type*)(n), const FunTypeNode*:(const Type*)(n), \
  FunTypeNode*:(Type*)(n), const Type*:(const Type*)(n), Type*:(Type*)(n), \
  const Node*: ({ assert_is_Type(n); (const Type*)(n); }), \
  Node*: ({ assert_is_Type(n); (Type*)(n); }))

// <type>* nullable maybe_<type>(Node* n)
// const <type>* nullable maybe_<type>(const Node* n)
#define maybe_BadNode(n) (is_BadNode(n)?(BadNode*)(n):NULL)
#define maybe_FieldNode(n) (is_FieldNode(n)?(FieldNode*)(n):NULL)
#define maybe_Stmt(n) (is_Stmt(n)?as_Stmt(n):NULL)
#define maybe_CUnitNode(n) (is_CUnitNode(n)?as_CUnitNode(n):NULL)
#define maybe_PkgNode(n) (is_PkgNode(n)?(PkgNode*)(n):NULL)
#define maybe_FileNode(n) (is_FileNode(n)?(FileNode*)(n):NULL)
#define maybe_CommentNode(n) (is_CommentNode(n)?(CommentNode*)(n):NULL)
#define maybe_Expr(n) (is_Expr(n)?as_Expr(n):NULL)
#define maybe_LitExpr(n) (is_LitExpr(n)?as_LitExpr(n):NULL)
#define maybe_NilNode(n) (is_NilNode(n)?(NilNode*)(n):NULL)
#define maybe_BoolLitNode(n) (is_BoolLitNode(n)?(BoolLitNode*)(n):NULL)
#define maybe_IntLitNode(n) (is_IntLitNode(n)?(IntLitNode*)(n):NULL)
#define maybe_FloatLitNode(n) (is_FloatLitNode(n)?(FloatLitNode*)(n):NULL)
#define maybe_StrLitNode(n) (is_StrLitNode(n)?(StrLitNode*)(n):NULL)
#define maybe_IdNode(n) (is_IdNode(n)?(IdNode*)(n):NULL)
#define maybe_BinOpNode(n) (is_BinOpNode(n)?(BinOpNode*)(n):NULL)
#define maybe_UnaryOpNode(n) (is_UnaryOpNode(n)?as_UnaryOpNode(n):NULL)
#define maybe_PrefixOpNode(n) (is_PrefixOpNode(n)?(PrefixOpNode*)(n):NULL)
#define maybe_PostfixOpNode(n) (is_PostfixOpNode(n)?(PostfixOpNode*)(n):NULL)
#define maybe_ReturnNode(n) (is_ReturnNode(n)?(ReturnNode*)(n):NULL)
#define maybe_AssignNode(n) (is_AssignNode(n)?(AssignNode*)(n):NULL)
#define maybe_ListExpr(n) (is_ListExpr(n)?as_ListExpr(n):NULL)
#define maybe_TupleNode(n) (is_TupleNode(n)?(TupleNode*)(n):NULL)
#define maybe_ArrayNode(n) (is_ArrayNode(n)?(ArrayNode*)(n):NULL)
#define maybe_BlockNode(n) (is_BlockNode(n)?(BlockNode*)(n):NULL)
#define maybe_FunNode(n) (is_FunNode(n)?(FunNode*)(n):NULL)
#define maybe_MacroNode(n) (is_MacroNode(n)?(MacroNode*)(n):NULL)
#define maybe_CallNode(n) (is_CallNode(n)?(CallNode*)(n):NULL)
#define maybe_TypeCastNode(n) (is_TypeCastNode(n)?(TypeCastNode*)(n):NULL)
#define maybe_VarNode(n) (is_VarNode(n)?(VarNode*)(n):NULL)
#define maybe_RefNode(n) (is_RefNode(n)?(RefNode*)(n):NULL)
#define maybe_NamedArgNode(n) (is_NamedArgNode(n)?(NamedArgNode*)(n):NULL)
#define maybe_SelectorNode(n) (is_SelectorNode(n)?(SelectorNode*)(n):NULL)
#define maybe_IndexNode(n) (is_IndexNode(n)?(IndexNode*)(n):NULL)
#define maybe_SliceNode(n) (is_SliceNode(n)?(SliceNode*)(n):NULL)
#define maybe_IfNode(n) (is_IfNode(n)?(IfNode*)(n):NULL)
#define maybe_Type(n) (is_Type(n)?as_Type(n):NULL)
#define maybe_TypeTypeNode(n) (is_TypeTypeNode(n)?(TypeTypeNode*)(n):NULL)
#define maybe_NamedTypeNode(n) (is_NamedTypeNode(n)?(NamedTypeNode*)(n):NULL)
#define maybe_AliasTypeNode(n) (is_AliasTypeNode(n)?(AliasTypeNode*)(n):NULL)
#define maybe_RefTypeNode(n) (is_RefTypeNode(n)?(RefTypeNode*)(n):NULL)
#define maybe_BasicTypeNode(n) (is_BasicTypeNode(n)?(BasicTypeNode*)(n):NULL)
#define maybe_ArrayTypeNode(n) (is_ArrayTypeNode(n)?(ArrayTypeNode*)(n):NULL)
#define maybe_TupleTypeNode(n) (is_TupleTypeNode(n)?(TupleTypeNode*)(n):NULL)
#define maybe_StructTypeNode(n) (is_StructTypeNode(n)?(StructTypeNode*)(n):NULL)
#define maybe_FunTypeNode(n) (is_FunTypeNode(n)?(FunTypeNode*)(n):NULL)

// Type* nullable TypeOfNode(Node* n)
// Type* TypeOfNode(Type* n)
#define TypeOfNode(n) _Generic((n), const TypeTypeNode*:(const Type*)kType_type, \
  TypeTypeNode*:kType_type, const NamedTypeNode*:(const Type*)kType_type, \
  NamedTypeNode*:kType_type, const AliasTypeNode*:(const Type*)kType_type, \
  AliasTypeNode*:kType_type, const RefTypeNode*:(const Type*)kType_type, \
  RefTypeNode*:kType_type, const BasicTypeNode*:(const Type*)kType_type, \
  BasicTypeNode*:kType_type, const ArrayTypeNode*:(const Type*)kType_type, \
  ArrayTypeNode*:kType_type, const TupleTypeNode*:(const Type*)kType_type, \
  TupleTypeNode*:kType_type, const StructTypeNode*:(const Type*)kType_type, \
  StructTypeNode*:kType_type, const FunTypeNode*:(const Type*)kType_type, \
  FunTypeNode*:kType_type, const Type*:(const Type*)kType_type, Type*:kType_type, \
  const NilNode*:(const Type*)((Expr*)(n))->type, NilNode*:((Expr*)(n))->type, \
  const BoolLitNode*:(const Type*)((Expr*)(n))->type, BoolLitNode*:((Expr*)(n))->type, \
  const IntLitNode*:(const Type*)((Expr*)(n))->type, IntLitNode*:((Expr*)(n))->type, \
  const FloatLitNode*:(const Type*)((Expr*)(n))->type, FloatLitNode*:((Expr*)(n))->type, \
  const StrLitNode*:(const Type*)((Expr*)(n))->type, StrLitNode*:((Expr*)(n))->type, \
  const struct LitExpr*:(const Type*)((Expr*)(n))->type, \
  struct LitExpr*:((Expr*)(n))->type, const IdNode*:(const Type*)((Expr*)(n))->type, \
  IdNode*:((Expr*)(n))->type, const BinOpNode*:(const Type*)((Expr*)(n))->type, \
  BinOpNode*:((Expr*)(n))->type, const PrefixOpNode*:(const Type*)((Expr*)(n))->type, \
  PrefixOpNode*:((Expr*)(n))->type, \
  const PostfixOpNode*:(const Type*)((Expr*)(n))->type, \
  PostfixOpNode*:((Expr*)(n))->type, \
  const struct UnaryOpNode*:(const Type*)((Expr*)(n))->type, \
  struct UnaryOpNode*:((Expr*)(n))->type, \
  const ReturnNode*:(const Type*)((Expr*)(n))->type, ReturnNode*:((Expr*)(n))->type, \
  const AssignNode*:(const Type*)((Expr*)(n))->type, AssignNode*:((Expr*)(n))->type, \
  const TupleNode*:(const Type*)((Expr*)(n))->type, TupleNode*:((Expr*)(n))->type, \
  const ArrayNode*:(const Type*)((Expr*)(n))->type, ArrayNode*:((Expr*)(n))->type, \
  const struct ListExpr*:(const Type*)((Expr*)(n))->type, \
  struct ListExpr*:((Expr*)(n))->type, const BlockNode*:(const Type*)((Expr*)(n))->type, \
  BlockNode*:((Expr*)(n))->type, const FunNode*:(const Type*)((Expr*)(n))->type, \
  FunNode*:((Expr*)(n))->type, const MacroNode*:(const Type*)((Expr*)(n))->type, \
  MacroNode*:((Expr*)(n))->type, const CallNode*:(const Type*)((Expr*)(n))->type, \
  CallNode*:((Expr*)(n))->type, const TypeCastNode*:(const Type*)((Expr*)(n))->type, \
  TypeCastNode*:((Expr*)(n))->type, const VarNode*:(const Type*)((Expr*)(n))->type, \
  VarNode*:((Expr*)(n))->type, const RefNode*:(const Type*)((Expr*)(n))->type, \
  RefNode*:((Expr*)(n))->type, const NamedArgNode*:(const Type*)((Expr*)(n))->type, \
  NamedArgNode*:((Expr*)(n))->type, const SelectorNode*:(const Type*)((Expr*)(n))->type, \
  SelectorNode*:((Expr*)(n))->type, const IndexNode*:(const Type*)((Expr*)(n))->type, \
  IndexNode*:((Expr*)(n))->type, const SliceNode*:(const Type*)((Expr*)(n))->type, \
  SliceNode*:((Expr*)(n))->type, const IfNode*:(const Type*)((Expr*)(n))->type, \
  IfNode*:((Expr*)(n))->type, const Expr*:(const Type*)((Expr*)(n))->type, \
  Expr*:((Expr*)(n))->type, BadNode*:NULL, FieldNode*:NULL, Stmt*:NULL, \
  struct CUnitNode*:NULL, PkgNode*:NULL, FileNode*:NULL, CommentNode*:NULL, \
  const Node*: ( is_Type(n) ? (const Type*)kType_type : \
   is_Expr(n) ? (const Type*)((Expr*)(n))->type : NULL ), \
  Node*:( is_Type(n) ? kType_type : is_Expr(n) ? ((Expr*)(n))->type : NULL))

union NodeUnion {
BadNode _0; FieldNode _1; PkgNode _2; FileNode _3; CommentNode _4; NilNode _5;
  BoolLitNode _6; IntLitNode _7; FloatLitNode _8; StrLitNode _9; IdNode _10;
  BinOpNode _11; PrefixOpNode _12; PostfixOpNode _13; ReturnNode _14;
  AssignNode _15; TupleNode _16; ArrayNode _17; BlockNode _18; FunNode _19;
  MacroNode _20; CallNode _21; TypeCastNode _22; VarNode _23; RefNode _24;
  NamedArgNode _25; SelectorNode _26; IndexNode _27; SliceNode _28; IfNode _29;
  TypeTypeNode _30; NamedTypeNode _31; AliasTypeNode _32; RefTypeNode _33;
  BasicTypeNode _34; ArrayTypeNode _35; TupleTypeNode _36; StructTypeNode _37;
  FunTypeNode _38;
};

typedef struct ASTVisitor     ASTVisitor;
typedef struct ASTVisitorFuns ASTVisitorFuns;
typedef int(*ASTVisitorFun)(ASTVisitor*, const Node*);
struct ASTVisitor {
  ASTVisitorFun ftable[41];
};
void ASTVisitorInit(ASTVisitor*, const ASTVisitorFuns*);
// error ASTVisit(ASTVisitor* v, const NODE_TYPE* n)
#define ASTVisit(v, n) _Generic((n), \
  const BadNode*: (v)->ftable[NBad]((v),(const Node*)(n)), \
  BadNode*: (v)->ftable[NBad]((v),(const Node*)(n)), \
  const FieldNode*: (v)->ftable[NField]((v),(const Node*)(n)), \
  FieldNode*: (v)->ftable[NField]((v),(const Node*)(n)), \
  const PkgNode*: (v)->ftable[NPkg]((v),(const Node*)(n)), \
  PkgNode*: (v)->ftable[NPkg]((v),(const Node*)(n)), \
  const FileNode*: (v)->ftable[NFile]((v),(const Node*)(n)), \
  FileNode*: (v)->ftable[NFile]((v),(const Node*)(n)), \
  const CommentNode*: (v)->ftable[NComment]((v),(const Node*)(n)), \
  CommentNode*: (v)->ftable[NComment]((v),(const Node*)(n)), \
  const NilNode*: (v)->ftable[NNil]((v),(const Node*)(n)), \
  NilNode*: (v)->ftable[NNil]((v),(const Node*)(n)), \
  const BoolLitNode*: (v)->ftable[NBoolLit]((v),(const Node*)(n)), \
  BoolLitNode*: (v)->ftable[NBoolLit]((v),(const Node*)(n)), \
  const IntLitNode*: (v)->ftable[NIntLit]((v),(const Node*)(n)), \
  IntLitNode*: (v)->ftable[NIntLit]((v),(const Node*)(n)), \
  const FloatLitNode*: (v)->ftable[NFloatLit]((v),(const Node*)(n)), \
  FloatLitNode*: (v)->ftable[NFloatLit]((v),(const Node*)(n)), \
  const StrLitNode*: (v)->ftable[NStrLit]((v),(const Node*)(n)), \
  StrLitNode*: (v)->ftable[NStrLit]((v),(const Node*)(n)), \
  const IdNode*: (v)->ftable[NId]((v),(const Node*)(n)), \
  IdNode*: (v)->ftable[NId]((v),(const Node*)(n)), \
  const BinOpNode*: (v)->ftable[NBinOp]((v),(const Node*)(n)), \
  BinOpNode*: (v)->ftable[NBinOp]((v),(const Node*)(n)), \
  const PrefixOpNode*: (v)->ftable[NPrefixOp]((v),(const Node*)(n)), \
  PrefixOpNode*: (v)->ftable[NPrefixOp]((v),(const Node*)(n)), \
  const PostfixOpNode*: (v)->ftable[NPostfixOp]((v),(const Node*)(n)), \
  PostfixOpNode*: (v)->ftable[NPostfixOp]((v),(const Node*)(n)), \
  const ReturnNode*: (v)->ftable[NReturn]((v),(const Node*)(n)), \
  ReturnNode*: (v)->ftable[NReturn]((v),(const Node*)(n)), \
  const AssignNode*: (v)->ftable[NAssign]((v),(const Node*)(n)), \
  AssignNode*: (v)->ftable[NAssign]((v),(const Node*)(n)), \
  const TupleNode*: (v)->ftable[NTuple]((v),(const Node*)(n)), \
  TupleNode*: (v)->ftable[NTuple]((v),(const Node*)(n)), \
  const ArrayNode*: (v)->ftable[NArray]((v),(const Node*)(n)), \
  ArrayNode*: (v)->ftable[NArray]((v),(const Node*)(n)), \
  const BlockNode*: (v)->ftable[NBlock]((v),(const Node*)(n)), \
  BlockNode*: (v)->ftable[NBlock]((v),(const Node*)(n)), \
  const FunNode*: (v)->ftable[NFun]((v),(const Node*)(n)), \
  FunNode*: (v)->ftable[NFun]((v),(const Node*)(n)), \
  const MacroNode*: (v)->ftable[NMacro]((v),(const Node*)(n)), \
  MacroNode*: (v)->ftable[NMacro]((v),(const Node*)(n)), \
  const CallNode*: (v)->ftable[NCall]((v),(const Node*)(n)), \
  CallNode*: (v)->ftable[NCall]((v),(const Node*)(n)), \
  const TypeCastNode*: (v)->ftable[NTypeCast]((v),(const Node*)(n)), \
  TypeCastNode*: (v)->ftable[NTypeCast]((v),(const Node*)(n)), \
  const VarNode*: (v)->ftable[NVar]((v),(const Node*)(n)), \
  VarNode*: (v)->ftable[NVar]((v),(const Node*)(n)), \
  const RefNode*: (v)->ftable[NRef]((v),(const Node*)(n)), \
  RefNode*: (v)->ftable[NRef]((v),(const Node*)(n)), \
  const NamedArgNode*: (v)->ftable[NNamedArg]((v),(const Node*)(n)), \
  NamedArgNode*: (v)->ftable[NNamedArg]((v),(const Node*)(n)), \
  const SelectorNode*: (v)->ftable[NSelector]((v),(const Node*)(n)), \
  SelectorNode*: (v)->ftable[NSelector]((v),(const Node*)(n)), \
  const IndexNode*: (v)->ftable[NIndex]((v),(const Node*)(n)), \
  IndexNode*: (v)->ftable[NIndex]((v),(const Node*)(n)), \
  const SliceNode*: (v)->ftable[NSlice]((v),(const Node*)(n)), \
  SliceNode*: (v)->ftable[NSlice]((v),(const Node*)(n)), \
  const IfNode*: (v)->ftable[NIf]((v),(const Node*)(n)), \
  IfNode*: (v)->ftable[NIf]((v),(const Node*)(n)), \
  const TypeTypeNode*: (v)->ftable[NTypeType]((v),(const Node*)(n)), \
  TypeTypeNode*: (v)->ftable[NTypeType]((v),(const Node*)(n)), \
  const NamedTypeNode*: (v)->ftable[NNamedType]((v),(const Node*)(n)), \
  NamedTypeNode*: (v)->ftable[NNamedType]((v),(const Node*)(n)), \
  const AliasTypeNode*: (v)->ftable[NAliasType]((v),(const Node*)(n)), \
  AliasTypeNode*: (v)->ftable[NAliasType]((v),(const Node*)(n)), \
  const RefTypeNode*: (v)->ftable[NRefType]((v),(const Node*)(n)), \
  RefTypeNode*: (v)->ftable[NRefType]((v),(const Node*)(n)), \
  const BasicTypeNode*: (v)->ftable[NBasicType]((v),(const Node*)(n)), \
  BasicTypeNode*: (v)->ftable[NBasicType]((v),(const Node*)(n)), \
  const ArrayTypeNode*: (v)->ftable[NArrayType]((v),(const Node*)(n)), \
  ArrayTypeNode*: (v)->ftable[NArrayType]((v),(const Node*)(n)), \
  const TupleTypeNode*: (v)->ftable[NTupleType]((v),(const Node*)(n)), \
  TupleTypeNode*: (v)->ftable[NTupleType]((v),(const Node*)(n)), \
  const StructTypeNode*: (v)->ftable[NStructType]((v),(const Node*)(n)), \
  StructTypeNode*: (v)->ftable[NStructType]((v),(const Node*)(n)), \
  const FunTypeNode*: (v)->ftable[NFunType]((v),(const Node*)(n)), \
  FunTypeNode*: (v)->ftable[NFunType]((v),(const Node*)(n)), \
  const Node*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  Node*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const Stmt*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  Stmt*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const struct CUnitNode*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  struct CUnitNode*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const Expr*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  Expr*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const struct LitExpr*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  struct LitExpr*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const struct UnaryOpNode*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  struct UnaryOpNode*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const struct ListExpr*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  struct ListExpr*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  const Type*: (v)->ftable[(n)->kind]((v),(const Node*)(n)), \
  Type*: (v)->ftable[(n)->kind]((v),(const Node*)(n)))

struct ASTVisitorFuns {
  error(*nullable Bad)(ASTVisitor*, const BadNode*);
  error(*nullable Field)(ASTVisitor*, const FieldNode*);
  error(*nullable Pkg)(ASTVisitor*, const PkgNode*);
  error(*nullable File)(ASTVisitor*, const FileNode*);
  error(*nullable Comment)(ASTVisitor*, const CommentNode*);
  error(*nullable Nil)(ASTVisitor*, const NilNode*);
  error(*nullable BoolLit)(ASTVisitor*, const BoolLitNode*);
  error(*nullable IntLit)(ASTVisitor*, const IntLitNode*);
  error(*nullable FloatLit)(ASTVisitor*, const FloatLitNode*);
  error(*nullable StrLit)(ASTVisitor*, const StrLitNode*);
  error(*nullable Id)(ASTVisitor*, const IdNode*);
  error(*nullable BinOp)(ASTVisitor*, const BinOpNode*);
  error(*nullable PrefixOp)(ASTVisitor*, const PrefixOpNode*);
  error(*nullable PostfixOp)(ASTVisitor*, const PostfixOpNode*);
  error(*nullable Return)(ASTVisitor*, const ReturnNode*);
  error(*nullable Assign)(ASTVisitor*, const AssignNode*);
  error(*nullable Tuple)(ASTVisitor*, const TupleNode*);
  error(*nullable Array)(ASTVisitor*, const ArrayNode*);
  error(*nullable Block)(ASTVisitor*, const BlockNode*);
  error(*nullable Fun)(ASTVisitor*, const FunNode*);
  error(*nullable Macro)(ASTVisitor*, const MacroNode*);
  error(*nullable Call)(ASTVisitor*, const CallNode*);
  error(*nullable TypeCast)(ASTVisitor*, const TypeCastNode*);
  error(*nullable Var)(ASTVisitor*, const VarNode*);
  error(*nullable Ref)(ASTVisitor*, const RefNode*);
  error(*nullable NamedArg)(ASTVisitor*, const NamedArgNode*);
  error(*nullable Selector)(ASTVisitor*, const SelectorNode*);
  error(*nullable Index)(ASTVisitor*, const IndexNode*);
  error(*nullable Slice)(ASTVisitor*, const SliceNode*);
  error(*nullable If)(ASTVisitor*, const IfNode*);
  error(*nullable TypeType)(ASTVisitor*, const TypeTypeNode*);
  error(*nullable NamedType)(ASTVisitor*, const NamedTypeNode*);
  error(*nullable AliasType)(ASTVisitor*, const AliasTypeNode*);
  error(*nullable RefType)(ASTVisitor*, const RefTypeNode*);
  error(*nullable BasicType)(ASTVisitor*, const BasicTypeNode*);
  error(*nullable ArrayType)(ASTVisitor*, const ArrayTypeNode*);
  error(*nullable TupleType)(ASTVisitor*, const TupleTypeNode*);
  error(*nullable StructType)(ASTVisitor*, const StructTypeNode*);
  error(*nullable FunType)(ASTVisitor*, const FunTypeNode*);

  // class-level visitors called for nodes without specific visitors
  error(*nullable Stmt)(ASTVisitor*, const Stmt*);
  error(*nullable CUnit)(ASTVisitor*, const struct CUnitNode*);
  error(*nullable Expr)(ASTVisitor*, const Expr*);
  error(*nullable LitExpr)(ASTVisitor*, const struct LitExpr*);
  error(*nullable UnaryOp)(ASTVisitor*, const struct UnaryOpNode*);
  error(*nullable ListExpr)(ASTVisitor*, const struct ListExpr*);
  error(*nullable Type)(ASTVisitor*, const Type*);

  // catch-all fallback visitor
  error(*nullable Node)(ASTVisitor*, const Node*);
};

//END GENERATED CODE

// keep the size of nodes in check. Update this if needed.
static_assert(sizeof(union NodeUnion) >= 104, "AST size shrunk");
static_assert(sizeof(union NodeUnion) <= 104, "AST size grew");

// NodeKindName returns a printable name. E.g. NBad => "Bad"
const char* NodeKindName(NodeKind);

inline static bool NodeIsPrimitiveConst(const Node* n) {
  // used by resolve_id
  return n->kind == NNil || n->kind == NBasicType || n->kind == NBoolLit;
}

inline static Node* nullable NodeAlloc(Mem mem) {
  return (Node*)memalloc(mem, sizeof(union NodeUnion));
}

Node* NodeInit(Node* n, NodeKind kind);

#define NodePosSpan(n) _NodePosSpan(as_Node(n))
PosSpan _NodePosSpan(const Node* n);

inline static Node* NodeCopy(Node* dst, const Node* src) {
  memcpy(dst, src, sizeof(union NodeUnion));
  return dst;
}

// NodeRefVar increments the reference counter of a Var node. Returns n as a convenience.
inline static VarNode* NodeRefVar(VarNode* n) {
  n->nrefs++;
  return n;
}
// NodeUnrefVar decrements the reference counter of a Var node.
// Returns the value of n->var.nrefs after the subtraction.
inline static u32 NodeUnrefVar(VarNode* n) {
  assertgt(n->nrefs, 0);
  return --n->nrefs;
}

// --------------------------------------------------------------------------------------
// repr

// NodeReprFlags changes behavior of NodeRepr
typedef u32 NodeReprFlags; // changes behavior of NodeRepr

enum NodeReprFlags {
  NodeReprNoColor  = 1 << 0, // disable ANSI terminal styling
  NodeReprColor    = 1 << 1, // enable ANSI terminal styling (even if stderr is not a TTY)
  NodeReprTypes    = 1 << 2, // include types in the output
  NodeReprUseCount = 1 << 3, // include information about uses (ie for Var)
  NodeReprRefs     = 1 << 4, // include "#N" reference indicators
  NodeReprAttrs    = 1 << 5, // include "@attr" attributes
} END_TYPED_ENUM(NodeReprFlags)

// nodename returns a node's type name. E.g. "Tuple"
// const char* nodename(const Node* n)
#define nodename(n) NodeKindName(as_Node(assertnotnull(n))->kind)

// NodeRepr formats an AST as a printable text representation
#define NodeRepr(s,n,fl) _NodeStr((s),as_Node(n),(fl))
Str _NodeRepr(Str s, const Node* nullable n, NodeReprFlags fl);

// NodeStr appends a short representation of an AST node to s
#define NodeStr(s,n) _NodeStr((s),as_Node(n))
Str _NodeStr(Str s, const Node* nullable n);

// fmtnode returns a short representation of n using NodeStr, suitable for error messages.
// This function is not suitable for high-frequency use as it uses temporary buffers in TLS.
#define fmtnode(n) _fmtnode(as_Node(n))
const char* _fmtnode(const Node* n);

// fmtast returns an exhaustive representation of n using NodeRepr.
// This function is not suitable for high-frequency use as it uses temporary buffers in TLS.
#define fmtast(n) _fmtast(as_Node(n))
const char* _fmtast(const Node* nullable n);

// --------------------------------------------------------------------------------------

Scope* nullable ScopeNew(Mem mem, const Scope* nullable parent);
void ScopeFree(Scope*, Mem mem);
const Node* nullable ScopeLookup(const Scope*, Sym);

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

ASSUME_NONNULL_END
