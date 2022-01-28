// AST nodes
#pragma once
#include "../array.h"
#include "../sym.h"
#include "token.h"
#include "type.h"
#include "pos.h"
ASSUME_NONNULL_BEGIN

typedef struct Node    Node;      // AST node, basis for Stmt, Expr and Type
typedef struct Stmt    Stmt;      // AST statement
typedef struct Expr    Expr;      // AST expression
typedef struct LitExpr LitExpr;   // AST constant literal expression
typedef struct Type    Type;      // AST type
typedef struct Scope   Scope;     // lexical namespace (may be chained)
typedef u8             NodeKind;  // AST node kind (NNone, NBad, NBoolLit ...)
typedef u16            NodeFlags; // NF_* constants; AST node flags (Unresolved, Const ...)

DEF_TYPED_ARRAY(NodeArray, Node*)
DEF_TYPED_ARRAY(TypeArray, Type*)

struct Node {
  void* nullable irval;  // used by IR builders for temporary storage
  Pos            pos;    // source origin & position
  Pos            endpos; // NoPos means "only use pos".
  NodeFlags      flags;  // flags describe meta attributes of the node
  NodeKind       kind;   // kind of node (e.g. NId)
} ATTR_PACKED;

struct BadNode { Node; }; // substitute "filler" for invalid syntax

// statements
struct Stmt { Node; } ATTR_PACKED;
struct CUnitNode { Stmt;
  const Str       name;         // reference to str in corresponding Pkg or Source struct
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
} ATTR_PACKED;

// literal constant expressions
struct LitExpr { Expr; } ATTR_PACKED;
struct BoolLitNode  { LitExpr; u64 ival; }; // boolean literal
struct IntLitNode   { LitExpr; u64 ival; }; // integer literal
struct FloatLitNode { LitExpr; f64 fval; }; // floating-point literal
struct StrLitNode   { LitExpr; Str sval; }; // string literal
struct NilNode      { LitExpr; };           // the nil atom

struct IdNode { Expr;
  Sym   name;
  Node* target;
};
struct BinOpNode { Expr;
  Tok   op;
  Expr* left;
  Expr* right;
};
struct UnaryOpNode { Expr; // used for NPrefixOp, NPostfixOp, NReturn, NAssign
  Tok   op;
  Expr* expr;
};
struct ListExpr { Expr;
  NodeArray a;            // array of nodes
  Node*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct TupleNode { struct ListExpr; };
struct ArrayNode { struct ListExpr; };
struct BlockNode { struct ListExpr; };
struct FunNode { Expr;
  Expr* nullable params;  // input params (NTuple or NULL if none)
  Expr* nullable result;  // output results (NTuple | NExpr)
  Sym   nullable name;    // NULL for lambda
  Expr* nullable body;    // NULL for fun-declaration
};
struct MacroNode { Expr;
  Node* nullable params;  // input params (NTuple or NULL if none)
  Sym   nullable name;
  Node*          template;
};
struct CallNode { Expr;
  Expr* receiver;      // Fun or Id
  Expr* nullable args; // NULL if there are no args, else a NTuple
};
struct TypeCastNode { Expr;
  Node* receiver;      // Type or Id
  Node* nullable args; // NULL if there are no args, else a NTuple
};
struct FieldNode { Expr;
  u32            nrefs; // reference count
  u32            index; // argument index or struct index
  Sym            name;
  Expr* nullable init;  // initial value (may be NULL)
};
struct VarNode { Expr;
  bool           isconst; // immutable storage? (true for "const x" vars) TODO: use NF_*
  u32            nrefs;   // reference count
  u32            index;   // argument index (used by function parameters)
  Sym            name;
  Node* nullable init;    // initial/default value (Type or Expr)
};
struct RefNode { Expr;
  Node* target;
};
struct NamedValNode { Expr;
  Sym   name;
  Expr* value;
};
struct SelectorNode { Expr; // Selector = Expr "." ( Ident | Selector )
  u32      indices_st[7]; // indices storage
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
} ATTR_PACKED;
struct BasicTypeNode { Type;
  TypeCode typeCode;
  Sym      name;
};
struct ArrayTypeNode { Type;
  u32            size;     // used for array. 0 until sizeexpr is resolved
  Node* nullable sizeexpr; // NULL for inferred types
  Type*          subtype;
};
struct TupleTypeNode { Type;
  TypeArray a;            // Type[]
  Type*     a_storage[5]; // in-struct storage for the first few elements
};
struct StructTypeNode { Type;
  Sym nullable name;         // NULL for anonymous structs
  NodeArray    fields;            // FieldNode[]
  Node*        fields_storage[4]; // in-struct storage for the first few fields
};
struct FunTypeNode { Type;
  Node* nullable params; // NTuple of NVar or null if no params
  Type* nullable result; // NTupleType of types or single type
};


struct Scope {
  const Scope* parent;
  SymMap       bindings; // must be last member
};


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

// Node{Is,Set,Clear}Unresolved controls the "unresolved" flag of a node
inline static bool NodeIsUnresolved(const Node* n) { return (n->flags & NF_Unresolved) != 0; }
inline static void NodeSetUnresolved(Node* n) { n->flags |= NF_Unresolved; }
inline static void NodeClearUnresolved(Node* n) { n->flags &= ~NF_Unresolved; }
inline static void NodeTransferUnresolved(Node* parent, Node* child) {
  parent->flags |= child->flags & NF_Unresolved;
}

// Node{Is,Set,Clear}Const controls the "constant value" flag of a node
inline static bool NodeIsConst(const Node* n) { return (n->flags & NF_Const) != 0; }
inline static void NodeSetConst(Node* n) { n->flags |= NF_Const; }
inline static void NodeClearConst(Node* n) { n->flags &= ~NF_Const; }
inline static void NodeTransferConst(Node* parent, Node* child) {
  // parent is mutable if n OR child is NOT const, else parent is marked const.
  parent->flags |= child->flags & NF_Const;
}
inline static void NodeTransferMut(Node* parent, Node* child) {
  // parent is const if n AND child is const, else parent is marked mutable.
  parent->flags = (parent->flags & ~NF_Const) | (
    (parent->flags & NF_Const) &
    (child->flags & NF_Const)
  );
}
inline static void NodeTransferMut2(Node* parent, Node* child1, Node* child2) {
  // parent is const if n AND child1 AND child2 is const, else parent is marked mutable.
  parent->flags = (parent->flags & ~NF_Const) | (
    (parent->flags & NF_Const) &
    (child1->flags & NF_Const) &
    (child2->flags & NF_Const)
  );
}

// Node{Is,Set,Clear}Param controls the "is function parameter" flag of a node
inline static bool NodeIsParam(const Node* n) { return (n->flags & NF_Param) != 0; }
inline static void NodeSetParam(Node* n) { n->flags |= NF_Param; }
inline static void NodeClearParam(Node* n) { n->flags &= ~NF_Param; }

// Node{Is,Set,Clear}MacroParam controls the "is function parameter" flag of a node
inline static bool NodeIsMacroParam(const Node* n) { return (n->flags & NF_MacroParam) != 0; }
inline static void NodeSetMacroParam(Node* n) { n->flags |= NF_MacroParam; }
inline static void NodeClearMacroParam(Node* n) { n->flags &= ~NF_MacroParam; }

// Node{Is,Set,Clear}Unused controls the "is unused" flag of a node
inline static bool NodeIsUnused(const Node* n) { return (n->flags & NF_Unused) != 0; }
inline static void NodeSetUnused(Node* n) { n->flags |= NF_Unused; }
inline static void NodeClearUnused(Node* n) { n->flags &= ~NF_Unused; }

// Node{Is,Set,Clear}Public controls the "is public" flag of a node
inline static bool NodeIsPublic(const Node* n) { return (n->flags & NF_Public) != 0; }
inline static void NodeSetPublic(Node* n) { n->flags |= NF_Public; }
inline static void NodeClearPublic(Node* n) { n->flags &= ~NF_Public; }

// Node{Is,Set,Clear}RValue controls the "is rvalue" flag of a node
inline static bool NodeIsRValue(const Node* n) { return (n->flags & NF_RValue) != 0; }
inline static void NodeSetRValue(Node* n) { n->flags |= NF_RValue; }
inline static void NodeClearRValue(Node* n) { n->flags &= ~NF_RValue; }

inline static void NodeTransferCustomInit(Node* parent, Node* child) {
  parent->flags |= child->flags & NF_CustomInit;
}
inline static void NodeTransferPartialType2(Node* parent, Node* c1, Node* c2) {
  parent->flags |= (c1->flags & NF_PartialType) |
                   (c2->flags & NF_PartialType);
}


//BEGIN GENERATED CODE by ast_gen.py

enum NodeKind {
  NBad            =  0, // struct BadNode
  NStmt_BEG       =  1,
    NCUnit_BEG    =  1,
      NPkg        =  1, // struct PkgNode
      NFile       =  2, // struct FileNode
    NCUnit_END    =  2,
    NComment      =  3, // struct CommentNode
  NStmt_END       =  3,
  NExpr_BEG       =  4,
    NLitExpr_BEG  =  4,
      NBoolLit    =  4, // struct BoolLitNode
      NIntLit     =  5, // struct IntLitNode
      NFloatLit   =  6, // struct FloatLitNode
      NStrLit     =  7, // struct StrLitNode
      NNil        =  8, // struct NilNode
    NLitExpr_END  =  8,
    NId           =  9, // struct IdNode
    NBinOp        = 10, // struct BinOpNode
    NUnaryOp      = 11, // struct UnaryOpNode
    NListExpr_BEG = 12,
      NTuple      = 12, // struct TupleNode
      NArray      = 13, // struct ArrayNode
      NBlock      = 14, // struct BlockNode
    NListExpr_END = 14,
    NFun          = 15, // struct FunNode
    NMacro        = 16, // struct MacroNode
    NCall         = 17, // struct CallNode
    NTypeCast     = 18, // struct TypeCastNode
    NField        = 19, // struct FieldNode
    NVar          = 20, // struct VarNode
    NRef          = 21, // struct RefNode
    NNamedVal     = 22, // struct NamedValNode
    NSelector     = 23, // struct SelectorNode
    NIndex        = 24, // struct IndexNode
    NSlice        = 25, // struct SliceNode
    NIf           = 26, // struct IfNode
  NExpr_END       = 26,
  NType_BEG       = 27,
    NBasicType    = 27, // struct BasicTypeNode
    NArrayType    = 28, // struct ArrayTypeNode
    NTupleType    = 29, // struct TupleTypeNode
    NStructType   = 30, // struct StructTypeNode
    NFunType      = 31, // struct FunTypeNode
  NType_END       = 31,
} END_TYPED_ENUM(NodeKind)

// NodeKindName returns a printable name. E.g. NBad => "Bad"
const char* NodeKindName(NodeKind);

typedef struct BadNode BadNode;
typedef struct PkgNode PkgNode;
typedef struct FileNode FileNode;
typedef struct CommentNode CommentNode;
typedef struct BoolLitNode BoolLitNode;
typedef struct IntLitNode IntLitNode;
typedef struct FloatLitNode FloatLitNode;
typedef struct StrLitNode StrLitNode;
typedef struct NilNode NilNode;
typedef struct IdNode IdNode;
typedef struct BinOpNode BinOpNode;
typedef struct UnaryOpNode UnaryOpNode;
typedef struct TupleNode TupleNode;
typedef struct ArrayNode ArrayNode;
typedef struct BlockNode BlockNode;
typedef struct FunNode FunNode;
typedef struct MacroNode MacroNode;
typedef struct CallNode CallNode;
typedef struct TypeCastNode TypeCastNode;
typedef struct FieldNode FieldNode;
typedef struct VarNode VarNode;
typedef struct RefNode RefNode;
typedef struct NamedValNode NamedValNode;
typedef struct SelectorNode SelectorNode;
typedef struct IndexNode IndexNode;
typedef struct SliceNode SliceNode;
typedef struct IfNode IfNode;
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
#define NodeKindIsListExpr(k) (NListExpr_BEG <= (k) && (k) <= NListExpr_END)
#define NodeKindIsType(k) (NType_BEG <= (k) && (k) <= NType_END)

// bool is_<kind>(const Node*)
#define is_BadNode(n) ((n)->kind==NBad)
#define is_Stmt(n) NodeKindIsStmt((n)->kind)
#define is_CUnitNode(n) NodeKindIsCUnit((n)->kind)
#define is_PkgNode(n) ((n)->kind==NPkg)
#define is_FileNode(n) ((n)->kind==NFile)
#define is_CommentNode(n) ((n)->kind==NComment)
#define is_Expr(n) NodeKindIsExpr((n)->kind)
#define is_LitExpr(n) NodeKindIsLitExpr((n)->kind)
#define is_BoolLitNode(n) ((n)->kind==NBoolLit)
#define is_IntLitNode(n) ((n)->kind==NIntLit)
#define is_FloatLitNode(n) ((n)->kind==NFloatLit)
#define is_StrLitNode(n) ((n)->kind==NStrLit)
#define is_NilNode(n) ((n)->kind==NNil)
#define is_IdNode(n) ((n)->kind==NId)
#define is_BinOpNode(n) ((n)->kind==NBinOp)
#define is_UnaryOpNode(n) ((n)->kind==NUnaryOp)
#define is_ListExpr(n) NodeKindIsListExpr((n)->kind)
#define is_TupleNode(n) ((n)->kind==NTuple)
#define is_ArrayNode(n) ((n)->kind==NArray)
#define is_BlockNode(n) ((n)->kind==NBlock)
#define is_FunNode(n) ((n)->kind==NFun)
#define is_MacroNode(n) ((n)->kind==NMacro)
#define is_CallNode(n) ((n)->kind==NCall)
#define is_TypeCastNode(n) ((n)->kind==NTypeCast)
#define is_FieldNode(n) ((n)->kind==NField)
#define is_VarNode(n) ((n)->kind==NVar)
#define is_RefNode(n) ((n)->kind==NRef)
#define is_NamedValNode(n) ((n)->kind==NNamedVal)
#define is_SelectorNode(n) ((n)->kind==NSelector)
#define is_IndexNode(n) ((n)->kind==NIndex)
#define is_SliceNode(n) ((n)->kind==NSlice)
#define is_IfNode(n) ((n)->kind==NIf)
#define is_Type(n) NodeKindIsType((n)->kind)
#define is_BasicTypeNode(n) ((n)->kind==NBasicType)
#define is_ArrayTypeNode(n) ((n)->kind==NArrayType)
#define is_TupleTypeNode(n) ((n)->kind==NTupleType)
#define is_StructTypeNode(n) ((n)->kind==NStructType)
#define is_FunTypeNode(n) ((n)->kind==NFunType)

// void assert_is_<kind>(const Node*)
#define _assert_is1(NAME,n) ({ \
  NodeKind nk__ = assertnotnull(n)->kind; \
  assertf(NodeKindIs##NAME(nk__), "expected N%s; got N%s #%d", \
          #NAME, NodeKindName(nk__), nk__); \
})
#define assert_is_BadNode(n) asserteq(assertnotnull(n)->kind,NBad)
#define assert_is_Stmt(n) _assert_is1(Stmt,(n))
#define assert_is_CUnitNode(n) _assert_is1(CUnit,(n))
#define assert_is_PkgNode(n) asserteq(assertnotnull(n)->kind,NPkg)
#define assert_is_FileNode(n) asserteq(assertnotnull(n)->kind,NFile)
#define assert_is_CommentNode(n) asserteq(assertnotnull(n)->kind,NComment)
#define assert_is_Expr(n) _assert_is1(Expr,(n))
#define assert_is_LitExpr(n) _assert_is1(LitExpr,(n))
#define assert_is_BoolLitNode(n) asserteq(assertnotnull(n)->kind,NBoolLit)
#define assert_is_IntLitNode(n) asserteq(assertnotnull(n)->kind,NIntLit)
#define assert_is_FloatLitNode(n) asserteq(assertnotnull(n)->kind,NFloatLit)
#define assert_is_StrLitNode(n) asserteq(assertnotnull(n)->kind,NStrLit)
#define assert_is_NilNode(n) asserteq(assertnotnull(n)->kind,NNil)
#define assert_is_IdNode(n) asserteq(assertnotnull(n)->kind,NId)
#define assert_is_BinOpNode(n) asserteq(assertnotnull(n)->kind,NBinOp)
#define assert_is_UnaryOpNode(n) asserteq(assertnotnull(n)->kind,NUnaryOp)
#define assert_is_ListExpr(n) _assert_is1(ListExpr,(n))
#define assert_is_TupleNode(n) asserteq(assertnotnull(n)->kind,NTuple)
#define assert_is_ArrayNode(n) asserteq(assertnotnull(n)->kind,NArray)
#define assert_is_BlockNode(n) asserteq(assertnotnull(n)->kind,NBlock)
#define assert_is_FunNode(n) asserteq(assertnotnull(n)->kind,NFun)
#define assert_is_MacroNode(n) asserteq(assertnotnull(n)->kind,NMacro)
#define assert_is_CallNode(n) asserteq(assertnotnull(n)->kind,NCall)
#define assert_is_TypeCastNode(n) asserteq(assertnotnull(n)->kind,NTypeCast)
#define assert_is_FieldNode(n) asserteq(assertnotnull(n)->kind,NField)
#define assert_is_VarNode(n) asserteq(assertnotnull(n)->kind,NVar)
#define assert_is_RefNode(n) asserteq(assertnotnull(n)->kind,NRef)
#define assert_is_NamedValNode(n) asserteq(assertnotnull(n)->kind,NNamedVal)
#define assert_is_SelectorNode(n) asserteq(assertnotnull(n)->kind,NSelector)
#define assert_is_IndexNode(n) asserteq(assertnotnull(n)->kind,NIndex)
#define assert_is_SliceNode(n) asserteq(assertnotnull(n)->kind,NSlice)
#define assert_is_IfNode(n) asserteq(assertnotnull(n)->kind,NIf)
#define assert_is_Type(n) _assert_is1(Type,(n))
#define assert_is_BasicTypeNode(n) asserteq(assertnotnull(n)->kind,NBasicType)
#define assert_is_ArrayTypeNode(n) asserteq(assertnotnull(n)->kind,NArrayType)
#define assert_is_TupleTypeNode(n) asserteq(assertnotnull(n)->kind,NTupleType)
#define assert_is_StructTypeNode(n) asserteq(assertnotnull(n)->kind,NStructType)
#define assert_is_FunTypeNode(n) asserteq(assertnotnull(n)->kind,NFunType)

// <type>* as_<type>(Node* n)
// const <type>* as_<type>(const Node* n)
#define as_BadNode(n) ({ assert_is_BadNode(n); (BadNode*)(n); })
#define as_PkgNode(n) ({ assert_is_PkgNode(n); (PkgNode*)(n); })
#define as_FileNode(n) ({ assert_is_FileNode(n); (FileNode*)(n); })
#define as_CommentNode(n) ({ assert_is_CommentNode(n); (CommentNode*)(n); })
#define as_BoolLitNode(n) ({ assert_is_BoolLitNode(n); (BoolLitNode*)(n); })
#define as_IntLitNode(n) ({ assert_is_IntLitNode(n); (IntLitNode*)(n); })
#define as_FloatLitNode(n) ({ assert_is_FloatLitNode(n); (FloatLitNode*)(n); })
#define as_StrLitNode(n) ({ assert_is_StrLitNode(n); (StrLitNode*)(n); })
#define as_NilNode(n) ({ assert_is_NilNode(n); (NilNode*)(n); })
#define as_IdNode(n) ({ assert_is_IdNode(n); (IdNode*)(n); })
#define as_BinOpNode(n) ({ assert_is_BinOpNode(n); (BinOpNode*)(n); })
#define as_UnaryOpNode(n) ({ assert_is_UnaryOpNode(n); (UnaryOpNode*)(n); })
#define as_TupleNode(n) ({ assert_is_TupleNode(n); (TupleNode*)(n); })
#define as_ArrayNode(n) ({ assert_is_ArrayNode(n); (ArrayNode*)(n); })
#define as_BlockNode(n) ({ assert_is_BlockNode(n); (BlockNode*)(n); })
#define as_FunNode(n) ({ assert_is_FunNode(n); (FunNode*)(n); })
#define as_MacroNode(n) ({ assert_is_MacroNode(n); (MacroNode*)(n); })
#define as_CallNode(n) ({ assert_is_CallNode(n); (CallNode*)(n); })
#define as_TypeCastNode(n) ({ assert_is_TypeCastNode(n); (TypeCastNode*)(n); })
#define as_FieldNode(n) ({ assert_is_FieldNode(n); (FieldNode*)(n); })
#define as_VarNode(n) ({ assert_is_VarNode(n); (VarNode*)(n); })
#define as_RefNode(n) ({ assert_is_RefNode(n); (RefNode*)(n); })
#define as_NamedValNode(n) ({ assert_is_NamedValNode(n); (NamedValNode*)(n); })
#define as_SelectorNode(n) ({ assert_is_SelectorNode(n); (SelectorNode*)(n); })
#define as_IndexNode(n) ({ assert_is_IndexNode(n); (IndexNode*)(n); })
#define as_SliceNode(n) ({ assert_is_SliceNode(n); (SliceNode*)(n); })
#define as_IfNode(n) ({ assert_is_IfNode(n); (IfNode*)(n); })
#define as_BasicTypeNode(n) ({ assert_is_BasicTypeNode(n); (BasicTypeNode*)(n); })
#define as_ArrayTypeNode(n) ({ assert_is_ArrayTypeNode(n); (ArrayTypeNode*)(n); })
#define as_TupleTypeNode(n) ({ assert_is_TupleTypeNode(n); (TupleTypeNode*)(n); })
#define as_StructTypeNode(n) ({ assert_is_StructTypeNode(n); (StructTypeNode*)(n); })
#define as_FunTypeNode(n) ({ assert_is_FunTypeNode(n); (FunTypeNode*)(n); })
#define as_Node(n) _Generic((n), const BadNode*:(const Node*)(n), BadNode*:(Node*)(n), \
  const PkgNode*:(const Node*)(n), PkgNode*:(Node*)(n), \
  const FileNode*:(const Node*)(n), FileNode*:(Node*)(n), \
  const struct CUnitNode*:(const Node*)(n), struct CUnitNode*:(Node*)(n), \
  const CommentNode*:(const Node*)(n), CommentNode*:(Node*)(n), \
  const Stmt*:(const Node*)(n), Stmt*:(Node*)(n), const BoolLitNode*:(const Node*)(n), \
  BoolLitNode*:(Node*)(n), const IntLitNode*:(const Node*)(n), IntLitNode*:(Node*)(n), \
  const FloatLitNode*:(const Node*)(n), FloatLitNode*:(Node*)(n), \
  const StrLitNode*:(const Node*)(n), StrLitNode*:(Node*)(n), \
  const NilNode*:(const Node*)(n), NilNode*:(Node*)(n), \
  const struct LitExpr*:(const Node*)(n), struct LitExpr*:(Node*)(n), \
  const IdNode*:(const Node*)(n), IdNode*:(Node*)(n), const BinOpNode*:(const Node*)(n), \
  BinOpNode*:(Node*)(n), const UnaryOpNode*:(const Node*)(n), UnaryOpNode*:(Node*)(n), \
  const TupleNode*:(const Node*)(n), TupleNode*:(Node*)(n), \
  const ArrayNode*:(const Node*)(n), ArrayNode*:(Node*)(n), \
  const BlockNode*:(const Node*)(n), BlockNode*:(Node*)(n), \
  const struct ListExpr*:(const Node*)(n), struct ListExpr*:(Node*)(n), \
  const FunNode*:(const Node*)(n), FunNode*:(Node*)(n), \
  const MacroNode*:(const Node*)(n), MacroNode*:(Node*)(n), \
  const CallNode*:(const Node*)(n), CallNode*:(Node*)(n), \
  const TypeCastNode*:(const Node*)(n), TypeCastNode*:(Node*)(n), \
  const FieldNode*:(const Node*)(n), FieldNode*:(Node*)(n), \
  const VarNode*:(const Node*)(n), VarNode*:(Node*)(n), const RefNode*:(const Node*)(n), \
  RefNode*:(Node*)(n), const NamedValNode*:(const Node*)(n), NamedValNode*:(Node*)(n), \
  const SelectorNode*:(const Node*)(n), SelectorNode*:(Node*)(n), \
  const IndexNode*:(const Node*)(n), IndexNode*:(Node*)(n), \
  const SliceNode*:(const Node*)(n), SliceNode*:(Node*)(n), \
  const IfNode*:(const Node*)(n), IfNode*:(Node*)(n), const Expr*:(const Node*)(n), \
  Expr*:(Node*)(n), const BasicTypeNode*:(const Node*)(n), BasicTypeNode*:(Node*)(n), \
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
  default: ({ assert_is_Stmt(n); (Stmt*)(n); }))

#define as_CUnitNode(n) _Generic((n), const PkgNode*:(const struct CUnitNode*)(n), \
  PkgNode*:(struct CUnitNode*)(n), const FileNode*:(const struct CUnitNode*)(n), \
  FileNode*:(struct CUnitNode*)(n), \
  const struct CUnitNode*:(const struct CUnitNode*)(n), \
  struct CUnitNode*:(struct CUnitNode*)(n), \
  default: ({ assert_is_CUnitNode(n); (struct CUnitNode*)(n); }))

#define as_Expr(n) _Generic((n), const BoolLitNode*:(const Expr*)(n), \
  BoolLitNode*:(Expr*)(n), const IntLitNode*:(const Expr*)(n), IntLitNode*:(Expr*)(n), \
  const FloatLitNode*:(const Expr*)(n), FloatLitNode*:(Expr*)(n), \
  const StrLitNode*:(const Expr*)(n), StrLitNode*:(Expr*)(n), \
  const NilNode*:(const Expr*)(n), NilNode*:(Expr*)(n), \
  const struct LitExpr*:(const Expr*)(n), struct LitExpr*:(Expr*)(n), \
  const IdNode*:(const Expr*)(n), IdNode*:(Expr*)(n), const BinOpNode*:(const Expr*)(n), \
  BinOpNode*:(Expr*)(n), const UnaryOpNode*:(const Expr*)(n), UnaryOpNode*:(Expr*)(n), \
  const TupleNode*:(const Expr*)(n), TupleNode*:(Expr*)(n), \
  const ArrayNode*:(const Expr*)(n), ArrayNode*:(Expr*)(n), \
  const BlockNode*:(const Expr*)(n), BlockNode*:(Expr*)(n), \
  const struct ListExpr*:(const Expr*)(n), struct ListExpr*:(Expr*)(n), \
  const FunNode*:(const Expr*)(n), FunNode*:(Expr*)(n), \
  const MacroNode*:(const Expr*)(n), MacroNode*:(Expr*)(n), \
  const CallNode*:(const Expr*)(n), CallNode*:(Expr*)(n), \
  const TypeCastNode*:(const Expr*)(n), TypeCastNode*:(Expr*)(n), \
  const FieldNode*:(const Expr*)(n), FieldNode*:(Expr*)(n), \
  const VarNode*:(const Expr*)(n), VarNode*:(Expr*)(n), const RefNode*:(const Expr*)(n), \
  RefNode*:(Expr*)(n), const NamedValNode*:(const Expr*)(n), NamedValNode*:(Expr*)(n), \
  const SelectorNode*:(const Expr*)(n), SelectorNode*:(Expr*)(n), \
  const IndexNode*:(const Expr*)(n), IndexNode*:(Expr*)(n), \
  const SliceNode*:(const Expr*)(n), SliceNode*:(Expr*)(n), \
  const IfNode*:(const Expr*)(n), IfNode*:(Expr*)(n), const Expr*:(const Expr*)(n), \
  Expr*:(Expr*)(n), default: ({ assert_is_Expr(n); (Expr*)(n); }))

#define as_LitExpr(n) _Generic((n), const BoolLitNode*:(const struct LitExpr*)(n), \
  BoolLitNode*:(struct LitExpr*)(n), const IntLitNode*:(const struct LitExpr*)(n), \
  IntLitNode*:(struct LitExpr*)(n), const FloatLitNode*:(const struct LitExpr*)(n), \
  FloatLitNode*:(struct LitExpr*)(n), const StrLitNode*:(const struct LitExpr*)(n), \
  StrLitNode*:(struct LitExpr*)(n), const NilNode*:(const struct LitExpr*)(n), \
  NilNode*:(struct LitExpr*)(n), const struct LitExpr*:(const struct LitExpr*)(n), \
  struct LitExpr*:(struct LitExpr*)(n), \
  default: ({ assert_is_LitExpr(n); (struct LitExpr*)(n); }))

#define as_ListExpr(n) _Generic((n), const TupleNode*:(const struct ListExpr*)(n), \
  TupleNode*:(struct ListExpr*)(n), const ArrayNode*:(const struct ListExpr*)(n), \
  ArrayNode*:(struct ListExpr*)(n), const BlockNode*:(const struct ListExpr*)(n), \
  BlockNode*:(struct ListExpr*)(n), const struct ListExpr*:(const struct ListExpr*)(n), \
  struct ListExpr*:(struct ListExpr*)(n), \
  default: ({ assert_is_ListExpr(n); (struct ListExpr*)(n); }))

#define as_Type(n) _Generic((n), const BasicTypeNode*:(const Type*)(n), \
  BasicTypeNode*:(Type*)(n), const ArrayTypeNode*:(const Type*)(n), \
  ArrayTypeNode*:(Type*)(n), const TupleTypeNode*:(const Type*)(n), \
  TupleTypeNode*:(Type*)(n), const StructTypeNode*:(const Type*)(n), \
  StructTypeNode*:(Type*)(n), const FunTypeNode*:(const Type*)(n), \
  FunTypeNode*:(Type*)(n), const Type*:(const Type*)(n), Type*:(Type*)(n), \
  default: ({ assert_is_Type(n); (Type*)(n); }))

union NodeUnion {
  BadNode _0; PkgNode _1; FileNode _2; CommentNode _3; BoolLitNode _4;
  IntLitNode _5; FloatLitNode _6; StrLitNode _7; NilNode _8; IdNode _9;
  BinOpNode _10; UnaryOpNode _11; TupleNode _12; ArrayNode _13; BlockNode _14;
  FunNode _15; MacroNode _16; CallNode _17; TypeCastNode _18; FieldNode _19;
  VarNode _20; RefNode _21; NamedValNode _22; SelectorNode _23; IndexNode _24;
  SliceNode _25; IfNode _26; BasicTypeNode _27; ArrayTypeNode _28;
  TupleTypeNode _29; StructTypeNode _30; FunTypeNode _31;
};

//END GENERATED CODE

// keep the size of nodes in check. Update this if needed.
static_assert(sizeof(union NodeUnion) == 96, "AST size changed");

// NodeKindName returns a printable name. E.g. NBad => "Bad"
const char* NodeKindName(NodeKind);

inline static bool NodeIsPrimitiveConst(const Node* n) {
  return n->kind == NNil || n->kind == NBasicType || n->kind == NBoolLit;
}

inline static Node* nullable NodeAlloc(Mem mem) {
  return (Node*)memalloc(mem, sizeof(union NodeUnion));
}

Node* NodeInit(Node* n, NodeKind kind);

inline static Node* nullable NodeCopy(Mem mem, const Node* n) {
  Node* n2 = NodeAlloc(mem);
  if (n2) {
    memcpy(n2, n, sizeof(Node));
    // TODO: Field and Var are reference counted, for IdNode maybe update target->nref?
  }
  return n2;
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

Scope* nullable ScopeNew(Mem mem, const Scope* nullable parent);
void ScopeFree(Scope*, Mem mem);
const Node* nullable ScopeLookup(const Scope*, Sym);

// ScopeAssoc associates key with *valuep_inout.
// On return, sets *valuep_inout to a replaced node or NULL if no existing node was found.
// Returns an error if memory allocation failed during growth of the hash table.
WARN_UNUSED_RESULT
inline static error ScopeAssoc(Scope* s, Sym key, Node** valuep_inout) {
  return SymMapSet(&s->bindings, key, (void**)valuep_inout);
}

ASSUME_NONNULL_END
