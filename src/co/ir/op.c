#include "../common.h"
#include "ir.h"

const char* const IROpNames[Op_MAX] = {
  // Do not edit. Generated by gen_ops.py
  "Nil",
  "NoOp",
  "Phi",
  "Copy",
  "Fun",
  "Arg",
  "Call",
  "ConstBool",
  "ConstI8",
  "ConstI16",
  "ConstI32",
  "ConstI64",
  "ConstF32",
  "ConstF64",
  "AddI8",
  "AddI16",
  "AddI32",
  "AddI64",
  "AddF32",
  "AddF64",
  "SubI8",
  "SubI16",
  "SubI32",
  "SubI64",
  "SubF32",
  "SubF64",
  "MulI8",
  "MulI16",
  "MulI32",
  "MulI64",
  "MulF32",
  "MulF64",
  "DivS8",
  "DivU8",
  "DivS16",
  "DivU16",
  "DivS32",
  "DivU32",
  "DivS64",
  "DivU64",
  "DivF32",
  "DivF64",
  "ModS8",
  "ModU8",
  "ModS16",
  "ModU16",
  "ModS32",
  "ModU32",
  "ModS64",
  "ModU64",
  "And8",
  "And16",
  "And32",
  "And64",
  "Or8",
  "Or16",
  "Or32",
  "Or64",
  "Xor8",
  "Xor16",
  "Xor32",
  "Xor64",
  "ShLI8x8",
  "ShLI8x16",
  "ShLI8x32",
  "ShLI8x64",
  "ShLI16x8",
  "ShLI16x16",
  "ShLI16x32",
  "ShLI16x64",
  "ShLI32x8",
  "ShLI32x16",
  "ShLI32x32",
  "ShLI32x64",
  "ShLI64x8",
  "ShLI64x16",
  "ShLI64x32",
  "ShLI64x64",
  "ShRS8x8",
  "ShRS8x16",
  "ShRS8x32",
  "ShRS8x64",
  "ShRS16x8",
  "ShRS16x16",
  "ShRS16x32",
  "ShRS16x64",
  "ShRS32x8",
  "ShRS32x16",
  "ShRS32x32",
  "ShRS32x64",
  "ShRS64x8",
  "ShRS64x16",
  "ShRS64x32",
  "ShRS64x64",
  "ShRU8x8",
  "ShRU8x16",
  "ShRU8x32",
  "ShRU8x64",
  "ShRU16x8",
  "ShRU16x16",
  "ShRU16x32",
  "ShRU16x64",
  "ShRU32x8",
  "ShRU32x16",
  "ShRU32x32",
  "ShRU32x64",
  "ShRU64x8",
  "ShRU64x16",
  "ShRU64x32",
  "ShRU64x64",
  "EqI8",
  "EqI16",
  "EqI32",
  "EqI64",
  "EqF32",
  "EqF64",
  "NEqI8",
  "NEqI16",
  "NEqI32",
  "NEqI64",
  "NEqF32",
  "NEqF64",
  "LessS8",
  "LessU8",
  "LessS16",
  "LessU16",
  "LessS32",
  "LessU32",
  "LessS64",
  "LessU64",
  "LessF32",
  "LessF64",
  "GreaterS8",
  "GreaterU8",
  "GreaterS16",
  "GreaterU16",
  "GreaterS32",
  "GreaterU32",
  "GreaterS64",
  "GreaterU64",
  "GreaterF32",
  "GreaterF64",
  "LEqS8",
  "LEqU8",
  "LEqS16",
  "LEqU16",
  "LEqS32",
  "LEqU32",
  "LEqS64",
  "LEqU64",
  "LEqF32",
  "LEqF64",
  "GEqS8",
  "GEqU8",
  "GEqS16",
  "GEqU16",
  "GEqS32",
  "GEqU32",
  "GEqS64",
  "GEqU64",
  "GEqF32",
  "GEqF64",
  "AndB",
  "OrB",
  "EqB",
  "NEqB",
  "NotB",
  "NegI8",
  "NegI16",
  "NegI32",
  "NegI64",
  "NegF32",
  "NegF64",
  "Compl8",
  "Compl16",
  "Compl32",
  "Compl64",
  "ConvS8to16",
  "ConvS8to32",
  "ConvS8to64",
  "ConvU8to16",
  "ConvU8to32",
  "ConvU8to64",
  "ConvS16to32",
  "ConvS16to64",
  "ConvU16to32",
  "ConvU16to64",
  "ConvS32to64",
  "ConvU32to64",
  "ConvI16to8",
  "ConvI32to8",
  "ConvI32to16",
  "ConvI64to8",
  "ConvI64to16",
  "ConvI64to32",
  "ConvS32toF32",
  "ConvS32toF64",
  "ConvS64toF32",
  "ConvS64toF64",
  "ConvU32toF32",
  "ConvU32toF64",
  "ConvU64toF32",
  "ConvU64toF64",
  "ConvF32toF64",
  "ConvF32toS32",
  "ConvF32toS64",
  "ConvF32toU32",
  "ConvF32toU64",
  "ConvF64toF32",
  "ConvF64toS32",
  "ConvF64toS64",
  "ConvF64toU32",
  "ConvF64toU64",
  "?", // Op_GENERIC_END
};


// _IROpConstMap maps TypeCode => IROp for constant materialization
const IROp _IROpConstMap[TypeCode_NUM_END] = {
  // Do not edit. Generated by gen_ops.py
  /* TypeCode_bool    = */ OpConstBool,
  /* TypeCode_int8    = */ OpConstI8,
  /* TypeCode_uint8   = */ OpConstI8,
  /* TypeCode_int16   = */ OpConstI16,
  /* TypeCode_uint16  = */ OpConstI16,
  /* TypeCode_int32   = */ OpConstI32,
  /* TypeCode_uint32  = */ OpConstI32,
  /* TypeCode_int64   = */ OpConstI64,
  /* TypeCode_uint64  = */ OpConstI64,
  /* TypeCode_float32 = */ OpConstF32,
  /* TypeCode_float64 = */ OpConstF64,
  /* TypeCode_int     = */ OpConstI32,
  /* TypeCode_uint    = */ OpConstI32,
  /* TypeCode_isize   = */ OpConstI64,
  /* TypeCode_usize   = */ OpConstI64,
};


const IROpDescr _IROpInfoMap[Op_MAX] = {
  // Do not edit. Generated by gen_ops.py
  { /* OpNil */ IROpFlagZeroWidth, TypeCode_nil, IRAuxNone },
  { /* OpNoOp */ IROpFlagZeroWidth, TypeCode_nil, IRAuxNone },
  { /* OpPhi */ IROpFlagZeroWidth, TypeCode_nil, IRAuxNone },
  { /* OpCopy */ IROpFlagZeroWidth, TypeCode_nil, IRAuxNone },
  { /* OpFun */ IROpFlagZeroWidth, TypeCode_nil, IRAuxMem },
  { /* OpArg */ IROpFlagNone, TypeCode_nil, IRAuxI32 },
  { /* OpCall */ IROpFlagCall, TypeCode_param1/*mem*/, IRAuxSym },
  { /* OpConstBool */ IROpFlagConstant, TypeCode_bool, IRAuxBool },
  { /* OpConstI8 */ IROpFlagConstant, TypeCode_param1/*i8*/, IRAuxI8 },
  { /* OpConstI16 */ IROpFlagConstant, TypeCode_param1/*i16*/, IRAuxI16 },
  { /* OpConstI32 */ IROpFlagConstant, TypeCode_param1/*i32*/, IRAuxI32 },
  { /* OpConstI64 */ IROpFlagConstant, TypeCode_param1/*i64*/, IRAuxI64 },
  { /* OpConstF32 */ IROpFlagConstant, TypeCode_float32, IRAuxI32 },
  { /* OpConstF64 */ IROpFlagConstant, TypeCode_float64, IRAuxI64 },
  { /* OpAddI8 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpAddI16 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpAddI32 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpAddI64 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpAddF32 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_float32, IRAuxNone },
  { /* OpAddF64 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_float64, IRAuxNone },
  { /* OpSubI8 */ IROpFlagResultInArg0, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpSubI16 */ IROpFlagResultInArg0, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpSubI32 */ IROpFlagResultInArg0, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpSubI64 */ IROpFlagResultInArg0, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpSubF32 */ IROpFlagResultInArg0, TypeCode_float32, IRAuxNone },
  { /* OpSubF64 */ IROpFlagResultInArg0, TypeCode_float64, IRAuxNone },
  { /* OpMulI8 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpMulI16 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpMulI32 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpMulI64 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpMulF32 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_float32, IRAuxNone },
  { /* OpMulF64 */ IROpFlagCommutative|IROpFlagResultInArg0, TypeCode_float64, IRAuxNone },
  { /* OpDivS8 */ IROpFlagResultInArg0, TypeCode_int8, IRAuxNone },
  { /* OpDivU8 */ IROpFlagResultInArg0, TypeCode_uint8, IRAuxNone },
  { /* OpDivS16 */ IROpFlagResultInArg0, TypeCode_int16, IRAuxNone },
  { /* OpDivU16 */ IROpFlagResultInArg0, TypeCode_uint16, IRAuxNone },
  { /* OpDivS32 */ IROpFlagResultInArg0, TypeCode_int32, IRAuxNone },
  { /* OpDivU32 */ IROpFlagResultInArg0, TypeCode_uint32, IRAuxNone },
  { /* OpDivS64 */ IROpFlagResultInArg0, TypeCode_int64, IRAuxNone },
  { /* OpDivU64 */ IROpFlagResultInArg0, TypeCode_uint64, IRAuxNone },
  { /* OpDivF32 */ IROpFlagResultInArg0, TypeCode_float32, IRAuxNone },
  { /* OpDivF64 */ IROpFlagResultInArg0, TypeCode_float64, IRAuxNone },
  { /* OpModS8 */ IROpFlagNone, TypeCode_int8, IRAuxNone },
  { /* OpModU8 */ IROpFlagNone, TypeCode_uint8, IRAuxNone },
  { /* OpModS16 */ IROpFlagNone, TypeCode_int16, IRAuxBool },
  { /* OpModU16 */ IROpFlagNone, TypeCode_uint16, IRAuxNone },
  { /* OpModS32 */ IROpFlagNone, TypeCode_int32, IRAuxBool },
  { /* OpModU32 */ IROpFlagNone, TypeCode_uint32, IRAuxNone },
  { /* OpModS64 */ IROpFlagNone, TypeCode_int64, IRAuxBool },
  { /* OpModU64 */ IROpFlagNone, TypeCode_uint64, IRAuxNone },
  { /* OpAnd8 */ IROpFlagCommutative, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpAnd16 */ IROpFlagCommutative, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpAnd32 */ IROpFlagCommutative, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpAnd64 */ IROpFlagCommutative, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpOr8 */ IROpFlagCommutative, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpOr16 */ IROpFlagCommutative, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpOr32 */ IROpFlagCommutative, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpOr64 */ IROpFlagCommutative, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpXor8 */ IROpFlagCommutative, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpXor16 */ IROpFlagCommutative, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpXor32 */ IROpFlagCommutative, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpXor64 */ IROpFlagCommutative, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpShLI8x8 */ IROpFlagNone, TypeCode_param1/*i8*/, IRAuxBool },
  { /* OpShLI8x16 */ IROpFlagNone, TypeCode_param1/*i8*/, IRAuxBool },
  { /* OpShLI8x32 */ IROpFlagNone, TypeCode_param1/*i8*/, IRAuxBool },
  { /* OpShLI8x64 */ IROpFlagNone, TypeCode_param1/*i8*/, IRAuxBool },
  { /* OpShLI16x8 */ IROpFlagNone, TypeCode_param1/*i16*/, IRAuxBool },
  { /* OpShLI16x16 */ IROpFlagNone, TypeCode_param1/*i16*/, IRAuxBool },
  { /* OpShLI16x32 */ IROpFlagNone, TypeCode_param1/*i16*/, IRAuxBool },
  { /* OpShLI16x64 */ IROpFlagNone, TypeCode_param1/*i16*/, IRAuxBool },
  { /* OpShLI32x8 */ IROpFlagNone, TypeCode_param1/*i32*/, IRAuxBool },
  { /* OpShLI32x16 */ IROpFlagNone, TypeCode_param1/*i32*/, IRAuxBool },
  { /* OpShLI32x32 */ IROpFlagNone, TypeCode_param1/*i32*/, IRAuxBool },
  { /* OpShLI32x64 */ IROpFlagNone, TypeCode_param1/*i32*/, IRAuxBool },
  { /* OpShLI64x8 */ IROpFlagNone, TypeCode_param1/*i64*/, IRAuxBool },
  { /* OpShLI64x16 */ IROpFlagNone, TypeCode_param1/*i64*/, IRAuxBool },
  { /* OpShLI64x32 */ IROpFlagNone, TypeCode_param1/*i64*/, IRAuxBool },
  { /* OpShLI64x64 */ IROpFlagNone, TypeCode_param1/*i64*/, IRAuxBool },
  { /* OpShRS8x8 */ IROpFlagNone, TypeCode_int8, IRAuxBool },
  { /* OpShRS8x16 */ IROpFlagNone, TypeCode_int8, IRAuxBool },
  { /* OpShRS8x32 */ IROpFlagNone, TypeCode_int8, IRAuxBool },
  { /* OpShRS8x64 */ IROpFlagNone, TypeCode_int8, IRAuxBool },
  { /* OpShRS16x8 */ IROpFlagNone, TypeCode_int16, IRAuxBool },
  { /* OpShRS16x16 */ IROpFlagNone, TypeCode_int16, IRAuxBool },
  { /* OpShRS16x32 */ IROpFlagNone, TypeCode_int16, IRAuxBool },
  { /* OpShRS16x64 */ IROpFlagNone, TypeCode_int16, IRAuxBool },
  { /* OpShRS32x8 */ IROpFlagNone, TypeCode_int32, IRAuxBool },
  { /* OpShRS32x16 */ IROpFlagNone, TypeCode_int32, IRAuxBool },
  { /* OpShRS32x32 */ IROpFlagNone, TypeCode_int32, IRAuxBool },
  { /* OpShRS32x64 */ IROpFlagNone, TypeCode_int32, IRAuxBool },
  { /* OpShRS64x8 */ IROpFlagNone, TypeCode_int64, IRAuxBool },
  { /* OpShRS64x16 */ IROpFlagNone, TypeCode_int64, IRAuxBool },
  { /* OpShRS64x32 */ IROpFlagNone, TypeCode_int64, IRAuxBool },
  { /* OpShRS64x64 */ IROpFlagNone, TypeCode_int64, IRAuxBool },
  { /* OpShRU8x8 */ IROpFlagNone, TypeCode_uint8, IRAuxBool },
  { /* OpShRU8x16 */ IROpFlagNone, TypeCode_uint8, IRAuxBool },
  { /* OpShRU8x32 */ IROpFlagNone, TypeCode_uint8, IRAuxBool },
  { /* OpShRU8x64 */ IROpFlagNone, TypeCode_uint8, IRAuxBool },
  { /* OpShRU16x8 */ IROpFlagNone, TypeCode_uint16, IRAuxBool },
  { /* OpShRU16x16 */ IROpFlagNone, TypeCode_uint16, IRAuxBool },
  { /* OpShRU16x32 */ IROpFlagNone, TypeCode_uint16, IRAuxBool },
  { /* OpShRU16x64 */ IROpFlagNone, TypeCode_uint16, IRAuxBool },
  { /* OpShRU32x8 */ IROpFlagNone, TypeCode_uint32, IRAuxBool },
  { /* OpShRU32x16 */ IROpFlagNone, TypeCode_uint32, IRAuxBool },
  { /* OpShRU32x32 */ IROpFlagNone, TypeCode_uint32, IRAuxBool },
  { /* OpShRU32x64 */ IROpFlagNone, TypeCode_uint32, IRAuxBool },
  { /* OpShRU64x8 */ IROpFlagNone, TypeCode_uint64, IRAuxBool },
  { /* OpShRU64x16 */ IROpFlagNone, TypeCode_uint64, IRAuxBool },
  { /* OpShRU64x32 */ IROpFlagNone, TypeCode_uint64, IRAuxBool },
  { /* OpShRU64x64 */ IROpFlagNone, TypeCode_uint64, IRAuxBool },
  { /* OpEqI8 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpEqI16 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpEqI32 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpEqI64 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpEqF32 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpEqF64 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqI8 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqI16 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqI32 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqI64 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqF32 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqF64 */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpLessS8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessU8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessS16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessU16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessS32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessU32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessS64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessU64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessF32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLessF64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterS8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterU8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterS16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterU16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterS32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterU32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterS64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterU64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterF32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGreaterF64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqS8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqU8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqS16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqU16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqS32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqU32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqS64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqU64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqF32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpLEqF64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqS8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqU8 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqS16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqU16 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqS32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqU32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqS64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqU64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqF32 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpGEqF64 */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpAndB */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpOrB */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpEqB */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNEqB */ IROpFlagCommutative, TypeCode_bool, IRAuxNone },
  { /* OpNotB */ IROpFlagNone, TypeCode_bool, IRAuxNone },
  { /* OpNegI8 */ IROpFlagNone, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpNegI16 */ IROpFlagNone, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpNegI32 */ IROpFlagNone, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpNegI64 */ IROpFlagNone, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpNegF32 */ IROpFlagNone, TypeCode_float32, IRAuxNone },
  { /* OpNegF64 */ IROpFlagNone, TypeCode_float64, IRAuxNone },
  { /* OpCompl8 */ IROpFlagNone, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpCompl16 */ IROpFlagNone, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpCompl32 */ IROpFlagNone, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpCompl64 */ IROpFlagNone, TypeCode_param1/*i64*/, IRAuxNone },
  { /* OpConvS8to16 */ IROpFlagNone, TypeCode_int16, IRAuxNone },
  { /* OpConvS8to32 */ IROpFlagNone, TypeCode_int32, IRAuxNone },
  { /* OpConvS8to64 */ IROpFlagNone, TypeCode_int64, IRAuxNone },
  { /* OpConvU8to16 */ IROpFlagNone, TypeCode_uint16, IRAuxNone },
  { /* OpConvU8to32 */ IROpFlagNone, TypeCode_uint32, IRAuxNone },
  { /* OpConvU8to64 */ IROpFlagNone, TypeCode_uint64, IRAuxNone },
  { /* OpConvS16to32 */ IROpFlagNone, TypeCode_int32, IRAuxNone },
  { /* OpConvS16to64 */ IROpFlagNone, TypeCode_int64, IRAuxNone },
  { /* OpConvU16to32 */ IROpFlagNone, TypeCode_uint32, IRAuxNone },
  { /* OpConvU16to64 */ IROpFlagNone, TypeCode_uint64, IRAuxNone },
  { /* OpConvS32to64 */ IROpFlagNone, TypeCode_int64, IRAuxNone },
  { /* OpConvU32to64 */ IROpFlagNone, TypeCode_uint64, IRAuxNone },
  { /* OpConvI16to8 */ IROpFlagLossy, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpConvI32to8 */ IROpFlagLossy, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpConvI32to16 */ IROpFlagLossy, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpConvI64to8 */ IROpFlagLossy, TypeCode_param1/*i8*/, IRAuxNone },
  { /* OpConvI64to16 */ IROpFlagLossy, TypeCode_param1/*i16*/, IRAuxNone },
  { /* OpConvI64to32 */ IROpFlagLossy, TypeCode_param1/*i32*/, IRAuxNone },
  { /* OpConvS32toF32 */ IROpFlagLossy, TypeCode_float32, IRAuxNone },
  { /* OpConvS32toF64 */ IROpFlagNone, TypeCode_float64, IRAuxNone },
  { /* OpConvS64toF32 */ IROpFlagLossy, TypeCode_float32, IRAuxNone },
  { /* OpConvS64toF64 */ IROpFlagLossy, TypeCode_float64, IRAuxNone },
  { /* OpConvU32toF32 */ IROpFlagLossy, TypeCode_float32, IRAuxNone },
  { /* OpConvU32toF64 */ IROpFlagNone, TypeCode_float64, IRAuxNone },
  { /* OpConvU64toF32 */ IROpFlagLossy, TypeCode_float32, IRAuxNone },
  { /* OpConvU64toF64 */ IROpFlagLossy, TypeCode_float64, IRAuxNone },
  { /* OpConvF32toF64 */ IROpFlagNone, TypeCode_float64, IRAuxNone },
  { /* OpConvF32toS32 */ IROpFlagLossy, TypeCode_int32, IRAuxNone },
  { /* OpConvF32toS64 */ IROpFlagLossy, TypeCode_int64, IRAuxNone },
  { /* OpConvF32toU32 */ IROpFlagLossy, TypeCode_uint32, IRAuxNone },
  { /* OpConvF32toU64 */ IROpFlagLossy, TypeCode_uint64, IRAuxNone },
  { /* OpConvF64toF32 */ IROpFlagLossy, TypeCode_float32, IRAuxNone },
  { /* OpConvF64toS32 */ IROpFlagLossy, TypeCode_int32, IRAuxNone },
  { /* OpConvF64toS64 */ IROpFlagLossy, TypeCode_int64, IRAuxNone },
  { /* OpConvF64toU32 */ IROpFlagLossy, TypeCode_uint32, IRAuxNone },
  { /* OpConvF64toU64 */ IROpFlagLossy, TypeCode_uint64, IRAuxNone },
  {0,0,0}, // Op_GENERIC_END
};


const IROp _IROpConvMap[TypeCode_NUM_END][TypeCode_NUM_END] = {
  // Do not edit. Generated by gen_ops.py
  { // bool -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpNil,
    /* -> uint8 */ OpNil,
    /* -> int16 */ OpNil,
    /* -> uint16 */ OpNil,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpNil,
    /* -> float64 */ OpNil,
    /* -> int */ OpNil,
    /* -> uint */ OpNil,
    /* -> isize */ OpNil,
    /* -> usize */ OpNil,
  },
  { // int8 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpNil,
    /* -> uint8 */ OpNil,
    /* -> int16 */ OpConvS8to16,
    /* -> uint16 */ OpNil,
    /* -> int32 */ OpConvS8to32,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpConvS8to64,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpNil,
    /* -> float64 */ OpNil,
    /* -> int */ OpConvS8to32,
    /* -> uint */ OpNil,
    /* -> isize */ OpConvS8to64,
    /* -> usize */ OpNil,
  },
  { // uint8 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpNil,
    /* -> uint8 */ OpNil,
    /* -> int16 */ OpNil,
    /* -> uint16 */ OpConvU8to16,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpConvU8to32,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpConvU8to64,
    /* -> float32 */ OpNil,
    /* -> float64 */ OpNil,
    /* -> int */ OpNil,
    /* -> uint */ OpConvU8to32,
    /* -> isize */ OpNil,
    /* -> usize */ OpConvU8to64,
  },
  { // int16 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI16to8,
    /* -> uint8 */ OpConvI16to8,
    /* -> int16 */ OpNil,
    /* -> uint16 */ OpNil,
    /* -> int32 */ OpConvS16to32,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpConvS16to64,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpNil,
    /* -> float64 */ OpNil,
    /* -> int */ OpConvS16to32,
    /* -> uint */ OpNil,
    /* -> isize */ OpConvS16to64,
    /* -> usize */ OpNil,
  },
  { // uint16 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI16to8,
    /* -> uint8 */ OpConvI16to8,
    /* -> int16 */ OpNil,
    /* -> uint16 */ OpNil,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpConvU16to32,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpConvU16to64,
    /* -> float32 */ OpNil,
    /* -> float64 */ OpNil,
    /* -> int */ OpNil,
    /* -> uint */ OpConvU16to32,
    /* -> isize */ OpNil,
    /* -> usize */ OpConvU16to64,
  },
  { // int32 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI32to8,
    /* -> uint8 */ OpConvI32to8,
    /* -> int16 */ OpConvI32to16,
    /* -> uint16 */ OpConvI32to16,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpConvS32to64,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpConvS32toF32,
    /* -> float64 */ OpConvS32toF64,
    /* -> int */ OpNil,
    /* -> uint */ OpNil,
    /* -> isize */ OpConvS32to64,
    /* -> usize */ OpNil,
  },
  { // uint32 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI32to8,
    /* -> uint8 */ OpConvI32to8,
    /* -> int16 */ OpConvI32to16,
    /* -> uint16 */ OpConvI32to16,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpConvU32to64,
    /* -> float32 */ OpConvU32toF32,
    /* -> float64 */ OpConvU32toF64,
    /* -> int */ OpNil,
    /* -> uint */ OpNil,
    /* -> isize */ OpNil,
    /* -> usize */ OpConvU32to64,
  },
  { // int64 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI64to8,
    /* -> uint8 */ OpConvI64to8,
    /* -> int16 */ OpConvI64to16,
    /* -> uint16 */ OpConvI64to16,
    /* -> int32 */ OpConvI64to32,
    /* -> uint32 */ OpConvI64to32,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpConvS64toF32,
    /* -> float64 */ OpConvS64toF64,
    /* -> int */ OpConvI64to32,
    /* -> uint */ OpConvI64to32,
    /* -> isize */ OpNil,
    /* -> usize */ OpNil,
  },
  { // uint64 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI64to8,
    /* -> uint8 */ OpConvI64to8,
    /* -> int16 */ OpConvI64to16,
    /* -> uint16 */ OpConvI64to16,
    /* -> int32 */ OpConvI64to32,
    /* -> uint32 */ OpConvI64to32,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpConvU64toF32,
    /* -> float64 */ OpConvU64toF64,
    /* -> int */ OpConvI64to32,
    /* -> uint */ OpConvI64to32,
    /* -> isize */ OpNil,
    /* -> usize */ OpNil,
  },
  { // float32 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpNil,
    /* -> uint8 */ OpNil,
    /* -> int16 */ OpNil,
    /* -> uint16 */ OpNil,
    /* -> int32 */ OpConvF32toS32,
    /* -> uint32 */ OpConvF32toU32,
    /* -> int64 */ OpConvF32toS64,
    /* -> uint64 */ OpConvF32toU64,
    /* -> float32 */ OpNil,
    /* -> float64 */ OpConvF32toF64,
    /* -> int */ OpConvF32toS32,
    /* -> uint */ OpConvF32toU32,
    /* -> isize */ OpConvF32toS64,
    /* -> usize */ OpConvF32toU64,
  },
  { // float64 -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpNil,
    /* -> uint8 */ OpNil,
    /* -> int16 */ OpNil,
    /* -> uint16 */ OpNil,
    /* -> int32 */ OpConvF64toS32,
    /* -> uint32 */ OpConvF64toU32,
    /* -> int64 */ OpConvF64toS64,
    /* -> uint64 */ OpConvF64toU64,
    /* -> float32 */ OpConvF64toF32,
    /* -> float64 */ OpNil,
    /* -> int */ OpConvF64toS32,
    /* -> uint */ OpConvF64toU32,
    /* -> isize */ OpConvF64toS64,
    /* -> usize */ OpConvF64toU64,
  },
  { // int -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI32to8,
    /* -> uint8 */ OpConvI32to8,
    /* -> int16 */ OpConvI32to16,
    /* -> uint16 */ OpConvI32to16,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpConvS32to64,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpConvS32toF32,
    /* -> float64 */ OpConvS32toF64,
    /* -> int */ OpNil,
    /* -> uint */ OpNil,
    /* -> isize */ OpConvS32to64,
    /* -> usize */ OpNil,
  },
  { // uint -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI32to8,
    /* -> uint8 */ OpConvI32to8,
    /* -> int16 */ OpConvI32to16,
    /* -> uint16 */ OpConvI32to16,
    /* -> int32 */ OpNil,
    /* -> uint32 */ OpNil,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpConvU32to64,
    /* -> float32 */ OpConvU32toF32,
    /* -> float64 */ OpConvU32toF64,
    /* -> int */ OpNil,
    /* -> uint */ OpNil,
    /* -> isize */ OpNil,
    /* -> usize */ OpConvU32to64,
  },
  { // isize -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI64to8,
    /* -> uint8 */ OpConvI64to8,
    /* -> int16 */ OpConvI64to16,
    /* -> uint16 */ OpConvI64to16,
    /* -> int32 */ OpConvI64to32,
    /* -> uint32 */ OpConvI64to32,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpConvS64toF32,
    /* -> float64 */ OpConvS64toF64,
    /* -> int */ OpConvI64to32,
    /* -> uint */ OpConvI64to32,
    /* -> isize */ OpNil,
    /* -> usize */ OpNil,
  },
  { // usize -> ...
    /* -> bool */ OpNil,
    /* -> int8 */ OpConvI64to8,
    /* -> uint8 */ OpConvI64to8,
    /* -> int16 */ OpConvI64to16,
    /* -> uint16 */ OpConvI64to16,
    /* -> int32 */ OpConvI64to32,
    /* -> uint32 */ OpConvI64to32,
    /* -> int64 */ OpNil,
    /* -> uint64 */ OpNil,
    /* -> float32 */ OpConvU64toF32,
    /* -> float64 */ OpConvU64toF64,
    /* -> int */ OpConvI64to32,
    /* -> uint */ OpConvI64to32,
    /* -> isize */ OpNil,
    /* -> usize */ OpNil,
  },
};


static TypeCode TypeCodeIntSignedCounterpart(TypeCode intType) {
  assertf(TypeCode_int8 <= intType && intType <= TypeCode_uint64,
    "unexpected intType %d \"%s\"", intType, TypeCodeName(intType));
  if ((TypeCodeFlags(intType) & TypeCodeFlagSigned) == 0) {
    // intType is not signed. Signed is just before in TypeCode enum.
    return intType-1;
  } else {
    // intType is signed. Unsigned is just after in TypeCode enum.
    return intType+1;
  }
}


IROp IROpConvertType(TypeCode fromType, TypeCode toType) {
  assert(fromType != toType);
  assert(fromType < TypeCode_NUM_END); // must be concrete
  assert(toType < TypeCode_NUM_END);   // must be concrete
  IROp op = _IROpConvMap[fromType][toType];
  dlog("%s -> %s: op: %s", TypeCodeName(fromType), TypeCodeName(toType), IROpName(op));
  if (op != OpNil) {
    return op;
  }
  // No conversion op. This means that either...
  // - two integers which differ in signed only => reinterpreted.
  // - two integers which differ in signed and size => reinterpreted and looked up again.
  // - boolean => failure (boolean cast must use boolean ops like < and ==)
  //
  // To understand the types, load their flags
  auto fromfl = TypeCodeFlags(fromType);
  auto tofl   = TypeCodeFlags(toType);
  if (fromfl & TypeCodeFlagInt && tofl & TypeCodeFlagInt) {
    // from and to are ints
    if ((fromfl & TypeCodeFlagSigned) != (tofl & TypeCodeFlagSigned)) {
      // one of them is signed the other is not
      if ((fromfl & TypeCodeFlagSizeMask) < (tofl & TypeCodeFlagSizeMask)) {
        // from is smaller than to and differs in signed. Reinterpret and look up again.
        fromType = TypeCodeIntSignedCounterpart(fromType);
        return _IROpConvMap[fromType][toType];
      } else if ((fromfl & TypeCodeFlagSizeMask) == (tofl & TypeCodeFlagSizeMask)) {
        // same size int with only signed difference. Reinterpret. No cast needed.
        dlog("TODO IROpConvertType signed-only conversion: %s reinterpreted as %s",
          TypeCodeName(fromType),
          TypeCodeName(TypeCodeIntSignedCounterpart(fromType)));
        // TODO NoOp op?
        // Maybe we just avoid calling IROpConvertType for type casts of integers of same size...
        //
        // Idea: Before even calling this function (ints only), check signed flags and
        // if the signed type is different, simply reinterpret ("lie" about) the type as the
        // destination "signed version". I.e. int8 -> uint32, "lie" and say that the from type
        // is uint8 instead of int8.
        //
      }
    }
    // all other valid conversions should have an op, like for example int16 -> int32.
  }
  return OpNil;
}


R_TEST(ir_op) {
  //
  // this test is work in progress
  return;
  //
  // printf("--------------------------------------------------\n");
  auto mem = MemLinearAlloc();
  #define mknode(t) NewNode(mem, (t))

  // i16 -> uint32 (extension with signed change)

  // TypeCode type1 = TypeCode_int32;
  // TypeCode type2 = TypeCode_int16;

  // IROp op = IROpConv(TypeCode_int16, TypeCode_int32);
  // dlog("int16 -> int32: op: %s", IROpName(op));

  auto fromType = TypeCode_int16;
  auto toType   = TypeCode_uint32;
  auto op = IROpConvertType(fromType, toType);
  dlog("IROpConvertType(%s -> %s) => %s  lossy? %s",
    TypeCodeName(fromType), TypeCodeName(toType), IROpName(op),
    _IROpInfoMap[op].flags & IROpFlagLossy ? "yes" : "no");


  // _( int8      , '1', TypeCodeFlagSize1 | TypeCodeFlagInt | TypeCodeFlagSigned ) \
  // _( uint8     , '2', TypeCodeFlagSize1 | TypeCodeFlagInt ) \
  // _( int16     , '3', TypeCodeFlagSize2 | TypeCodeFlagInt | TypeCodeFlagSigned ) \
  // _( uint16    , '4', TypeCodeFlagSize2 | TypeCodeFlagInt ) \
  // _( int32     , '5', TypeCodeFlagSize4 | TypeCodeFlagInt | TypeCodeFlagSigned ) \
  // _( uint32    , '6', TypeCodeFlagSize4 | TypeCodeFlagInt ) \
  // _( int64     , '7', TypeCodeFlagSize8 | TypeCodeFlagInt | TypeCodeFlagSigned ) \
  // _( uint64    , '8', TypeCodeFlagSize8 | TypeCodeFlagInt ) \
  // _( float32   , 'f', TypeCodeFlagSize4 | TypeCodeFlagFloat ) \
  // _( float64   , 'F', TypeCodeFlagSize8 | TypeCodeFlagFloat ) \
  //


  // TypeCode type1 = TypeCode_int32;
  // TypeCode type2 = TypeCode_int16;
  // dlog("type1: 0x%X", type1);
  // dlog("type2: 0x%X", type2);
  // u32 key = ((u32)type1 << 16) | (u32)type2;
  // dlog("key: 0x%X", key);
  // dlog("t1 0x%X", key >> 16);
  // dlog("t2 0x%X", key & 0xFFFF);

  // TODO: rewrite these tests to use the new matrix table:
  //
  // // construct an AST node: uint32:(Op +)
  // auto uint32TypeNode = mknode(NBasicType);
  // uint32TypeNode->t.basic.typeCode = TypeCode_uint32;
  //
  // auto addUInt32OpNode = mknode(NOp);
  // addUInt32OpNode->op.op = TPlus;
  // addUInt32OpNode->type = uint32TypeNode;
  //
  // auto n = addUInt32OpNode;
  //
  // int index1 = n->op.op - T_PRIM_OPS_START - 1;
  // dlog("index1: %d", index1);
  //
  // auto irOpTable = astToIROpTable[index1];
  // // dlog("astToIROpTable_Add: %p", astToIROpTable_Add);
  // // dlog("astToIROpTable_Sub: %p", astToIROpTable_Sub);
  // // dlog("astToIROpTable_Mul: %p", astToIROpTable_Mul);
  // // dlog("astToIROpTable_Div: %p", astToIROpTable_Div);
  // // dlog("matched irOpTable:  %p", irOpTable);
  // assert(irOpTable != NULL);
  //
  // auto irop = irOpTable[n->type->t.basic.typeCode];
  // dlog("irop: TypeCode %c #%d => #%u %s",
  //   TypeCodeEncoding(n->type->t.basic.typeCode), n->type->t.basic.typeCode,
  //   irop, IROpNames[irop]);
  // assert(irop == OpAddI32);
  //
  // assert(IROpFromASTOp2(n->op.op, n->type->t.basic.typeCode, n->type->t.basic.typeCode)
  //        == OpAddI32);

  // IROp IROpFromAST(Tok tok, TypeCode type1, TypeCode type2)

  MemLinearFree(mem);
  // printf("--------------------------------------------------\n");
}
