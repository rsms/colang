#pragma once
//
// Fundamental type information used across components of Co (parse, ir, etc.)
//
ASSUME_NONNULL_BEGIN

typedef enum TypeCodeFlag {
  TypeCodeFlagNone = 0,
  TypeCodeFlagSizeMask = 0b0000000000001111, // bitmask for extracting SizeN flag
  TypeCodeFlagSize1    = 1 << 0, // = 1 = 1 byte (8 bits) wide
  TypeCodeFlagSize2    = 1 << 1, // = 2 = 2 bytes (16 bits) wide
  TypeCodeFlagSize4    = 1 << 2, // = 4 = 4 bytes (32 bits) wide
  TypeCodeFlagSize8    = 1 << 3, // = 8 = 8 bytes (64 bits) wide
  TypeCodeFlagInt      = 1 << 4, // is integer
  TypeCodeFlagFloat    = 1 << 5, // is float
  TypeCodeFlagSigned   = 1 << 6, // [integers only]: is signed
} TypeCodeFlag;

// TypeCode with their string encoding. Becomes TypeCode_NAME
// Note: ir/gen_ops.py relies on "#define TYPE_CODES" and "NUM_END".
#define TYPE_CODES(_)                                                                          \
  /* named types exported in the global scope. Names must match those of TYPE_SYMS.          */\
  /* Note: numeric types are listed first as their enum value is used as dense indices.      */\
  /* Note: order of intrinsic integer types must be signed,unsigned,signed,unsigned...       */\
  /* Reordering these requires updating TypeCodeIsInt() below.                               */\
  /*                                                                                         */\
  /* name       encoding  flags                                                              */\
  _( bool      , 'b', 0 )                                                                      \
  _( int8      , '1', TypeCodeFlagSize1 | TypeCodeFlagInt | TypeCodeFlagSigned )               \
  _( uint8     , '2', TypeCodeFlagSize1 | TypeCodeFlagInt )                                    \
  _( int16     , '3', TypeCodeFlagSize2 | TypeCodeFlagInt | TypeCodeFlagSigned )               \
  _( uint16    , '4', TypeCodeFlagSize2 | TypeCodeFlagInt )                                    \
  _( int32     , '5', TypeCodeFlagSize4 | TypeCodeFlagInt | TypeCodeFlagSigned )               \
  _( uint32    , '6', TypeCodeFlagSize4 | TypeCodeFlagInt )                                    \
  _( int64     , '7', TypeCodeFlagSize8 | TypeCodeFlagInt | TypeCodeFlagSigned )               \
  _( uint64    , '8', TypeCodeFlagSize8 | TypeCodeFlagInt )                                    \
  _( float32   , 'f', TypeCodeFlagSize4 | TypeCodeFlagFloat )                                  \
  _( float64   , 'F', TypeCodeFlagSize8 | TypeCodeFlagFloat )                                  \
  _( int       , 'i', TypeCodeFlagInt | TypeCodeFlagSigned )                                   \
  _( uint      , 'u', TypeCodeFlagInt )                                                        \
  _( NUM_END, 0, 0 ) /* sentinel; not a TypeCode */                                            \
  _( str       , 's', 0 )                                                                      \
  _( nil       , '0', 0 )                                                                      \
  /*                                                                                         */\
  _( CONCRETE_END, 0, 0 ) /* sentinel; not a TypeCode */                                       \
  /*                                                                                         */\
  /* internal types not directly reachable by names in the language */                         \
  _( mem       , 'M', 0 ) /* memory location */                                                \
  _( fun       , '^', 0 )                                                                      \
  _( tuple     , '(', 0 ) _( tupleEnd  , ')', 0 )                                              \
  _( list      , '[', 0 ) _( listEnd   , ']', 0 )                                              \
  _( struct    , '{', 0 ) _( structEnd , '}', 0 )                                              \
  /* special type codes used in IR */                                                          \
  _( ideal     , '*' , 0 ) /* untyped numeric constants */                                     \
  _( param1    , 'P', 0 ) /* parameteric. For IR, matches other type, e.g. output == input */  \
  _( param2    , 'P', 0 )
/*END TYPE_CODES*/

// TypeCode identifies all basic types
typedef enum {
  #define I_ENUM(name, _encoding, _flags) TypeCode_##name,
  TYPE_CODES(I_ENUM)
  #undef  I_ENUM
  TypeCode_MAX
} TypeCode;

// order of intrinsic integer types must be signed,unsigned,signed,unsigned...
static_assert(TypeCode_int8+1  == TypeCode_uint8,  "integer order incorrect");
static_assert(TypeCode_int16+1 == TypeCode_uint16, "integer order incorrect");
static_assert(TypeCode_int32+1 == TypeCode_uint32, "integer order incorrect");
static_assert(TypeCode_int64+1 == TypeCode_uint64, "integer order incorrect");
// must be less than 32 numeric types
static_assert(TypeCode_NUM_END <= 32, "there must be no more than 32 numeric types");

// CType describes the constant kind of an "ideal" (untyped) constant.
// These are ordered from less dominant to more dominant -- a CType with a higher value
// takes precedence over a CType with a lower value in cases like untyped binary operations.
typedef enum CType { // TODO: Rename to ConstType
  CType_INVALID,
  CType_int,
  CType_rune,
  CType_float,
  CType_str,
  CType_bool,
  CType_nil,
} CType;
const char* CTypeName(CType ct);

// TYPE_SYMS: named types exported in the global namespace as keywords (by universe)
// These also have AST nodes `Type_NAME` predefined (by universe)
// IMPORTANT: These must match the list of TypeCodes up until CONCRETE_END.
// Looking for all type defs? universe.h puts it all together.
#define TYPE_SYMS(_) \
  _( bool    ) \
  _( int8    ) \
  _( uint8   ) \
  _( int16   ) \
  _( uint16  ) \
  _( int32   ) \
  _( uint32  ) \
  _( int64   ) \
  _( uint64  ) \
  _( float32 ) \
  _( float64 ) \
  _( int     ) \
  _( uint    ) \
  _( str     ) \
/*END TYPE_SYMS*/

// TYPE_SYMS_PRIVATE: named types like TYPE_SYMS but not exported in the global namespace.
// These also have AST nodes `Type_NAME` predefined by universe.c
#define TYPE_SYMS_PRIVATE(_) \
  _( ideal ) \
  _( nil ) \
/*END TYPE_SYMS_PRIVATE*/

// Note: The following function is provided by sym.h
// static Node* TypeCodeToTypeNode(TypeCode t);

// Lookup table TypeCode => string encoding char
extern const char TypeCodeEncoding[TypeCode_MAX];

// Symbolic name of type code. Eg "int32"
static const char* TypeCodeName(TypeCode);
extern const char* _TypeCodeName[TypeCode_MAX];
inline static const char* TypeCodeName(TypeCode tc) {
  assert(tc >= 0 && tc < TypeCode_MAX);
  return _TypeCodeName[tc];
}

// access TypeCodeFlag
extern const TypeCodeFlag TypeCodeFlagMap[TypeCode_MAX];

ALWAYS_INLINE static TypeCodeFlag TypeCodeFlags(TypeCode t) {
  return TypeCodeFlagMap[t];
}
ALWAYS_INLINE static bool TypeCodeIsInt(TypeCode t) {
  return TypeCodeFlags(t) & TypeCodeFlagInt;
}
ALWAYS_INLINE static bool TypeCodeIsFloat(TypeCode t) {
  return TypeCodeFlags(t) & TypeCodeFlagFloat;
}

ASSUME_NONNULL_END
