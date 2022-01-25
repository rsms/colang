#pragma once

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
typedef u8                TypeCode;   // TC_* constants
typedef u16               TypeFlag;   // TF_* constants (enum TypeFlag)
typedef u8                TypeKind;   // TF_Kind* constants (part of TypeFlag)

// AST types
typedef u16              Tok;       // language tokens (produced by Scanner)
typedef struct Node      Node;      // AST node
typedef u8               NodeKind;  // AST node kind (NNone, NBad, NBoolLit ...)
typedef u16              NodeFlags; // NF_* constants; AST node flags (Unresolved, Const ...)
typedef struct NodeArray NodeArray; // dynamically-sized array of Nodes
typedef struct Node      Type;      // AST type node (alias of Node)


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
#define DEF_TYPE_CODES_BASIC_PUB(_)/* (name, char encoding, TypeFlag) */\
  _( bool  , 'b' , TF_KindBool )                           \
  _( i8    , '1' , TF_KindInt | TF_Size1 | TF_Signed )     \
  _( u8    , '2' , TF_KindInt | TF_Size1 )                 \
  _( i16   , '3' , TF_KindInt | TF_Size2 | TF_Signed )     \
  _( u16   , '4' , TF_KindInt | TF_Size2 )                 \
  _( i32   , '5' , TF_KindInt | TF_Size4 | TF_Signed )     \
  _( u32   , '6' , TF_KindInt | TF_Size4 )                 \
  _( i64   , '7' , TF_KindInt | TF_Size8 | TF_Signed )     \
  _( u64   , '8' , TF_KindInt | TF_Size8 )                 \
  _( f32   , 'f' , TF_KindF32 | TF_Size4 | TF_Signed )     \
  _( f64   , 'F' , TF_KindF64 | TF_Size8 | TF_Signed )     \
  _( int   , 'i' , TF_KindInt            | TF_Signed )     \
  _( uint  , 'u' , TF_KindInt )                            \
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

enum TypeFlag {
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
  TF_Size_MASK = (TypeFlag)(~0) >> (sizeof(TypeFlag)*8 - TF_Size_BITOFFS) << TF_Size_NBIT,

  // attributes
  TF_Attr_BITOFFS = ILOG2(TF_Size_MAX) + 1,
  TF_Signed = 1 << TF_Attr_BITOFFS, // is signed (integers only)
} END_TYPED_ENUM(TypeFlag)

enum NodeFlags {
  NF_Unresolved  = 1 << 0,  // contains unresolved references. MUST BE VALUE 1!
  NF_Const       = 1 << 1,  // constant; value known at compile time (comptime)
  NF_Base        = 1 << 2,  // [struct field] the field is a base of the struct
  NF_RValue      = 1 << 4,  // resolved as rvalue
  NF_Param       = 1 << 5,  // [Var] function parameter
  NF_MacroParam  = 1 << 6,  // [Var] macro parameter
  NF_CustomInit  = 1 << 7,  // [StructType] has fields w/ non-zero initializer
  NF_Unused      = 1 << 8,  // [Var] never referenced
  NF_Public      = 1 << 9,  // [Var|Fun] public visibility (aka published, exported)
  NF_Named       = 1 << 11, // [Tuple when used as args] has named argument
  NF_PartialType = 1 << 12, // Type resolver should visit even if the node is typed
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

// NodeArray is a typed Array of Node* elements
struct NodeArray { Array a; };


// NodeKind       kind;   // kind of node (e.g. NId)
// NodeFlags      flags;  // flags describe meta attributes of the node
// Pos            pos;    // source origin & position
// Pos            endpos; // Used by compound types like tuple. NoPos means "only use pos".
// Type* nullable type;   // value type. NULL if unknown.
// void* nullable irval;  // used by IR builders for temporary storage
struct Node {
  Type* nullable type;   // value type. NULL if unknown.
  void* nullable irval;  // used by IR builders for temporary storage
  Pos            pos;    // source origin & position
  Pos            endpos; // Used by compound types like tuple. NoPos means "only use pos".
  NodeFlags      flags;  // flags describe meta attributes of the node
  NodeKind       kind;   // kind of node (e.g. NId)
  union {
    u64    ival;  // NBoolLit, NIntLit  (Note: Co2 uses NVal & CType)
    double fval;  // NFloatLit
    Str    sval;  // NStrLit
    /* str */ struct { // NComment
      const u8* ptr;
      size_t    len;
    } str;
    /* id */ struct { // NId
      Sym   name;
      Node* target;
    } id;
    /* op */ struct { // NBinOp, NPrefixOp, NPostfixOp, NReturn, NAssign
      Node*          left;
      Node* nullable right;  // NULL for PrefixOp & PostfixOp
      Tok            op;
    } op;
    /* cunit */ struct { // NFile, NPkg
      const Str       name;         // reference to str in corresponding Source/Pkg struct
      Scope* nullable scope;
      NodeArray       a;            // array of nodes
      Node*           a_storage[4]; // in-struct storage for the first few entries of a
    } cunit;
    /* array */ struct { // NTuple, NBlock, NArray
      NodeArray a;            // array of nodes
      Node*     a_storage[6]; // in-struct storage for the first few entries of a
    } array;
    /* fun */ struct { // NFun
      Node* nullable params;  // input params (NTuple or NULL if none)
      Node* nullable result;  // output results (NTuple | NExpr)
      Sym   nullable name;    // NULL for lambda
      Node* nullable body;    // NULL for fun-declaration
    } fun;
    /* macro */ struct { // NMacro
      Node* nullable params;  // input params (NTuple or NULL if none)
      Sym   nullable name;
      Node*          template;
    } macro;
    /* call */ struct { // NCall, NTypeCast
      Node* receiver;      // Fun, Id or type
      Node* nullable args; // NULL if there are no args, else a NTuple
    } call;
    /* field */ struct { // NField
      Sym            name;
      Node* nullable init;  // initial value (may be NULL)
      u32            nrefs; // reference count
      u32            index; // argument index or struct index
    } field;
    /* var */ struct { // NVar
      Sym            name;
      Node* nullable init;    // initial/default value
      u32            nrefs;   // reference count
      u32            index;   // argument index (used by function parameters)
      bool           isconst; // immutable storage? (true for "const x" vars)
    } var;
    /* ref */ struct { // NRef
      Node* target;
    } ref;
    /* namedval */ struct { // NNamedVal
      Sym   name;
      Node* value;
    } namedval;
    /* sel */ struct { // NSelector = Expr "." ( Ident | Selector )
      Node*    operand;
      Sym      member;  // id
      U32Array indices; // GEP index path
      u32      indices_st[10]; // indices storage
    } sel;
    /* index */ struct { // NIndex = Expr "[" Expr "]"
      Node* operand;
      Node* indexexpr;
      u32   index; // 0xffffffff if indexexpr is not a compile-time constant
    } index;
    /* slice */ struct { // NSlice = Expr "[" Expr? ":" Expr? "]"
      Node*          operand;
      Node* nullable start;
      Node* nullable end;
    } slice;
    /* cond */ struct { // NIf
      Node*          cond;
      Node*          thenb;
      Node* nullable elseb; // NULL or expr
    } cond;

    // Type
    /* t */ struct {
      Sym nullable id; // lazy; initially NULL. Computed from Node.
      TypeFlag     flags; // Note: used to be TypeKind kind
      union {
        /* basic */ struct { // NBasicType (int, bool, auto, etc)
          TypeCode typeCode;
          Sym      name;
        } basic;
        /* array */ struct { // NArrayType
          Node* nullable sizeexpr; // NULL for inferred types
          u32            size;     // used for array. 0 until sizeexpr is resolved
          Node*          subtype;
        } array;
        /* tuple */ struct { // NTupleType
          NodeArray a;            // Node[]
          Node*     a_storage[4]; // in-struct storage for the first few elements
        } tuple;
        /* struc */ struct { // NStructType
          Sym nullable name;         // NULL for anonymous structs
          NodeArray    a;            // NField[]
          Node*        a_storage[3]; // in-struct storage for the first few fields
        } struc;
        /* fun */ struct { // NFunType
          Node* nullable params; // NTuple of NVar or null if no params
          Type* nullable result; // NTupleType of types or single type
        } fun;
        Type* ref;  // NRefType element
        Type* type; // NTypeType type
      };
    } t;

  }; // union
}; // struct Node
static_assert(sizeof(Node) <= 112, "Node struct grew. Update this check (or revert change)");

// BuildCtx holds state for a Co compilation session
struct BuildCtx {
  Mem      mem;       // memory allocator
  bool     opt;       // optimize
  bool     debug;     // include debug information
  bool     safe;      // enable boundary checks and memory ref checks
  SymPool* syms;      // symbol pool
  TypeCode sint_type; // concrete type of "int"
  TypeCode uint_type; // concrete type of "uint"
  Array    diagarray; // all diagnostic messages produced. Stored in mem.
  PosMap   posmap;    // maps Source <-> Pos

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

struct Diagnostic {
  BuildCtx*   build;
  DiagLevel   level;
  PosSpan     pos;
  const char* message;
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

extern const Node* NodeBad; // kind==NBad

// #define _(name, _type, _val) \
//   extern const Sym sym_##name; \
//   extern Node* Const_##name;
// DEF_CONST_NODES_PUB(_)
// #undef _

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


// TF_Kind returns the TF_Kind* value of a TypeFlag
inline static TypeKind TF_Kind(TypeFlag tf) { return tf & ((1 << TF_Kind_NBIT) - 1); }

// TF_Size returns the storage size in bytes for a TypeFlag
inline static u8 TF_Size(TypeFlag tf) { return (tf & TF_Size_MASK) >> TF_Size_BITOFFS; }

// TF_IsSigned returns true if TF_Signed is set for tf
inline static bool TF_IsSigned(TypeFlag tf) { return (tf & TF_Signed) != 0; }

// TypeCodeEncoding
// Lookup table TypeCode => string encoding char
extern const char _TypeCodeEncodingMap[TC_END];
ALWAYS_INLINE static char TypeCodeEncoding(TypeCode t) { return _TypeCodeEncodingMap[t]; }


// tokname returns a printable name for a token (second part in TOKENS definition)
const char* tokname(Tok);

// langtok returns the Tok representing this sym in the language syntax.
// Either returns a keyword token or TId if s is not a keyword.
static Tok langtok(Sym s);

// ----

Scope* nullable scope_new(Mem mem, const Scope* nullable parent);
void scope_free(Scope*, Mem mem);
// scope_assoc returns replaced value (or NULL) in valuep_inout
static error scope_assoc(Scope* s, Sym key, const Node** valuep_inout);
const Node* nullable scope_lookup(const Scope*, Sym);

// ----

void universe_init();
const Scope* universe_scope();
const SymPool* universe_syms();

// ----

DEF_TYPED_ARRAY_FUNCTIONS(NodeArray, a, Node, narray)

// ----

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

// ----

// diag_fmt appends to dst a ready-to-print representation of a diagnostic message
Str diag_fmt(const Diagnostic*, Str dst);

// diag_free frees a diagnostics object.
// It is useful when a ctx's mem is a shared allocator.
// Normally you'd just dipose an entire ctx mem arena instead of calling this function.
// Co never calls this itself but a user's diagh function may.
void diag_free(Diagnostic*);

// DiagLevelName returns a printable string like "error"
const char* DiagLevelName(DiagLevel);

// ----

// scan_init initializes a scanner. Returns false if SourceOpenBody fails.
error scan_init(Scanner*, BuildCtx*, Source*, ParseFlags);

// scan_dispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as scan_init calls SourceOpenBody.
void scan_dispose(Scanner*);

// scan_next scans the next token
Tok scan_next(Scanner*);

// scan_pos returns the source position of s->tok (current token)
static Pos scan_pos(const Scanner* s);

// scan_tokstr returns a token's string value and length, which is a pointer
// into the source's body.
static const u8* scan_tokstr(const Scanner* s, usize* len_out);

// scan_comment_pop removes and returns the least recently scanned comment.
// The caller takes ownership of the comment and should free it using memfree(s->mem,comment).
Comment* nullable scan_comment_pop(Scanner* s);

// parse a translation unit and return AST or NULL on error (reported to diagh)
// Expects p to be zero-initialized on first call. Can reuse p after return.
Node* nullable parse(Parser* p, BuildCtx*, Source*, ParseFlags, Scope* pkgscope);

// ----

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

static bool NodeIsExpr(const Node*);

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


// ---------------------------------------------------------------------------------
// implementations

inline static Tok langtok(Sym s) {
  // Bits [4-8) represents offset into Tok enum when s is a language keyword.
  u8 kwindex = symflags(s);
  return kwindex == 0 ? TId : TKeywordsStart + kwindex;
}

inline static error scope_assoc(Scope* s, Sym key, const Node** valuep_inout) {
  return SymMapSet(&s->bindings, key, (void**)valuep_inout);
}

inline static const u8* scan_tokstr(const Scanner* s, usize* len_out) {
  *len_out = (usize)(s->tokend - s->tokstart);
  return s->tokstart;
}

inline static Pos scan_pos(const Scanner* s) {
  // assert(s->tokend >= s->tokstart);
  u32 col = 1 + (u32)((uintptr)s->tokstart - (uintptr)s->linestart);
  u32 span = s->tokend - s->tokstart;
  return pos_make(s->srcposorigin, s->lineno, col, span);
}

bool _TypeEquals(BuildCtx* ctx, Type* x, Type* y); // impl typeid.c

inline static bool TypeEquals(BuildCtx* ctx, Type* x, Type* y) {
  return x == y || _TypeEquals(ctx, x, y);
}

// inline static Node* nullable NodeEvalUint(BuildCtx* ctx, Node* expr) {
//   auto zn = NodeEval(ctx, expr, Type_uint, NodeEvalMustSucceed);
//
//   #if DEBUG
//   if (zn) {
//     asserteq_debug(zn->kind, NIntLit);
//     asserteq_debug(zn->val.ct, CType_int);
//   }
//   #endif
//
//   // result in zn->val.i
//   return zn;
// }

ASSUME_NONNULL_END
