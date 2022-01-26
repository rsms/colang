#pragma once
#include "../coimpl.h"
#include "../mem.h"
#include "../array.h"
#include "../str.h"
#include "../sym.h"

ASSUME_NONNULL_BEGIN

typedef struct BuildCtx   BuildCtx;
typedef struct Parser     Parser;     // parser state (includes Scanner)
typedef u8                ParseFlags; // flags for changing parser behavior
typedef struct Scanner    Scanner;    // lexical scanner state
typedef struct Diagnostic Diagnostic; // diagnostic message
typedef u8                DiagLevel;  // diagnostic level (Error, Warn ...)
typedef struct Comment    Comment;    // source comment
typedef struct Indent     Indent;     // source indentation
typedef struct Scope      Scope;      // lexical scope
typedef struct Pkg        Pkg;        // logical package, a unit of sources
typedef struct Source     Source;     // an input source file

// Pos is a compact representation of a source position: source file, line and column.
// Limits: 1048575 number of sources, 1048575 max lines, 4095 max columns, 4095 max width.
// Inspired by the Go compiler's xpos & lico.
typedef u64            Pos;
typedef struct PosMap  PosMap;  // maps Source to Pos indices
typedef struct PosSpan PosSpan; // span in a Source

// AST types
typedef u16         Tok;       // language tokens (produced by Scanner)
typedef struct Node Node;      // AST node, basis for Stmt, Expr and Type
typedef struct Stmt Stmt;      // AST statement
typedef struct Expr Expr;      // AST expression
typedef struct Type Type;      // AST type
typedef u8          NodeKind;  // AST node kind (NNone, NBad, NBoolLit ...)
typedef u16         NodeFlags; // NF_* constants; AST node flags (Unresolved, Const ...)
typedef u8          TypeCode;  // TC_* constants
typedef u16         TypeFlags; // TF_* constants (enum TypeFlags)
typedef u8          TypeKind;  // TF_Kind* constants (part of TypeFlags)


// Tok definitions
#define DEF_TOKENS(_)   \
  _( TNone  , "TNone" ) \
  _( TComma , ",")      \
  _( TSemi  , ";")      \
  _( TColon , ":")      \
  \
  _( T_PRIM_OPS_START , "") \
  /* primary "intrinsic" operator tokens, most of them mapping directly to IR ops */ \
  _( TPlus          , "+")  \
  _( TMinus         , "-")  \
  _( TStar          , "*")  \
  _( TSlash         , "/")  \
  _( TPercent       , "%")  \
  _( TShl           , "<<") \
  _( TShr           , ">>") \
  _( TAnd           , "&")  \
  _( TPipe          , "|")  \
  _( THat           , "^")  \
  _( TTilde         , "~")  \
  _( TExcalm        , "!")  \
  /* binary comparison ops (IR builder assume these are packed!) */ \
  _( TEq            , "==") /* must be first */ \
  _( TNEq           , "!=") \
  _( TLt            , "<")  \
  _( TLEq           , "<=") \
  _( TGt            , ">")  \
  _( TGEq           , ">=") /* must be last */ \
  /* unary ops */ \
  _( TPlusPlus      , "++") \
  _( TMinusMinus    , "--") \
  \
  _( T_PRIM_OPS_END , "") /* end of operator tokens */ \
  \
  _( TAssign        , "=")   \
  _( TShlAssign     , "<<=") \
  _( TShrAssign     , ">>=") \
  _( TPlusAssign    , "+=")  \
  _( TMinusAssign   , "-=")  \
  _( TStarAssign    , "*=")  \
  _( TSlashAssign   , "/=")  \
  _( TPercentAssign , "%=")  \
  _( TAndAssign     , "&=")  \
  _( TPipeAssign    , "|=")  \
  _( TTildeAssign   , "~=")  \
  _( THatAssign     , "^=")  \
  _( TLParen        , "(")   \
  _( TRParen        , ")")   \
  _( TLBrace        , "{")   \
  _( TRBrace        , "}")   \
  _( TLBrack        , "[")   \
  _( TRBrack        , "]")   \
  _( TAndAnd        , "&&")  \
  _( TPipePipe      , "||")  \
  _( TRArr          , "->")  \
  _( TDot           , ".")  \
  _( TId            , "identifier")  \
  _( TIntLit        , "int") \
  _( TFloatLit      , "float") \
// end DEF_TOKENS
#define DEF_TOKENS_KEYWORD(_) \
  _( TAs,       as )          \
  _( TAuto,     auto )        \
  _( TBreak,    break )       \
  _( TContinue, continue )    \
  _( TDefer,    defer )       \
  _( TElse,     else )        \
  _( TEnum,     enum )        \
  _( TFor,      for )         \
  _( TFun,      fun )         \
  _( TIf,       if )          \
  _( TImport,   import )      \
  _( TIn,       in )          \
  _( TNil,      nil )         \
  _( TReturn,   return )      \
  _( TStruct,   struct )      \
  _( TSwitch,   switch )      \
  _( TType,     type )        \
  _( TConst,    const )       \
  _( TMut,      mut )         \
  _( TVar,      var )         \
// end DEF_TOKENS_KEYWORD
// Limited to a total of 31 keywords. See parse_scan.c



// NodeKind for Nodes
// statements
#define DEF_NODE_KINDS_STMT(_) \
  _( NNone     ) \
  _( NBad      ) /* substitute "filler node" for invalid syntax */ \
  _( NPkg      ) \
  _( NFile     ) \
  _( NTypeType ) /* type of a type */ \
// end DEF_NODE_KINDS_STMT
// literal constants
#define DEF_NODE_KINDS_CONSTLIT(_) \
  _( NBoolLit  ) /* boolean literal */ \
  _( NIntLit   ) /* integer literal */ \
  _( NFloatLit ) /* floating-point literal */ \
  _( NStrLit   ) /* string literal */ \
  _( NNil      ) /* the nil atom */ \
// end DEF_NODE_KINDS_CONSTLIT
// expressions
#define DEF_NODE_KINDS_EXPR(_) \
  _( NAssign    ) \
  _( NBlock     ) \
  _( NCall      ) \
  _( NField     ) \
  _( NSelector  ) \
  _( NIndex     ) \
  _( NSlice     ) \
  _( NFun       ) \
  _( NId        ) \
  _( NIf        ) \
  _( NVar       ) \
  _( NRef       ) \
  _( NNamedVal  ) \
  _( NBinOp     ) \
  _( NPrefixOp  ) \
  _( NPostfixOp ) \
  _( NReturn    ) \
  _( NArray     ) \
  _( NTuple     ) \
  _( NTypeCast  ) \
  _( NMacro     ) /* TODO: different NodeClass */ \
// end DEF_NODE_KINDS_EXPR
// types
#define DEF_NODE_KINDS_TYPE(_) \
  _( NBasicType  ) /* int, bool, ... */ \
  _( NRefType    ) /* &T */ \
  _( NArrayType  ) /* [4]int, []int */ \
  _( NTupleType  ) /* (float,bool,int) */ \
  _( NStructType ) /* struct{foo float; y bool} */ \
  _( NFunType    ) /* fun(int,int)(float,bool) */ \
// end DEF_NODE_KINDS_TYPE


// TypeCode identifies all types.
//
// The following is generated for all type codes:
//   enum TypeCode { TC_##name ... }
//   const Sym sym_##name
//   Type*     kType_##name
//
// Additionally, entries in DEF_TYPE_CODES_*_PUB are included in universe_syms()
//
// basic: housed in NBasicType, named & exported in the global scope
#define DEF_TYPE_CODES_BASIC_PUB(_)/* (name, char encoding, TypeFlags)                    */\
  _( bool      , 'b' , TF_KindBool )                                                       \
  _( i8        , '1' , TF_KindInt | TF_Size1 | TF_Signed )                                 \
  _( u8        , '2' , TF_KindInt | TF_Size1 )                                             \
  _( i16       , '3' , TF_KindInt | TF_Size2 | TF_Signed )                                 \
  _( u16       , '4' , TF_KindInt | TF_Size2 )                                             \
  _( i32       , '5' , TF_KindInt | TF_Size4 | TF_Signed )                                 \
  _( u32       , '6' , TF_KindInt | TF_Size4 )                                             \
  _( i64       , '7' , TF_KindInt | TF_Size8 | TF_Signed )                                 \
  _( u64       , '8' , TF_KindInt | TF_Size8 )                                             \
  _( f32       , 'f' , TF_KindF32 | TF_Size4 | TF_Signed )                                 \
  _( f64       , 'F' , TF_KindF64 | TF_Size8 | TF_Signed )                                 \
  _( int       , 'i' , TF_KindInt            | TF_Signed )                                 \
  _( uint      , 'u' , TF_KindInt )                                                        \
// end DEF_TYPE_CODES_BASIC_PUB
#define DEF_TYPE_CODES_BASIC(_)                                                            \
  _( nil       , '0' , TF_KindVoid )                                                       \
  _( ideal     , '*' , TF_KindVoid )/* type of const literal                             */\
// end DEF_TYPE_CODES_BASIC
#define DEF_TYPE_CODES_PUB(_)                                                              \
  _( str       , 's' , TF_KindPointer )                                                    \
  _( auto      , 'a' , TF_KindVoid    ) /* inferred                                      */\
// end DEF_TYPE_CODES_PUB
#define DEF_TYPE_CODES_ETC(_)                                                              \
  _( ref       , '&' , TF_KindPointer ) /* pointer memory address                        */\
  _( fun       , '^' , TF_KindFunc )                                                       \
  _( array     , '[' , TF_KindArray )                                                      \
  _( struct    , '{' , TF_KindStruct )                                                     \
  _( structEnd , '}' , TF_KindVoid )                                                       \
  _( tuple     , '(' , TF_KindArray )                                                      \
  _( tupleEnd  , ')' , TF_KindVoid )                                                       \
  _( param1    , 'P' , TF_KindVoid ) /* IR parameter, matches other type (output==input) */\
  _( param2    , 'P' , TF_KindVoid )
// end DEF_TYPE_CODES_ETC


// predefined named constant AST Nodes. Generated Syms:
//   const Sym sym_##name
//   Node*     kNode_##name
#define DEF_CONST_NODES_PUB(_) /* (name, NodeKind, typecode_suffix, int value) */ \
  _( true,  NBoolLit, bool, 1 ) \
  _( false, NBoolLit, bool, 0 ) \
  _( nil,   NNil,     nil, 0 ) \
// end DEF_CONST_NODES_PUB


// predefined additional symbols
//   const Sym sym_##name
#define DEF_SYMS_PUB(X) /* (name) */ \
  X( _ ) \
// end DEF_SYMS_PUB


enum Tok {
  #define I_ENUM(name, _str) name,

  DEF_TOKENS(I_ENUM)

  // TKeywordsStart is used for 0-based keyword indexing.
  // Its explicit value is used by sym.c to avoid having to regenerate keyword symbols
  // whenever a non-keyword token is added. I.e. this number can be changed freely but will
  // require regeneration of the code in sym.c.
  TKeywordsStart = 0x100,
  DEF_TOKENS_KEYWORD(I_ENUM)
  TKeywordsEnd,

  #undef I_ENUM
} END_TYPED_ENUM(Tok)
// We only have 5 bits to encode tokens in Sym. Additionally, the value 0 is reserved
// for "not a keyword", leaving the max number of values at 31 (i.e. 2^5=32-1).
static_assert(TKeywordsEnd - TKeywordsStart < 32, "too many keywords");


enum NodeKind {
  #define I_ENUM(name) name,

  DEF_NODE_KINDS_STMT(I_ENUM)
  NodeKind_END_STMT,

  NodeKind_START_CONSTLIT = NodeKind_END_STMT,
  DEF_NODE_KINDS_CONSTLIT(I_ENUM)
  NodeKind_END_CONSTLIT,

  NodeKind_START_EXPR = NodeKind_END_CONSTLIT,
  DEF_NODE_KINDS_EXPR(I_ENUM)
  NodeKind_END_EXPR,

  NodeKind_START_TYPE = NodeKind_END_EXPR,
  DEF_NODE_KINDS_TYPE(I_ENUM)

  #undef I_ENUM
} END_TYPED_ENUM(NodeKind)


// TypeCode identifies all basic types
enum TypeCode {
  #define _(name, _encoding, _flags) TC_##name,
  // IMPORTANT: order of macro invocations must match _TypeCodeEncodingMap impl
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  TC_BASIC_END,
  DEF_TYPE_CODES_PUB(_)
  DEF_TYPE_CODES_ETC(_)
  #undef _
  TC_END
} END_TYPED_ENUM(TypeCode)
// order of intrinsic integer types must be signed,unsigned,signed,unsigned...
static_assert(TC_i8+1  == TC_u8,  "integer order incorrect");
static_assert(TC_i16+1 == TC_u16, "integer order incorrect");
static_assert(TC_i32+1 == TC_u32, "integer order incorrect");
static_assert(TC_i64+1 == TC_u64, "integer order incorrect");
// must be less than 32 basic (numeric) types
static_assert(TC_BASIC_END <= 32, "there must be no more than 32 basic types");


enum TypeKind {
  // type kinds (similar to LLVMTypeKind)
  TF_KindVoid,    // type with no size
  TF_KindBool,    // boolean
  TF_KindInt,     // Arbitrary bit-width integers
  TF_KindF16,     // 16 bit floating point type
  TF_KindF32,     // 32 bit floating point type
  TF_KindF64,     // 64 bit floating point type
  TF_KindFunc,    // Functions
  TF_KindStruct,  // Structures
  TF_KindArray,   // Arrays
  TF_KindPointer, // Pointers
  TF_KindVector,  // Fixed width SIMD vector type
  TF_Kind_MAX = TF_KindVector,
  TF_Kind_NBIT = ILOG2(TF_Kind_MAX) + 1,
} END_TYPED_ENUM(TypeKind)

enum TypeFlags {
  // implicitly includes TF_Kind* (enum TypeKind)

  // size in bytes
  TF_Size_BITOFFS = TF_Kind_NBIT,
  TF_Size1  = 1 << TF_Size_BITOFFS,       // = 1 byte (8 bits) wide
  TF_Size2  = 1 << (TF_Size_BITOFFS + 1), // = 2 bytes (16 bits) wide
  TF_Size4  = 1 << (TF_Size_BITOFFS + 2), // = 4 bytes (32 bits) wide
  TF_Size8  = 1 << (TF_Size_BITOFFS + 3), // = 8 bytes (64 bits) wide
  TF_Size16 = 1 << (TF_Size_BITOFFS + 4), // = 16 bytes (128 bits) wide
  TF_Size_MAX = TF_Size16,
  TF_Size_NBIT = (ILOG2(TF_Size_MAX) + 1) - TF_Size_BITOFFS,
  TF_Size_MASK = (TypeFlags)(~0) >> (sizeof(TypeFlags)*8 - TF_Size_BITOFFS) << TF_Size_NBIT,

  // attributes
  TF_Attr_BITOFFS = ILOG2(TF_Size_MAX) + 1,
  #define B TF_Attr_BITOFFS
  TF_Signed     = 1 << B,       // is signed (integers only)
  TF_CustomInit = 1 << (B + 1), // struct has fields w/ non-zero initializer
  #undef B
} END_TYPED_ENUM(TypeFlags)

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

enum DiagLevel {
  DiagError,
  DiagWarn,
  DiagNote,
  DiagMAX = DiagNote,
} END_TYPED_ENUM(DiagLevel)

enum ParseFlags {
  ParseFlagsDefault = 0,
  ParseComments     = 1 << 1, // parse comments, populating S.comments_{head,tail}
  ParseOpt          = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
} END_TYPED_ENUM(ParseFlags)


// DiagHandler callback type.
// msg is a preformatted error message and is only valid until this function returns.
typedef void(DiagHandler)(Diagnostic* d, void* userdata);

// NodeArray
DEF_TYPED_ARRAY(NodeArray, Node*)

// Node types (union'ed in Node struct)
struct CommentNode {
  u32       len;
  const u8* ptr;
};
struct IdNode {
  Sym   name;
  Node* target;
};
struct BinOpNode {
  Tok   op;
  Node* left;
  Node* right;
};
struct UnaryOpNode { // used for NPrefixOp, NPostfixOp, NReturn, NAssign
  Tok   op;
  Node* expr;
};
struct FileNode { // used for  NFile, NPkg
  const Str       name;         // reference to str in corresponding Source/Pkg struct
  Scope* nullable scope;
  NodeArray       a;            // array of nodes
  Node*           a_storage[4]; // in-struct storage for the first few entries of a
};
struct ArrayNode { // used for NTuple, NBlock, NArray
  NodeArray a;            // array of nodes
  Node*     a_storage[5]; // in-struct storage for the first few entries of a
};
struct FunNode {
  Node* nullable params;  // input params (NTuple or NULL if none)
  Node* nullable result;  // output results (NTuple | NExpr)
  Sym   nullable name;    // NULL for lambda
  Node* nullable body;    // NULL for fun-declaration
};
struct MacroNode {
  Node* nullable params;  // input params (NTuple or NULL if none)
  Sym   nullable name;
  Node*          template;
};
struct CallNode {
  Node* receiver;      // Fun or Id
  Node* nullable args; // NULL if there are no args, else a NTuple
};
struct TypeCastNode {
  Node* receiver;      // Type or Id
  Node* nullable args; // NULL if there are no args, else a NTuple
};
struct FieldNode {
  u32            nrefs; // reference count
  u32            index; // argument index or struct index
  Sym            name;
  Node* nullable init;  // initial value (may be NULL)
};
struct VarNode {
  bool           isconst; // immutable storage? (true for "const x" vars)
  u32            nrefs;   // reference count
  u32            index;   // argument index (used by function parameters)
  Sym            name;
  Node* nullable init;    // initial/default value
};
struct RefNode {
  Node* target;
};
struct NamedValNode {
  Sym   name;
  Node* value;
};
struct SelectorNode { // Selector = Expr "." ( Ident | Selector )
  Node*    operand;
  Sym      member;  // id
  U32Array indices; // GEP index path
  u32      indices_st[4]; // indices storage
};
struct IndexNode { // Index = Expr "[" Expr "]"
  Node* operand;
  Node* indexexpr;
  u32   index; // 0xffffffff if indexexpr is not a compile-time constant
};
struct SliceNode { // Slice = Expr "[" Expr? ":" Expr? "]"
  Node*          operand;
  Node* nullable start;
  Node* nullable end;
};
struct IfNode {
  Node*          cond;
  Node*          thenb;
  Node* nullable elseb; // NULL or expr
};

// --- type nodes ---
struct BasicTypeNode {
  TypeCode typeCode;
  Sym      name;
};
struct ArrayTypeNode {
  Node* nullable sizeexpr; // NULL for inferred types
  u32            size;     // used for array. 0 until sizeexpr is resolved
  Node*          subtype;
};
struct TupleTypeNode {
  NodeArray a;            // Node[]
  Node*     a_storage[4]; // in-struct storage for the first few elements
};
struct StructTypeNode {
  Sym nullable name;         // NULL for anonymous structs
  NodeArray    a;            // NField[]
  Node*        a_storage[3]; // in-struct storage for the first few fields
};
struct FunTypeNode {
  Node* nullable params; // NTuple of NVar or null if no params
  Type* nullable result; // NTupleType of types or single type
};


struct Node {
  void* nullable irval;  // used by IR builders for temporary storage
  Pos            pos;    // source origin & position
  Pos            endpos; // NoPos means "only use pos".
  NodeFlags      flags;  // flags describe meta attributes of the node
  NodeKind       kind;   // kind of node (e.g. NId)
};

// trick using C11 seamless struct extension to get compiler type checking on Node
struct Stmt { struct Node;
  union {
    struct FileNode    nfile;
    struct FileNode    npkg;
    struct CommentNode ncomment;
  };
};
struct Expr { struct Node;
  Type* nullable type; // value type. NULL if unknown.
  union {
    u64                 intval; // NBoolLit, NIntLit
    double              numval; // NFloatLit
    Str                 strval; // NStrLit
    struct IdNode       id;
    struct BinOpNode    binop;
    struct UnaryOpNode  unaryop; // NPrefixOp, NPostfixOp, NReturn, NAssign
    struct ArrayNode    tuple;
    struct ArrayNode    block;
    struct ArrayNode    array;
    struct FunNode      fun;
    struct MacroNode    macro;
    struct CallNode     call;
    struct TypeCastNode typecast;
    struct FieldNode    field;
    struct VarNode      var;
    struct RefNode      ref;
    struct NamedValNode namedval;
    struct SelectorNode selector;
    struct IndexNode    index;
    struct SliceNode    slice;
    struct IfNode       iff;
  };
};
struct Type { struct Node;
  TypeFlags    tflags; // u16 (Note: used to be TypeKind kind)
  Sym nullable tid;    // initially NULL for user-defined types, computed as needed
  union {
    Type*                 tref;  // NRefType element
    Type*                 ttype; // NTypeType type
    struct BasicTypeNode  tbasic;
    struct ArrayTypeNode  tarray;
    struct TupleTypeNode  ttuple;
    struct StructTypeNode tstruct;
    struct FunTypeNode    tfun;
  };
};
#define NODE_UNION_SIZE sizeof(struct{union{Stmt a;Expr b;Type c;};})
static_assert(NODE_UNION_SIZE <= 96, "Node structs grew! (update this check)");

struct Pkg {
  Str     id;      // fully qualified name (e.g. "bar/cat"); TODO: consider using Sym
  Source* srclist; // list of sources (linked via Source.next)
};

struct Source {
  Source*   next;       // list link
  Str       filename;   // copy of filename given to source_open
  const u8* body;       // file body (usually mmap'ed)
  u32       len;        // size of body in bytes
  int       fd;         // file descriptor
  u8        sha256[32]; // SHA-256 checksum of body, set with source_checksum
  bool      ismmap;     // true if the file is memory-mapped
};

struct PosMap {
  Mem      mem; // used to allocate extra memory for a
  PtrArray a;
  void*    a_storage[32]; // slot 0 is always NULL
};

struct PosSpan {
  Pos start;
  Pos end; // inclusive, unless it's NoPos
};

struct Diagnostic {
  BuildCtx*   build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
};

DEF_TYPED_ARRAY(DiagnosticArray, Diagnostic*)

struct BuildCtx {
  bool     opt;       // optimize
  bool     debug;     // include debug information
  bool     safe;      // enable boundary checks and memory ref checks
  TypeCode sint_type; // concrete type of "int"
  TypeCode uint_type; // concrete type of "uint"

  Mem             mem;       // memory allocator
  SymPool*        syms;      // symbol pool
  DiagnosticArray diagarray; // all diagnostic messages produced. Stored in mem.
  PosMap          posmap;    // maps Source <-> Pos

  // interned types
  struct {
    SymMap       types;
    SymMapBucket types_st[8];
  };

  // build state
  Pkg* pkg; // top-level package for which we are building

  // diagnostics
  DiagHandler* nullable diagh;     // diagnostics handler
  void* nullable        userdata;  // custom user data passed to error handler
  DiagLevel             diaglevel; // diagnostics filter (some > diaglevel is ignored)
  u32                   errcount;  // total number of errors since last call to build_init
};

// Comment is a scanned comment
struct Comment {
  struct Comment* next; // next comment in linked list
  Source*         src;  // source
  const u8*       ptr;  // ptr into source
  u32             len;  // byte length
};

// Indent tracks source indetation
struct Indent {
  bool isblock; // true if this indent is a block
  u32  n;       // number of whitespace chars
};

// Scope represents a lexical namespace which may be chained.
struct Scope {
  const Scope* parent;
  SymMap       bindings; // must be last member
};

// Scanner reads source code and produces tokens
struct Scanner {
  BuildCtx*  build;        // build context (memory allocator, sympool, pkg, etc.)
  Source*    src;          // input source
  u32        srcposorigin;
  ParseFlags flags;
  bool       insertSemi;   // insert a semicolon before next newline
  const u8*  inp;          // input buffer current pointer
  const u8*  inend;        // input buffer end

  // indentation
  Indent indent;           // current level
  Indent indentDst;        // unwind to level
  struct { // previous indentation levels (Indent elements)
    Indent* v;
    u32     len;
    u32     cap;
    Indent  storage[16];
  } indentStack;

  // token
  Tok        tok;           // current token
  const u8*  tokstart;      // start of current token
  const u8*  tokend;        // end of current token
  const u8*  prevtokend;    // end of previous token
  Sym        name;          // Current name (valid for TId and keywords)

  u32        lineno;        // source position line
  const u8*  linestart;     // source position line start pointer (for column)

  Comment*   comments_head; // linked list head of comments scanned so far
  Comment*   comments_tail; // linked list tail of comments scanned so far
};

// Parser holds state used during parsing
struct Parser {
  Scanner   s;        // parser is based on a scanner
  BuildCtx* build;    // build context
  Scope*    pkgscope; // package-level scope
  Node*     expr;     // most recently parsed expression
  u32       fnest;    // function nesting level

  // set when parsing named type e.g. "type Foo ..."
  Sym nullable typename;

  // ctxtype is non-null when the parser is confident about the type context
  Type* nullable ctxtype;

  // scopestack is used for tracking identifiers during parsing.
  // This is a simple stack which we do a linear search on when looking up identifiers.
  // It is faster than using chained hash maps in most cases because of cache locality
  // and the fact that...
  // 1. Most identifiers reference an identifier defined nearby. For example:
  //      x = 3
  //      A = x + 5
  //      B = x - 5
  // 2. Most bindings are short-lived and temporary ("locals") which means we can
  //    simply change a single index pointer to "unwind" an entire scope of bindings and
  //    then reuse that memory for the next binding scope.
  //
  // base is the offset in ptr to the current scope's base. Loading ptr[base] yields a uintptr
  // that is the next scope's base index.
  // keys (Sym) and values (Node) are interleaved in ptr together with saved base pointers.
  struct {
    uintptr cap;          // capacity of ptr (count, not bytes)
    uintptr len;          // current length (use) of ptr
    uintptr base;         // current scope's base index into ptr
    void**  ptr;          // entries
    void*   storage[256]; // initial storage in parser's memory
  } scopestack;
};

// end of types
// ======================================================================================
// start of data

// NoPos is a valid unknown position; pos_isknown(NoPos) returns false.
static const Pos NoPos = 0;

extern Node* kNode_bad; // kind=NBad
extern Node* kType_type; // kind=NTypeType

extern Type* kType_bool;
extern Type* kType_i8;
extern Type* kType_u8;
extern Type* kType_i16;
extern Type* kType_u16;
extern Type* kType_i32;
extern Type* kType_u32;
extern Type* kType_i64;
extern Type* kType_u64;
extern Type* kType_f32;
extern Type* kType_f64;
extern Type* kType_int;
extern Type* kType_uint;
extern Type* kType_nil;
extern Type* kType_ideal;
extern Type* kType_str;
extern Type* kType_auto;
extern Node* kNode_true;
extern Node* kNode_false;
extern Node* kNode_nil;

// end of data
// ======================================================================================
// start of functions

// ---- types

// TF_Kind returns the TF_Kind* value of a TypeFlags
inline static TypeKind TF_Kind(TypeFlags tf) { return tf & ((1 << TF_Kind_NBIT) - 1); }

// TF_Size returns the storage size in bytes for a TypeFlags
inline static u8 TF_Size(TypeFlags tf) { return (tf & TF_Size_MASK) >> TF_Size_BITOFFS; }

// TF_IsSigned returns true if TF_Signed is set for tf
inline static bool TF_IsSigned(TypeFlags tf) { return (tf & TF_Signed) != 0; }

// TypeCodeEncoding
// Lookup table TypeCode => string encoding char
extern const char _TypeCodeEncodingMap[TC_END];
ALWAYS_INLINE static char TypeCodeEncoding(TypeCode t) { return _TypeCodeEncodingMap[t]; }


// ---- universe

void universe_init();
const Scope* universe_scope();
const SymPool* universe_syms();

// ---- Pkg

void pkg_add_source(Pkg* pkg, Source* src); // add src to pkg->srclist
error pkg_add_file(Pkg* pkg, Mem mem, const char* filename);
error pkg_add_dir(Pkg* pkg, Mem mem, const char* filename); // add all *.co files in dir

// ---- Source

error source_open_file(Source* src, Mem mem, const char* filename);
error source_open_data(Source* src, Mem mem, const char* filename, const char* text, u32 len);
error source_body_open(Source* src);
error source_body_close(Source* src);
error source_close(Source* src); // src can be reused with open after this call
void  source_checksum(Source* src); // populates src->sha256 <= sha256(src->body)

// ---- Pos

void posmap_init(PosMap* pm, Mem mem);
void posmap_dispose(PosMap* pm);

// posmap_origin retrieves the origin for source, allocating one if needed.
// See pos_source for the inverse function.
u32 posmap_origin(PosMap* pm, Source* source);

// pos_source looks up the source for a pos. The inverse of posmap_origin.
// Returns NULL for unknown positions.
static Source* nullable pos_source(const PosMap* pm, Pos p);

static Pos pos_make(u32 origin, u32 line, u32 col, u32 width);
static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width); // no bounds checks!

static u32 pos_origin(Pos p); // 0 for pos without origin
static u32 pos_line(Pos p);
static u32 pos_col(Pos p);
static u32 pos_width(Pos p);

static Pos pos_with_origin(Pos p, u32 origin); // returns copy of p with specific origin
static Pos pos_with_line(Pos p, u32 line);   // returns copy of p with specific line
static Pos pos_with_col(Pos p, u32 col);    // returns copy of p with specific col
static Pos pos_with_width(Pos p, u32 width);  // returns copy of p with specific width

// pos_with_adjusted_start returns a copy of p with its start and width adjusted by deltacol.
// Can not overflow; the result is clamped.
Pos pos_with_adjusted_start(Pos p, i32 deltacol);

// pos_union returns a Pos that covers the column extent of both a and b.
// a and b must be on the same line.
Pos pos_union(Pos a, Pos b);

// pos_isknown reports whether the position is a known position.
static bool pos_isknown(Pos);

// pos_isbefore reports whether the position p comes before q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isbefore(Pos p, Pos q);

// pos_isafter reports whether the position p comes after q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isafter(Pos p, Pos q);

// pos_str appends "file:line:col" to dst
Str pos_str(const PosMap*, Pos, Str dst);

// pos_fmt appends "file:line:col: format ..." to s, including source context
Str pos_fmt(const PosMap*, PosSpan, Str s, const char* fmt, ...) ATTR_FORMAT(printf, 4, 5);
Str pos_fmtv(const PosMap*, PosSpan, Str s, const char* fmt, va_list);

// --- Pos inline implementations

// Layout constants: 20 bits origin, 20 bits line, 12 bits column, 12 bits width.
// Limits: sources: 1048575, lines: 1048575, columns: 4095, width: 4095
// If this is too tight, we can either make lico 64b wide, or we can introduce a tiered encoding
// where we remove column information as line numbers grow bigger; similar to what gcc does.
static const u64 _pos_widthBits  = 12;
static const u64 _pos_colBits    = 12;
static const u64 _pos_lineBits   = 20;
static const u64 _pos_originBits = 64 - _pos_lineBits - _pos_colBits - _pos_widthBits;

static const u64 _pos_originMax = (1llu << _pos_originBits) - 1;
static const u64 _pos_lineMax   = (1llu << _pos_lineBits) - 1;
static const u64 _pos_colMax    = (1llu << _pos_colBits) - 1;
static const u64 _pos_widthMax  = (1llu << _pos_widthBits) - 1;

static const u64 _pos_originShift = _pos_originBits + _pos_colBits + _pos_widthBits;
static const u64 _pos_lineShift   = _pos_colBits + _pos_widthBits;
static const u64 _pos_colShift    = _pos_widthBits;

inline static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width) {
  return (Pos)( ((u64)origin << _pos_originShift)
              | ((u64)line << _pos_lineShift)
              | ((u64)col << _pos_colShift)
              | width );
}
inline static Pos pos_make(u32 origin, u32 line, u32 col, u32 width) {
  return pos_make_unchecked(
    MIN(_pos_originMax, origin),
    MIN(_pos_lineMax, line),
    MIN(_pos_colMax, col),
    MIN(_pos_widthMax, width));
}
inline static u32 pos_origin(Pos p) { return p >> _pos_originShift; }
inline static u32 pos_line(Pos p)   { return (p >> _pos_lineShift) & _pos_lineMax; }
inline static u32 pos_col(Pos p)    { return (p >> _pos_colShift) & _pos_colMax; }
inline static u32 pos_width(Pos p)   { return p & _pos_widthMax; }

// TODO: improve the efficiency of these
inline static Pos pos_with_origin(Pos p, u32 origin) {
  return pos_make_unchecked(
    MIN(_pos_originMax, origin), pos_line(p), pos_col(p), pos_width(p));
}
inline static Pos pos_with_line(Pos p, u32 line) {
  return pos_make_unchecked(
    pos_origin(p), MIN(_pos_lineMax, line), pos_col(p), pos_width(p));
}
inline static Pos pos_with_col(Pos p, u32 col) {
  return pos_make_unchecked(
    pos_origin(p), pos_line(p), MIN(_pos_colMax, col), pos_width(p));
}
inline static Pos pos_with_width(Pos p, u32 width) {
  return pos_make_unchecked(
    pos_origin(p), pos_line(p), pos_col(p), MIN(_pos_widthMax, width));
}
inline static bool pos_isbefore(Pos p, Pos q) { return p < q; }
inline static bool pos_isafter(Pos p, Pos q) { return p > q; }
inline static bool pos_isknown(Pos p) {
  return pos_origin(p) != 0 || pos_line(p) != 0;
}
inline static Source* nullable pos_source(const PosMap* pm, Pos p) {
  return (Source*)pm->a.v[pos_origin(p)];
}

// ---- BuildCtx

// buildctx_init initializes a BuildCtx structure
void buildctx_init(BuildCtx*,
  Mem                   mem,
  SymPool*              syms,
  Pkg*                  pkg,
  DiagHandler* nullable diagh,
  void* nullable        userdata // passed along to diagh
);

// buildctx_dispose frees up internal resources. BuildCtx can be reused with buildctx_init after this call.
void buildctx_dispose(BuildCtx*);

// buildctx_diag invokes b->diagh with message (the message's bytes are copied into b->mem)
void buildctx_diag(BuildCtx*, DiagLevel, PosSpan, const char* message);

// buildctx_diagv formats a diagnostic message and invokes ctx->diagh
void buildctx_diagv(BuildCtx*, DiagLevel, PosSpan, const char* format, va_list);

// buildctx_diagf formats a diagnostic message invokes b->diagh
void buildctx_diagf(BuildCtx*, DiagLevel, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 4, 5);

// buildctx_errf calls buildctx_diagf with DiagError
void buildctx_errf(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// buildctx_warnf calls buildctx_diagf with DiagWarn
void buildctx_warnf(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// buildctx_warnf calls buildctx_diagf with DiagNote
void buildctx_notef(BuildCtx*, PosSpan, const char* format, ...) ATTR_FORMAT(printf, 3, 4);

// ---- diagnostics

// diag_fmt appends to dst a ready-to-print representation of a diagnostic message
Str diag_fmt(const Diagnostic*, Str dst);

// diag_free frees a diagnostics object.
// It is useful when a ctx's mem is a shared allocator.
// Normally you'd just dipose an entire ctx mem arena instead of calling this function.
// Co never calls this itself but a user's diagh function may.
void diag_free(Diagnostic*);

// DiagLevelName returns a printable string like "error"
const char* DiagLevelName(DiagLevel);

// ---- parsing

// tokname returns a printable name for a token (second part in TOKENS definition)
const char* tokname(Tok);

// langtok returns the Tok representing this sym in the language syntax.
// Either returns a keyword token or TId if sym is not a keyword.
inline static Tok langtok(Sym s) {
  // Bits [4-8) represents offset into Tok enum when s is a language keyword.
  u8 kwindex = symflags(s);
  return kwindex == 0 ? TId : TKeywordsStart + kwindex;
}

// scan_init initializes a scanner. Returns false if SourceOpenBody fails.
error scan_init(Scanner*, BuildCtx*, Source*, ParseFlags);

// scan_dispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as scan_init calls SourceOpenBody.
void scan_dispose(Scanner*);

// scan_next scans the next token
Tok scan_next(Scanner*);

// scan_pos returns the source position of s->tok (current token)
Pos scan_pos(const Scanner* s);

// scan_tokstr returns a token's string value and length, which is a pointer
// into the source's body.
inline static const u8* scan_tokstr(const Scanner* s, usize* len_out) {
  *len_out = (usize)(s->tokend - s->tokstart);
  return s->tokstart;
}

// scan_comment_pop removes and returns the least recently scanned comment.
// The caller takes ownership of the comment and should free it using memfree(s->mem,comment).
Comment* nullable scan_comment_pop(Scanner* s);

// parse a translation unit and return AST or NULL on error (reported to diagh)
// Expects p to be zero-initialized on first call. Can reuse p after return.
Node* nullable parse(Parser* p, BuildCtx*, Source*, ParseFlags, Scope* pkgscope);

// ---- AST

Scope* nullable scope_new(Mem mem, const Scope* nullable parent);
void scope_free(Scope*, Mem mem);
const Node* nullable scope_lookup(const Scope*, Sym);
inline static error scope_assoc(Scope* s, Sym key, const Node** valuep_inout) {
  return SymMapSet(&s->bindings, key, (void**)valuep_inout);
}

const char* NodeKindName(NodeKind); // e.g. "NIntLit"
const char* TypeKindName(TypeKind); // e.g. "integer"

inline static bool NodeIsStmt(const Node* n) {
  return n->kind < NodeKind_END_STMT;
}
inline static bool NodeIsConstLit(const Node* n) {
  return n->kind > NodeKind_START_CONSTLIT && n->kind < NodeKind_END_CONSTLIT;
}
inline static bool NodeIsExpr(const Node* n) {
  return n->kind > NodeKind_START_EXPR && n->kind < NodeKind_END_EXPR;
}
inline static bool NodeIsType(const Node* n) {
  return n->kind > NodeKind_START_TYPE;
}

inline static bool NodeIsPrimitiveConst(const Node* n) {
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

bool _TypeEquals(BuildCtx* ctx, Type* x, Type* y); // impl parse_type.c
inline static bool TypeEquals(BuildCtx* ctx, Type* x, Type* y) {
  return x == y || _TypeEquals(ctx, x, y);
}

ASSUME_NONNULL_END
