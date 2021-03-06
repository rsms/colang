#include "../common.h"
#include "ir.h"
#include "irbuilder.h"

IROp IROpFromAST(Tok tok, TypeCode type1, TypeCode type2) {
  assert(tok > T_PRIM_OPS_START);  // op must be an operator token
  assert(tok < T_PRIM_OPS_END);
  assertf(type1 < TypeCode_NUM_END,
    "t must be a concrete, basic numeric type (got %s)",
    TypeCodeName(type1));
  assert(type2 == TypeCode_nil || type2 < TypeCode_NUM_END);
  //!BEGIN_AST_TO_IR_OP_SWITCHES
  // Do not edit. Generated by gen_ops.py
  switch (type1) {
    case TypeCode_bool:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TExcalm : return OpNotB ;// bool -> bool
          default: return OpNil;
        }
        case TypeCode_bool: switch (tok) {
          case TAnd  : return OpAndB ;// bool bool -> bool
          case TPipe : return OpOrB  ;// bool bool -> bool
          case TEq   : return OpEqB  ;// bool bool -> bool
          case TNEq  : return OpNEqB ;// bool bool -> bool
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_i8:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI8  ;// i8 -> i8
          case THat   : return OpCompl8 ;// i8 -> i8
          default: return OpNil;
        }
        case TypeCode_i8: switch (tok) {
          case TSlash : return OpDivS8     ;// s8 s8 -> s8
          case TLt    : return OpLessS8    ;// s8 s8 -> bool
          case TLEq   : return OpLEqS8     ;// s8 s8 -> bool
          case TGt    : return OpGreaterS8 ;// s8 s8 -> bool
          case TGEq   : return OpGEqS8     ;// s8 s8 -> bool
          case TStar  : return OpMulI8     ;// i8 i8 -> i8
          case TAnd   : return OpAnd8      ;// i8 i8 -> i8
          case TPlus  : return OpAddI8     ;// i8 i8 -> i8
          case TMinus : return OpSubI8     ;// i8 i8 -> i8
          case TPipe  : return OpOr8       ;// i8 i8 -> i8
          case TEq    : return OpEqI8      ;// i8 i8 -> bool
          case TNEq   : return OpNEqI8     ;// i8 i8 -> bool
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr   : return OpShRS8x8 ;// s8 u8 -> s8
          case TShl   : return OpShLI8x8 ;// i8 u8 -> i8
          case TStar  : return OpMulI8   ;// i8 i8 -> i8
          case TAnd   : return OpAnd8    ;// i8 i8 -> i8
          case TPlus  : return OpAddI8   ;// i8 i8 -> i8
          case TMinus : return OpSubI8   ;// i8 i8 -> i8
          case TPipe  : return OpOr8     ;// i8 i8 -> i8
          case TEq    : return OpEqI8    ;// i8 i8 -> bool
          case TNEq   : return OpNEqI8   ;// i8 i8 -> bool
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr : return OpShRS8x16 ;// s8 u16 -> s8
          case TShl : return OpShLI8x16 ;// i8 u16 -> i8
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr : return OpShRS8x32 ;// s8 u32 -> s8
          case TShl : return OpShLI8x32 ;// i8 u32 -> i8
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr : return OpShRS8x64 ;// s8 u64 -> s8
          case TShl : return OpShLI8x64 ;// i8 u64 -> i8
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_u8:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI8  ;// i8 -> i8
          case THat   : return OpCompl8 ;// i8 -> i8
          default: return OpNil;
        }
        case TypeCode_i8: switch (tok) {
          case TStar  : return OpMulI8 ;// i8 i8 -> i8
          case TAnd   : return OpAnd8  ;// i8 i8 -> i8
          case TPlus  : return OpAddI8 ;// i8 i8 -> i8
          case TMinus : return OpSubI8 ;// i8 i8 -> i8
          case TPipe  : return OpOr8   ;// i8 i8 -> i8
          case TEq    : return OpEqI8  ;// i8 i8 -> bool
          case TNEq   : return OpNEqI8 ;// i8 i8 -> bool
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TSlash : return OpDivU8     ;// u8 u8 -> u8
          case TShr   : return OpShRU8x8   ;// u8 u8 -> u8
          case TLt    : return OpLessU8    ;// u8 u8 -> bool
          case TLEq   : return OpLEqU8     ;// u8 u8 -> bool
          case TGt    : return OpGreaterU8 ;// u8 u8 -> bool
          case TGEq   : return OpGEqU8     ;// u8 u8 -> bool
          case TShl   : return OpShLI8x8   ;// i8 u8 -> i8
          case TStar  : return OpMulI8     ;// i8 i8 -> i8
          case TAnd   : return OpAnd8      ;// i8 i8 -> i8
          case TPlus  : return OpAddI8     ;// i8 i8 -> i8
          case TMinus : return OpSubI8     ;// i8 i8 -> i8
          case TPipe  : return OpOr8       ;// i8 i8 -> i8
          case TEq    : return OpEqI8      ;// i8 i8 -> bool
          case TNEq   : return OpNEqI8     ;// i8 i8 -> bool
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr : return OpShRU8x16 ;// u8 u16 -> u8
          case TShl : return OpShLI8x16 ;// i8 u16 -> i8
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr : return OpShRU8x32 ;// u8 u32 -> u8
          case TShl : return OpShLI8x32 ;// i8 u32 -> i8
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr : return OpShRU8x64 ;// u8 u64 -> u8
          case TShl : return OpShLI8x64 ;// i8 u64 -> i8
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_i16:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI16  ;// i16 -> i16
          case THat   : return OpCompl16 ;// i16 -> i16
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr : return OpShRS16x8 ;// s16 u8 -> s16
          case TShl : return OpShLI16x8 ;// i16 u8 -> i16
          default: return OpNil;
        }
        case TypeCode_i16: switch (tok) {
          case TSlash : return OpDivS16     ;// s16 s16 -> s16
          case TLt    : return OpLessS16    ;// s16 s16 -> bool
          case TLEq   : return OpLEqS16     ;// s16 s16 -> bool
          case TGt    : return OpGreaterS16 ;// s16 s16 -> bool
          case TGEq   : return OpGEqS16     ;// s16 s16 -> bool
          case TStar  : return OpMulI16     ;// i16 i16 -> i16
          case TAnd   : return OpAnd16      ;// i16 i16 -> i16
          case TPlus  : return OpAddI16     ;// i16 i16 -> i16
          case TMinus : return OpSubI16     ;// i16 i16 -> i16
          case TPipe  : return OpOr16       ;// i16 i16 -> i16
          case TEq    : return OpEqI16      ;// i16 i16 -> bool
          case TNEq   : return OpNEqI16     ;// i16 i16 -> bool
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr   : return OpShRS16x16 ;// s16 u16 -> s16
          case TShl   : return OpShLI16x16 ;// i16 u16 -> i16
          case TStar  : return OpMulI16    ;// i16 i16 -> i16
          case TAnd   : return OpAnd16     ;// i16 i16 -> i16
          case TPlus  : return OpAddI16    ;// i16 i16 -> i16
          case TMinus : return OpSubI16    ;// i16 i16 -> i16
          case TPipe  : return OpOr16      ;// i16 i16 -> i16
          case TEq    : return OpEqI16     ;// i16 i16 -> bool
          case TNEq   : return OpNEqI16    ;// i16 i16 -> bool
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr : return OpShRS16x32 ;// s16 u32 -> s16
          case TShl : return OpShLI16x32 ;// i16 u32 -> i16
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr : return OpShRS16x64 ;// s16 u64 -> s16
          case TShl : return OpShLI16x64 ;// i16 u64 -> i16
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_u16:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI16  ;// i16 -> i16
          case THat   : return OpCompl16 ;// i16 -> i16
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr : return OpShRU16x8 ;// u16 u8 -> u16
          case TShl : return OpShLI16x8 ;// i16 u8 -> i16
          default: return OpNil;
        }
        case TypeCode_i16: switch (tok) {
          case TStar  : return OpMulI16 ;// i16 i16 -> i16
          case TAnd   : return OpAnd16  ;// i16 i16 -> i16
          case TPlus  : return OpAddI16 ;// i16 i16 -> i16
          case TMinus : return OpSubI16 ;// i16 i16 -> i16
          case TPipe  : return OpOr16   ;// i16 i16 -> i16
          case TEq    : return OpEqI16  ;// i16 i16 -> bool
          case TNEq   : return OpNEqI16 ;// i16 i16 -> bool
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TSlash : return OpDivU16     ;// u16 u16 -> u16
          case TShr   : return OpShRU16x16  ;// u16 u16 -> u16
          case TLt    : return OpLessU16    ;// u16 u16 -> bool
          case TLEq   : return OpLEqU16     ;// u16 u16 -> bool
          case TGt    : return OpGreaterU16 ;// u16 u16 -> bool
          case TGEq   : return OpGEqU16     ;// u16 u16 -> bool
          case TShl   : return OpShLI16x16  ;// i16 u16 -> i16
          case TStar  : return OpMulI16     ;// i16 i16 -> i16
          case TAnd   : return OpAnd16      ;// i16 i16 -> i16
          case TPlus  : return OpAddI16     ;// i16 i16 -> i16
          case TMinus : return OpSubI16     ;// i16 i16 -> i16
          case TPipe  : return OpOr16       ;// i16 i16 -> i16
          case TEq    : return OpEqI16      ;// i16 i16 -> bool
          case TNEq   : return OpNEqI16     ;// i16 i16 -> bool
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr : return OpShRU16x32 ;// u16 u32 -> u16
          case TShl : return OpShLI16x32 ;// i16 u32 -> i16
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr : return OpShRU16x64 ;// u16 u64 -> u16
          case TShl : return OpShLI16x64 ;// i16 u64 -> i16
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_int:
    case TypeCode_i32:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI32  ;// i32 -> i32
          case THat   : return OpCompl32 ;// i32 -> i32
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr : return OpShRS32x8 ;// s32 u8 -> s32
          case TShl : return OpShLI32x8 ;// i32 u8 -> i32
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr : return OpShRS32x16 ;// s32 u16 -> s32
          case TShl : return OpShLI32x16 ;// i32 u16 -> i32
          default: return OpNil;
        }
        case TypeCode_int:
        case TypeCode_i32: switch (tok) {
          case TSlash : return OpDivS32     ;// s32 s32 -> s32
          case TLt    : return OpLessS32    ;// s32 s32 -> bool
          case TLEq   : return OpLEqS32     ;// s32 s32 -> bool
          case TGt    : return OpGreaterS32 ;// s32 s32 -> bool
          case TGEq   : return OpGEqS32     ;// s32 s32 -> bool
          case TStar  : return OpMulI32     ;// i32 i32 -> i32
          case TAnd   : return OpAnd32      ;// i32 i32 -> i32
          case TPlus  : return OpAddI32     ;// i32 i32 -> i32
          case TMinus : return OpSubI32     ;// i32 i32 -> i32
          case TPipe  : return OpOr32       ;// i32 i32 -> i32
          case TEq    : return OpEqI32      ;// i32 i32 -> bool
          case TNEq   : return OpNEqI32     ;// i32 i32 -> bool
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr   : return OpShRS32x32 ;// s32 u32 -> s32
          case TShl   : return OpShLI32x32 ;// i32 u32 -> i32
          case TStar  : return OpMulI32    ;// i32 i32 -> i32
          case TAnd   : return OpAnd32     ;// i32 i32 -> i32
          case TPlus  : return OpAddI32    ;// i32 i32 -> i32
          case TMinus : return OpSubI32    ;// i32 i32 -> i32
          case TPipe  : return OpOr32      ;// i32 i32 -> i32
          case TEq    : return OpEqI32     ;// i32 i32 -> bool
          case TNEq   : return OpNEqI32    ;// i32 i32 -> bool
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr : return OpShRS32x64 ;// s32 u64 -> s32
          case TShl : return OpShLI32x64 ;// i32 u64 -> i32
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_uint:
    case TypeCode_u32:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI32  ;// i32 -> i32
          case THat   : return OpCompl32 ;// i32 -> i32
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr : return OpShRU32x8 ;// u32 u8 -> u32
          case TShl : return OpShLI32x8 ;// i32 u8 -> i32
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr : return OpShRU32x16 ;// u32 u16 -> u32
          case TShl : return OpShLI32x16 ;// i32 u16 -> i32
          default: return OpNil;
        }
        case TypeCode_int:
        case TypeCode_i32: switch (tok) {
          case TStar  : return OpMulI32 ;// i32 i32 -> i32
          case TAnd   : return OpAnd32  ;// i32 i32 -> i32
          case TPlus  : return OpAddI32 ;// i32 i32 -> i32
          case TMinus : return OpSubI32 ;// i32 i32 -> i32
          case TPipe  : return OpOr32   ;// i32 i32 -> i32
          case TEq    : return OpEqI32  ;// i32 i32 -> bool
          case TNEq   : return OpNEqI32 ;// i32 i32 -> bool
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TSlash : return OpDivU32     ;// u32 u32 -> u32
          case TShr   : return OpShRU32x32  ;// u32 u32 -> u32
          case TLt    : return OpLessU32    ;// u32 u32 -> bool
          case TLEq   : return OpLEqU32     ;// u32 u32 -> bool
          case TGt    : return OpGreaterU32 ;// u32 u32 -> bool
          case TGEq   : return OpGEqU32     ;// u32 u32 -> bool
          case TShl   : return OpShLI32x32  ;// i32 u32 -> i32
          case TStar  : return OpMulI32     ;// i32 i32 -> i32
          case TAnd   : return OpAnd32      ;// i32 i32 -> i32
          case TPlus  : return OpAddI32     ;// i32 i32 -> i32
          case TMinus : return OpSubI32     ;// i32 i32 -> i32
          case TPipe  : return OpOr32       ;// i32 i32 -> i32
          case TEq    : return OpEqI32      ;// i32 i32 -> bool
          case TNEq   : return OpNEqI32     ;// i32 i32 -> bool
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr : return OpShRU32x64 ;// u32 u64 -> u32
          case TShl : return OpShLI32x64 ;// i32 u64 -> i32
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_i64:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI64  ;// i64 -> i64
          case THat   : return OpCompl64 ;// i64 -> i64
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr : return OpShRS64x8 ;// s64 u8 -> s64
          case TShl : return OpShLI64x8 ;// i64 u8 -> i64
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr : return OpShRS64x16 ;// s64 u16 -> s64
          case TShl : return OpShLI64x16 ;// i64 u16 -> i64
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr : return OpShRS64x32 ;// s64 u32 -> s64
          case TShl : return OpShLI64x32 ;// i64 u32 -> i64
          default: return OpNil;
        }
        case TypeCode_i64: switch (tok) {
          case TSlash : return OpDivS64     ;// s64 s64 -> s64
          case TLt    : return OpLessS64    ;// s64 s64 -> bool
          case TLEq   : return OpLEqS64     ;// s64 s64 -> bool
          case TGt    : return OpGreaterS64 ;// s64 s64 -> bool
          case TGEq   : return OpGEqS64     ;// s64 s64 -> bool
          case TStar  : return OpMulI64     ;// i64 i64 -> i64
          case TAnd   : return OpAnd64      ;// i64 i64 -> i64
          case TPlus  : return OpAddI64     ;// i64 i64 -> i64
          case TMinus : return OpSubI64     ;// i64 i64 -> i64
          case TPipe  : return OpOr64       ;// i64 i64 -> i64
          case TEq    : return OpEqI64      ;// i64 i64 -> bool
          case TNEq   : return OpNEqI64     ;// i64 i64 -> bool
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TShr   : return OpShRS64x64 ;// s64 u64 -> s64
          case TShl   : return OpShLI64x64 ;// i64 u64 -> i64
          case TStar  : return OpMulI64    ;// i64 i64 -> i64
          case TAnd   : return OpAnd64     ;// i64 i64 -> i64
          case TPlus  : return OpAddI64    ;// i64 i64 -> i64
          case TMinus : return OpSubI64    ;// i64 i64 -> i64
          case TPipe  : return OpOr64      ;// i64 i64 -> i64
          case TEq    : return OpEqI64     ;// i64 i64 -> bool
          case TNEq   : return OpNEqI64    ;// i64 i64 -> bool
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_u64:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegI64  ;// i64 -> i64
          case THat   : return OpCompl64 ;// i64 -> i64
          default: return OpNil;
        }
        case TypeCode_u8: switch (tok) {
          case TShr : return OpShRU64x8 ;// u64 u8 -> u64
          case TShl : return OpShLI64x8 ;// i64 u8 -> i64
          default: return OpNil;
        }
        case TypeCode_u16: switch (tok) {
          case TShr : return OpShRU64x16 ;// u64 u16 -> u64
          case TShl : return OpShLI64x16 ;// i64 u16 -> i64
          default: return OpNil;
        }
        case TypeCode_uint:
        case TypeCode_u32: switch (tok) {
          case TShr : return OpShRU64x32 ;// u64 u32 -> u64
          case TShl : return OpShLI64x32 ;// i64 u32 -> i64
          default: return OpNil;
        }
        case TypeCode_i64: switch (tok) {
          case TStar  : return OpMulI64 ;// i64 i64 -> i64
          case TAnd   : return OpAnd64  ;// i64 i64 -> i64
          case TPlus  : return OpAddI64 ;// i64 i64 -> i64
          case TMinus : return OpSubI64 ;// i64 i64 -> i64
          case TPipe  : return OpOr64   ;// i64 i64 -> i64
          case TEq    : return OpEqI64  ;// i64 i64 -> bool
          case TNEq   : return OpNEqI64 ;// i64 i64 -> bool
          default: return OpNil;
        }
        case TypeCode_u64: switch (tok) {
          case TSlash : return OpDivU64     ;// u64 u64 -> u64
          case TShr   : return OpShRU64x64  ;// u64 u64 -> u64
          case TLt    : return OpLessU64    ;// u64 u64 -> bool
          case TLEq   : return OpLEqU64     ;// u64 u64 -> bool
          case TGt    : return OpGreaterU64 ;// u64 u64 -> bool
          case TGEq   : return OpGEqU64     ;// u64 u64 -> bool
          case TShl   : return OpShLI64x64  ;// i64 u64 -> i64
          case TStar  : return OpMulI64     ;// i64 i64 -> i64
          case TAnd   : return OpAnd64      ;// i64 i64 -> i64
          case TPlus  : return OpAddI64     ;// i64 i64 -> i64
          case TMinus : return OpSubI64     ;// i64 i64 -> i64
          case TPipe  : return OpOr64       ;// i64 i64 -> i64
          case TEq    : return OpEqI64      ;// i64 i64 -> bool
          case TNEq   : return OpNEqI64     ;// i64 i64 -> bool
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_f32:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegF32 ;// f32 -> f32
          default: return OpNil;
        }
        case TypeCode_f32: switch (tok) {
          case TStar  : return OpMulF32     ;// f32 f32 -> f32
          case TSlash : return OpDivF32     ;// f32 f32 -> f32
          case TPlus  : return OpAddF32     ;// f32 f32 -> f32
          case TMinus : return OpSubF32     ;// f32 f32 -> f32
          case TEq    : return OpEqF32      ;// f32 f32 -> bool
          case TNEq   : return OpNEqF32     ;// f32 f32 -> bool
          case TLt    : return OpLessF32    ;// f32 f32 -> bool
          case TLEq   : return OpLEqF32     ;// f32 f32 -> bool
          case TGt    : return OpGreaterF32 ;// f32 f32 -> bool
          case TGEq   : return OpGEqF32     ;// f32 f32 -> bool
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    case TypeCode_f64:
      switch (type2) {
        case TypeCode_nil: switch (tok) {
          case TMinus : return OpNegF64 ;// f64 -> f64
          default: return OpNil;
        }
        case TypeCode_f64: switch (tok) {
          case TStar  : return OpMulF64     ;// f64 f64 -> f64
          case TSlash : return OpDivF64     ;// f64 f64 -> f64
          case TPlus  : return OpAddF64     ;// f64 f64 -> f64
          case TMinus : return OpSubF64     ;// f64 f64 -> f64
          case TEq    : return OpEqF64      ;// f64 f64 -> bool
          case TNEq   : return OpNEqF64     ;// f64 f64 -> bool
          case TLt    : return OpLessF64    ;// f64 f64 -> bool
          case TLEq   : return OpLEqF64     ;// f64 f64 -> bool
          case TGt    : return OpGreaterF64 ;// f64 f64 -> bool
          case TGEq   : return OpGEqF64     ;// f64 f64 -> bool
          default: return OpNil;
        }
        default: return OpNil;
      } // switch (type2)
    default: return OpNil;
  } // switch (type1)
    //!END_AST_TO_IR_OP_SWITCHES
}

R_TEST(ast_ir_opmap) {
  // IROp IROpFromAST(Tok tok, TypeCode type1, TypeCode type2)

  asserteq(IROpFromAST(TMinus, TypeCode_i32, TypeCode_nil), OpNegI32);

  asserteq(IROpFromAST(TPlus, TypeCode_i8,  TypeCode_i8),  OpAddI8);
  asserteq(IROpFromAST(TPlus, TypeCode_u8,  TypeCode_u8),  OpAddI8);
  asserteq(IROpFromAST(TPlus, TypeCode_i16, TypeCode_i16), OpAddI16);
  asserteq(IROpFromAST(TPlus, TypeCode_u16, TypeCode_u16), OpAddI16);
  asserteq(IROpFromAST(TPlus, TypeCode_i32, TypeCode_i32), OpAddI32);
  asserteq(IROpFromAST(TPlus, TypeCode_u32, TypeCode_u32), OpAddI32);
  asserteq(IROpFromAST(TPlus, TypeCode_i64, TypeCode_i64), OpAddI64);
  asserteq(IROpFromAST(TPlus, TypeCode_u64, TypeCode_u64), OpAddI64);
}
