// AST nodes
ASSUME_NONNULL_BEGIN

typedef struct Node    Node;      // AST node, basis for Stmt, Expr and Type
typedef struct Stmt    Stmt;      // AST statement
typedef struct Expr    Expr;      // AST expression
typedef struct LitExpr LitExpr; // AST constant literal expression
typedef struct Type    Type;      // AST type
typedef u8             NodeKind;  // AST node kind (NNone, NBad, NBoolLit ...)
typedef u16            NodeFlags; // NF_* constants; AST node flags (Unresolved, Const ...)

DEF_TYPED_ARRAY(NodeArray, Node*)

struct Node {
  void* nullable irval;  // used by IR builders for temporary storage
  Pos            pos;    // source origin & position
  Pos            endpos; // NoPos means "only use pos".
  NodeFlags      flags;  // flags describe meta attributes of the node
  NodeKind       kind;   // kind of node (e.g. NId)
};

struct Stmt { Node;
};
struct Expr { Node;
  Type* nullable type; // value type. NULL if unknown.
};
struct LitExpr { Expr;
};
struct Type { Node;
  TypeFlags    tflags; // u16 (Note: used to be TypeKind kind)
  Sym nullable tid;    // initially NULL for user-defined types, computed as needed
};

struct BadNode { Stmt; }; // substitute "filler" for invalid syntax
struct PkgNode { Stmt;
  const Str       name;         // reference to str in corresponding Pkg struct
  Scope* nullable scope;
  NodeArray       a;            // array of nodes
  Node*           a_storage[4]; // in-struct storage for the first few entries of a
};
struct FileNode { Stmt;
  const Str       name;         // reference to str in corresponding Source struct
  Scope* nullable scope;
  NodeArray       a;            // array of nodes
  Node*           a_storage[4]; // in-struct storage for the first few entries of a
};
struct CommentNode { Stmt;
  u32       len;
  const u8* ptr;
};

// expressions (literal constants)
struct BoolLitNode  { LitExpr; u64 ival; }; // boolean literal
struct IntLitNode   { LitExpr; u64 ival; }; // integer literal
struct FloatLitNode { LitExpr; f64 fval; }; // floating-point literal
struct StrLitNode   { LitExpr; Str sval; }; // string literal
struct NilNode      { LitExpr; };           // the nil atom

// expressions
struct IdNode { Expr;
  Sym   name;
  Node* target;
};
struct BinOpNode { Expr;
  Tok   op;
  Node* left;
  Node* right;
};
struct UnaryOpNode { Expr; // used for NPrefixOp, NPostfixOp, NReturn, NAssign
  Tok   op;
  Node* expr;
};;
struct ArrayNode { Expr; // used for NTuple, NBlock, NArray
  NodeArray a;            // array of nodes
  Node*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct FunNode { Expr;
  Node* nullable params;  // input params (NTuple or NULL if none)
  Node* nullable result;  // output results (NTuple | NExpr)
  Sym   nullable name;    // NULL for lambda
  Node* nullable body;    // NULL for fun-declaration
};
struct MacroNode { Expr;
  Node* nullable params;  // input params (NTuple or NULL if none)
  Sym   nullable name;
  Node*          template;
};
struct CallNode { Expr;
  Node* receiver;      // Fun or Id
  Node* nullable args; // NULL if there are no args, else a NTuple
};
struct TypeCastNode { Expr;
  Node* receiver;      // Type or Id
  Node* nullable args; // NULL if there are no args, else a NTuple
};
struct FieldNode { Expr;
  u32            nrefs; // reference count
  u32            index; // argument index or struct index
  Sym            name;
  Node* nullable init;  // initial value (may be NULL)
};
struct VarNode { Expr;
  bool           isconst; // immutable storage? (true for "const x" vars)
  u32            nrefs;   // reference count
  u32            index;   // argument index (used by function parameters)
  Sym            name;
  Node* nullable init;    // initial/default value
};
struct RefNode { Expr;
  Node* target;
};
struct NamedValNode { Expr;
  Sym   name;
  Node* value;
};
struct SelectorNode { Expr; // Selector = Expr "." ( Ident | Selector )
  Node*    operand;
  Sym      member;  // id
  U32Array indices; // GEP index path
  u32      indices_st[4]; // indices storage
};
struct IndexNode { Expr; // Index = Expr "[" Expr "]"
  Node* operand;
  Node* indexexpr;
  u32   index; // 0xffffffff if indexexpr is not a compile-time constant
};
struct SliceNode { Expr; // Slice = Expr "[" Expr? ":" Expr? "]"
  Node*          operand;
  Node* nullable start;
  Node* nullable end;
};
struct IfNode { Expr;
  Node*          cond;
  Node*          thenb;
  Node* nullable elseb; // NULL or expr
};

// types
struct BasicTypeNode { Type;
  TypeCode typeCode;
  Sym      name;
};
struct ArrayTypeNode { Type;
  Node* nullable sizeexpr; // NULL for inferred types
  u32            size;     // used for array. 0 until sizeexpr is resolved
  Node*          subtype;
};
struct TupleTypeNode { Type;
  NodeArray a;            // Node[]
  Node*     a_storage[4]; // in-struct storage for the first few elements
};
struct StructTypeNode { Type;
  Sym nullable name;         // NULL for anonymous structs
  NodeArray    a;            // NField[]
  Node*        a_storage[3]; // in-struct storage for the first few fields
};
struct FunTypeNode { Type;
  Node* nullable params; // NTuple of NVar or null if no params
  Type* nullable result; // NTupleType of types or single type
};


enum NodeFlags {
  NF_Unresolved  = 1 << 0, // contains unresolved references. MUST BE VALUE 1!
  NF_Const       = 1 << 1, // constant; value known at compile time (comptime)
  NF_Base        = 1 << 2, // [struct field] the field is a base of the struct
  NF_RValue      = 1 << 3, // resolved as rvalue
  NF_Param       = 1 << 4, // [Var] function parameter
  NF_MacroParam  = 1 << 5, // [Var] macro parameter
  NF_Unused      = 1 << 6, // [Var] never referenced
  NF_Public      = 1 << 7, // [Var|Fun] public visibility (aka published, exported)
  NF_Named       = 1 << 8, // [Tuple when used as args] has named argument
  TF_PartialType = 1 << 9, // Type resolver should visit even if the node is typed
  // Changing this? Remember to update NodeFlagsStr impl
} END_TYPED_ENUM(NodeFlags)


//BEGIN GENERATED CODE by ast_gen.py

enum NodeKind {
  NStmt_BEG      =  0,
    NBad         =  0, // struct BadNode
    NPkg         =  1, // struct PkgNode
    NFile        =  2, // struct FileNode
    NComment     =  3, // struct CommentNode
  NStmt_END      =  3,
  NExpr_BEG      =  4,
    NLitExpr_BEG =  4,
      NBoolLit   =  4, // struct BoolLitNode
      NIntLit    =  5, // struct IntLitNode
      NFloatLit  =  6, // struct FloatLitNode
      NStrLit    =  7, // struct StrLitNode
      NNil       =  8, // struct NilNode
    NLitExpr_END =  8,
    NId          =  9, // struct IdNode
    NBinOp       = 10, // struct BinOpNode
    NUnaryOp     = 11, // struct UnaryOpNode
    NArray       = 12, // struct ArrayNode
    NFun         = 13, // struct FunNode
    NMacro       = 14, // struct MacroNode
    NCall        = 15, // struct CallNode
    NTypeCast    = 16, // struct TypeCastNode
    NField       = 17, // struct FieldNode
    NVar         = 18, // struct VarNode
    NRef         = 19, // struct RefNode
    NNamedVal    = 20, // struct NamedValNode
    NSelector    = 21, // struct SelectorNode
    NIndex       = 22, // struct IndexNode
    NSlice       = 23, // struct SliceNode
    NIf          = 24, // struct IfNode
  NExpr_END      = 24,
  NType_BEG      = 25,
    NBasicType   = 25, // struct BasicTypeNode
    NArrayType   = 26, // struct ArrayTypeNode
    NTupleType   = 27, // struct TupleTypeNode
    NStructType  = 28, // struct StructTypeNode
    NFunType     = 29, // struct FunTypeNode
  NType_END      = 29,
} END_TYPED_ENUM(NodeKind)

// NodeKindName returns a printable name. E.g. NBad => "Bad"
const char* NodeKindName(NodeKind);

// bool NodeKindIs<kind>(NodeKind)
#define NodeKindIsStmt(nkind) ((int)(nkind)-NStmt_BEG <= (int)NStmt_END-NStmt_BEG)
#define NodeKindIsExpr(nkind) ((int)(nkind)-NExpr_BEG <= (int)NExpr_END-NExpr_BEG)
#define NodeKindIsLitExpr(nkind) ((int)(nkind)-NLitExpr_BEG <= (int)NLitExpr_END-NLitExpr_BEG)
#define NodeKindIsType(nkind) ((int)(nkind)-NType_BEG <= (int)NType_END-NType_BEG)

// bool NodeIs<kind>(const Node*)
#define NodeIsStmt(n) NodeKindIsStmt((n)->kind)
#define NodeIsExpr(n) NodeKindIsExpr((n)->kind)
#define NodeIsLitExpr(n) NodeKindIsLitExpr((n)->kind)
#define NodeIsType(n) NodeKindIsType((n)->kind)
#define NodeIsBad(n) ((n)->kind==NBad)
#define NodeIsPkg(n) ((n)->kind==NPkg)
#define NodeIsFile(n) ((n)->kind==NFile)
#define NodeIsComment(n) ((n)->kind==NComment)
#define NodeIsBoolLit(n) ((n)->kind==NBoolLit)
#define NodeIsIntLit(n) ((n)->kind==NIntLit)
#define NodeIsFloatLit(n) ((n)->kind==NFloatLit)
#define NodeIsStrLit(n) ((n)->kind==NStrLit)
#define NodeIsNil(n) ((n)->kind==NNil)
#define NodeIsId(n) ((n)->kind==NId)
#define NodeIsBinOp(n) ((n)->kind==NBinOp)
#define NodeIsUnaryOp(n) ((n)->kind==NUnaryOp)
#define NodeIsArray(n) ((n)->kind==NArray)
#define NodeIsFun(n) ((n)->kind==NFun)
#define NodeIsMacro(n) ((n)->kind==NMacro)
#define NodeIsCall(n) ((n)->kind==NCall)
#define NodeIsTypeCast(n) ((n)->kind==NTypeCast)
#define NodeIsField(n) ((n)->kind==NField)
#define NodeIsVar(n) ((n)->kind==NVar)
#define NodeIsRef(n) ((n)->kind==NRef)
#define NodeIsNamedVal(n) ((n)->kind==NNamedVal)
#define NodeIsSelector(n) ((n)->kind==NSelector)
#define NodeIsIndex(n) ((n)->kind==NIndex)
#define NodeIsSlice(n) ((n)->kind==NSlice)
#define NodeIsIf(n) ((n)->kind==NIf)
#define NodeIsBasicType(n) ((n)->kind==NBasicType)
#define NodeIsArrayType(n) ((n)->kind==NArrayType)
#define NodeIsTupleType(n) ((n)->kind==NTupleType)
#define NodeIsStructType(n) ((n)->kind==NStructType)
#define NodeIsFunType(n) ((n)->kind==NFunType)

// void NodeAssert<kind>(const Node*)
#define NodeAssertStmt(n) assertf(NodeKindIsStmt((n)->kind),"%d",(n)->kind)
#define NodeAssertExpr(n) assertf(NodeKindIsExpr((n)->kind),"%d",(n)->kind)
#define NodeAssertLitExpr(n) assertf(NodeKindIsLitExpr((n)->kind),"%d",(n)->kind)
#define NodeAssertType(n) assertf(NodeKindIsType((n)->kind),"%d",(n)->kind)
#define NodeAssertBad(n) assertf((n)->kind==NBad,"%d",(n)->kind)
#define NodeAssertPkg(n) assertf((n)->kind==NPkg,"%d",(n)->kind)
#define NodeAssertFile(n) assertf((n)->kind==NFile,"%d",(n)->kind)
#define NodeAssertComment(n) assertf((n)->kind==NComment,"%d",(n)->kind)
#define NodeAssertBoolLit(n) assertf((n)->kind==NBoolLit,"%d",(n)->kind)
#define NodeAssertIntLit(n) assertf((n)->kind==NIntLit,"%d",(n)->kind)
#define NodeAssertFloatLit(n) assertf((n)->kind==NFloatLit,"%d",(n)->kind)
#define NodeAssertStrLit(n) assertf((n)->kind==NStrLit,"%d",(n)->kind)
#define NodeAssertNil(n) assertf((n)->kind==NNil,"%d",(n)->kind)
#define NodeAssertId(n) assertf((n)->kind==NId,"%d",(n)->kind)
#define NodeAssertBinOp(n) assertf((n)->kind==NBinOp,"%d",(n)->kind)
#define NodeAssertUnaryOp(n) assertf((n)->kind==NUnaryOp,"%d",(n)->kind)
#define NodeAssertArray(n) assertf((n)->kind==NArray,"%d",(n)->kind)
#define NodeAssertFun(n) assertf((n)->kind==NFun,"%d",(n)->kind)
#define NodeAssertMacro(n) assertf((n)->kind==NMacro,"%d",(n)->kind)
#define NodeAssertCall(n) assertf((n)->kind==NCall,"%d",(n)->kind)
#define NodeAssertTypeCast(n) assertf((n)->kind==NTypeCast,"%d",(n)->kind)
#define NodeAssertField(n) assertf((n)->kind==NField,"%d",(n)->kind)
#define NodeAssertVar(n) assertf((n)->kind==NVar,"%d",(n)->kind)
#define NodeAssertRef(n) assertf((n)->kind==NRef,"%d",(n)->kind)
#define NodeAssertNamedVal(n) assertf((n)->kind==NNamedVal,"%d",(n)->kind)
#define NodeAssertSelector(n) assertf((n)->kind==NSelector,"%d",(n)->kind)
#define NodeAssertIndex(n) assertf((n)->kind==NIndex,"%d",(n)->kind)
#define NodeAssertSlice(n) assertf((n)->kind==NSlice,"%d",(n)->kind)
#define NodeAssertIf(n) assertf((n)->kind==NIf,"%d",(n)->kind)
#define NodeAssertBasicType(n) assertf((n)->kind==NBasicType,"%d",(n)->kind)
#define NodeAssertArrayType(n) assertf((n)->kind==NArrayType,"%d",(n)->kind)
#define NodeAssertTupleType(n) assertf((n)->kind==NTupleType,"%d",(n)->kind)
#define NodeAssertStructType(n) assertf((n)->kind==NStructType,"%d",(n)->kind)
#define NodeAssertFunType(n) assertf((n)->kind==NFunType,"%d",(n)->kind)

// <type>* as_<type>(Node* n)
#define as_Stmt(n) ({ NodeAssertStmt(n); (Stmt*)(n); })
#define as_Expr(n) ({ NodeAssertExpr(n); (Expr*)(n); })
#define as_LitExpr(n) ({ NodeAssertLitExpr(n); (LitExpr*)(n); })
#define as_Type(n) ({ NodeAssertType(n); (Type*)(n); })
#define as_BadNode(n) ({ NodeAssertBad(n); (BadNode*)(n); })
#define as_PkgNode(n) ({ NodeAssertPkg(n); (PkgNode*)(n); })
#define as_FileNode(n) ({ NodeAssertFile(n); (FileNode*)(n); })
#define as_CommentNode(n) ({ NodeAssertComment(n); (CommentNode*)(n); })
#define as_BoolLitNode(n) ({ NodeAssertBoolLit(n); (BoolLitNode*)(n); })
#define as_IntLitNode(n) ({ NodeAssertIntLit(n); (IntLitNode*)(n); })
#define as_FloatLitNode(n) ({ NodeAssertFloatLit(n); (FloatLitNode*)(n); })
#define as_StrLitNode(n) ({ NodeAssertStrLit(n); (StrLitNode*)(n); })
#define as_NilNode(n) ({ NodeAssertNil(n); (NilNode*)(n); })
#define as_IdNode(n) ({ NodeAssertId(n); (IdNode*)(n); })
#define as_BinOpNode(n) ({ NodeAssertBinOp(n); (BinOpNode*)(n); })
#define as_UnaryOpNode(n) ({ NodeAssertUnaryOp(n); (UnaryOpNode*)(n); })
#define as_ArrayNode(n) ({ NodeAssertArray(n); (ArrayNode*)(n); })
#define as_FunNode(n) ({ NodeAssertFun(n); (FunNode*)(n); })
#define as_MacroNode(n) ({ NodeAssertMacro(n); (MacroNode*)(n); })
#define as_CallNode(n) ({ NodeAssertCall(n); (CallNode*)(n); })
#define as_TypeCastNode(n) ({ NodeAssertTypeCast(n); (TypeCastNode*)(n); })
#define as_FieldNode(n) ({ NodeAssertField(n); (FieldNode*)(n); })
#define as_VarNode(n) ({ NodeAssertVar(n); (VarNode*)(n); })
#define as_RefNode(n) ({ NodeAssertRef(n); (RefNode*)(n); })
#define as_NamedValNode(n) ({ NodeAssertNamedVal(n); (NamedValNode*)(n); })
#define as_SelectorNode(n) ({ NodeAssertSelector(n); (SelectorNode*)(n); })
#define as_IndexNode(n) ({ NodeAssertIndex(n); (IndexNode*)(n); })
#define as_SliceNode(n) ({ NodeAssertSlice(n); (SliceNode*)(n); })
#define as_IfNode(n) ({ NodeAssertIf(n); (IfNode*)(n); })
#define as_BasicTypeNode(n) ({ NodeAssertBasicType(n); (BasicTypeNode*)(n); })
#define as_ArrayTypeNode(n) ({ NodeAssertArrayType(n); (ArrayTypeNode*)(n); })
#define as_TupleTypeNode(n) ({ NodeAssertTupleType(n); (TupleTypeNode*)(n); })
#define as_StructTypeNode(n) ({ NodeAssertStructType(n); (StructTypeNode*)(n); })
#define as_FunTypeNode(n) ({ NodeAssertFunType(n); (FunTypeNode*)(n); })

//END GENERATED CODE

ASSUME_NONNULL_END
