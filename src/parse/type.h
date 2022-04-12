// runtime types
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef u8  TypeCode;  // TC_* constants
typedef u16 TypeFlags; // TF_* constants (enum TypeFlags)
typedef u8  TypeKind;  // TF_Kind* constants (part of TypeFlags)

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
#define DEF_TYPE_CODES_BASIC_PUB(_)/* (name, char encoding, TypeFlags)                   */\
  _( bool      , 'b' , TF_KindBool )                                                       \
  _( i8        , 'c' , TF_KindInt  | TF_Size1 | TF_Signed )                                 \
  _( u8        , 'B' , TF_KindInt  | TF_Size1 )                                             \
  _( i16       , 's' , TF_KindInt  | TF_Size2 | TF_Signed )                                 \
  _( u16       , 'S' , TF_KindInt  | TF_Size2 )                                             \
  _( i32       , 'w' , TF_KindInt  | TF_Size4 | TF_Signed )                                 \
  _( u32       , 'W' , TF_KindInt  | TF_Size4 )                                             \
  _( i64       , 'd' , TF_KindInt  | TF_Size8 | TF_Signed )                                 \
  _( u64       , 'D' , TF_KindInt  | TF_Size8 )                                             \
  _( i128      , 'e' , TF_KindInt  | TF_Size16 | TF_Signed )                                 \
  _( u128      , 'E' , TF_KindInt  | TF_Size16 )                                             \
  _( f32       , 'f' , TF_KindF32  | TF_Size4  | TF_Signed )                                 \
  _( f64       , 'F' , TF_KindF64  | TF_Size8  | TF_Signed )                                 \
  _( f128      , 'F' , TF_KindF128 | TF_Size16 | TF_Signed )                                 \
  _( int       , 'i' , TF_KindInt            | TF_Signed )                                 \
  _( uint      , 'u' , TF_KindInt )                                                        \
// end DEF_TYPE_CODES_BASIC_PUB
#define DEF_TYPE_CODES_BASIC(_)                                                            \
  _( nil       , '0' , TF_KindVoid )                                                       \
  _( ideal     , '*' , TF_KindVoid )/* type of const literal                             */\
// end DEF_TYPE_CODES_BASIC
#define DEF_TYPE_CODES_PUB(_)                                                              \
  _( auto      , 'a' , TF_KindVoid    ) /* inferred                                      */\
// end DEF_TYPE_CODES_PUB
#define DEF_TYPE_CODES_ETC(_)                                                              \
  _( ref       , '&' , TF_KindPointer ) /* pointer memory address                        */\
  _( fun       , '^' , TF_KindFunc )                                                       \
  _( array     , '[' , TF_KindArray )                                                      \
  _( arrayEnd  , ']' , TF_KindVoid )                                                       \
  _( struct    , '{' , TF_KindStruct )                                                     \
  _( structEnd , '}' , TF_KindVoid )                                                       \
  _( tuple     , '(' , TF_KindArray )                                                      \
  _( tupleEnd  , ')' , TF_KindVoid )                                                       \
  _( param1    , 'P' , TF_KindVoid ) /* IR parameter, matches other type (output==input) */\
  _( param2    , 'P' , TF_KindVoid )
// end DEF_TYPE_CODES_ETC


// TypeCode identifies all basic types
enum TypeCode {
  #define _(name, _encoding, _flags) TC_##name,
  // IMPORTANT: this must match _TypeCodeEncodingMap impl
  DEF_TYPE_CODES_BASIC_PUB(_)
  TC_NUM_END,
  DEF_TYPE_CODES_BASIC(_)
  TC_BASIC_END,
  DEF_TYPE_CODES_PUB(_)
  DEF_TYPE_CODES_ETC(_)
  #undef _
  TC_END
} END_ENUM(TypeCode)
// order of intrinsic integer types must be signed,unsigned,signed,unsigned...
static_assert(TC_i8+1   == TC_u8,   "integer order incorrect");
static_assert(TC_i16+1  == TC_u16,  "integer order incorrect");
static_assert(TC_i32+1  == TC_u32,  "integer order incorrect");
static_assert(TC_i64+1  == TC_u64,  "integer order incorrect");
static_assert(TC_i128+1 == TC_u128, "integer order incorrect");
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
  TF_KindF128,    // 128 bit floating point type
  TF_KindFunc,    // Functions
  TF_KindStruct,  // Structures
  TF_KindArray,   // Arrays
  TF_KindPointer, // Pointers
  TF_KindVector,  // Fixed width SIMD vector type
  TF_KindType,    // Types
  TF_Kind_MAX = TF_KindVector,
  TF_Kind_NBIT = ILOG2(TF_Kind_MAX) + 1,
} END_ENUM(TypeKind)

enum TypeFlags {
  // implicitly includes TF_Kind… (enum TypeKind)

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
  TF_Signed = 1 << B,       // is signed (integers only)
  #undef B
} END_ENUM(TypeFlags)

const char* TypeKindName(TypeKind); // e.g. "integer"

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


END_INTERFACE
