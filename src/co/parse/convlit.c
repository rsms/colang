#include "../common.h"
#include "parse.h"

// DEBUG_MODULE: define to enable debug logging
//#define DEBUG_MODULE "[convlit] "

#ifdef DEBUG_MODULE
  #define dlog_mod(format, ...) dlog(DEBUG_MODULE format, ##__VA_ARGS__)
#else
  #define dlog_mod(...) do{}while(0)
#endif



static const i64 min_intval[TypeCode_NUM_END] = {
  (i64)0,                   // bool
  (i8)-0x80,               // int8
  (i64)0,                   // uint8
  (i16)-0x8000,             // int16
  (i64)0,                   // uint16
  (i32)-0x80000000,         // int32
  (i64)0,                   // uint32
  (i64)-0x8000000000000000, // int64
  (i64)0,                   // uint64
  (i64)0,                   // TODO f32
  (i64)0,                   // TODO f64
  (i32)-0x80000000,         // int == int32
  (i64)0,                   // uint == uint32
};

static const u64 max_intval[TypeCode_NUM_END] = {
  1,                   // bool
  0x7f,                // int8
  0xff,                // uint8
  0x7fff,              // int16
  0xffff,              // uint16
  0x7fffffff,          // int32
  0xffffffff,          // uint32
  0x7fffffffffffffff,  // int64
  0xffffffffffffffff,  // uint64
  0,                   // TODO f32
  0,                   // TODO f64
  0x7fffffff,          // int == int32
  0xffffffff,          // uint == uint32
};

// convert an intrinsic numeric value v to an integer of type tc.
// Note: tc is the target type, not the type of v.
static bool convval_to_int(Build* b, Node* srcnode, NVal* v, TypeCode tc) {
  assert(TypeCodeIsInt(tc));
  switch (v->ct) {
    case CType_int:
      // int -> int; check overflow and simply leave as-is (reinterpret.)
      if (R_UNLIKELY((i64)v->i < min_intval[tc] || max_intval[tc] < v->i)) {
        auto nval = NValFmt(str_new(16), *v);
        build_errf(b, NodePosSpan(srcnode), "constant %s overflows %s", nval, TypeCodeName(tc));
        str_free(nval);
      }
      return true;

    case CType_rune:
    case CType_float:
    case CType_str:
    case CType_bool:
    case CType_nil:
      dlog("TODO convert %s -> %s", CTypeName(v->ct), TypeCodeName(tc));
      break;

    case CType_INVALID:
      assertf(0, "invalid CType (srcnode %s)", fmtnode(srcnode));
      break;
  }
  return false;
}


// convert an intrinsic numeric value v to a floating point number of type tc
static bool convval_to_float(Build* b, Node* srcnode, NVal* v, TypeCode t) {
  dlog("convlit TODO float");
  return false;
}


// convval converts v into a representation appropriate for targetType.
// If no such representation exists, return false.
static bool convval(Build* b, Node* srcnode, NVal* v, TypeCode tc) {
  // TODO make use of 'explicit' to allow "greater" conversions like for example int -> str.
  auto tcfl = TypeCodeFlags(tc);

  // * -> integer
  if (tcfl & TypeCodeFlagInt)
    return convval_to_int(b, srcnode, v, tc);

  // * -> float
  if (tcfl & TypeCodeFlagFloat)
    return convval_to_float(b, srcnode, v, tc);

  dlog("convlit TODO TypeCode %s", TypeCodeName(tc));
  return false;
}


static Node* report_non_convertible(Build* b, Node* n, Type* t) {
  const char* msg = "%s is not compatible with type %s";
  if (n->type == Type_ideal)
    msg = "ideal value %s can not be interpreted as type %s";
  build_errf(b, NodePosSpan(n), msg, fmtnode(n), fmtnode(t));
  return n;
}


// convlit converts an expression n to type t.
// If n is already of type t, n is simply returned.
Node* convlit(Build* b, Node* n, Type* t, ConvlitFlags fl) {
  assert(t != NULL);
  assert(t != Type_ideal);
  assert(NodeKindIsType(t->kind));

  dlog_mod("[%s] N%s %s of type %s as %s",
    (fl & ConvlitExplicit) ? "explicit" : "implicit",
    NodeKindName(n->kind), fmtnode(n), fmtnode(n->type), fmtnode(t));

  if ((fl & ConvlitRelaxedType) &&
    n->type != NULL && n->type != Type_nil && n->type != Type_ideal)
  {
    if ((fl & ConvlitExplicit) == 0) {
      // in implicit mode, if something is typed already, we don't try and convert the type.
      dlog_mod("[implicit] no-op -- n is already typed: %s", fmtnode(n->type));
      return n;
    }
    if (TypeEquals(b, n->type, t)) {
      // in both modes: if n is already of target type, stop here.
      dlog_mod("no-op -- n is already of target type %s", fmtnode(n->type));
      return n;
    }
  }

  switch (n->kind) {

  case NIntLit:
    n = NodeCopy(b->mem, n); // copy, since literals may be referenced by many
    if (R_UNLIKELY(t->kind != NBasicType))
      return report_non_convertible(b, n, t);
    if (convval(b, n, &n->val, t->t.basic.typeCode)) {
      n->type = t;
      return n;
    }
    break;

  case NBinOp:
    if (R_UNLIKELY(t->kind == NBasicType)) {
      return report_non_convertible(b, n, t);
    } else {
      //
      // TODO: IROpFromAST
      // // check to see if there is an operation on t; if the type cast is valid
      // auto tc = t->t.basic.typeCode;
      // if (IROpFromAST(n->op.op, tc, tc) == OpNil) {
      //   err_invalid_binop(b, n);
      //   break;
      // }
      assert(TypeEquals(b, n->op.left->type, n->op.right->type));
      Node* left  = convlit(b, n->op.left, t, fl);
      Node* right = convlit(b, n->op.right, t, fl);
      if (!TypeEquals(b, left->type, right->type)) {
        // literal conversion failed
        return n;
      }
      n->op.left = left;
      n->op.right = right;
      n->type = n->op.left->type;
    }
    break;

  default:
    n = NodeUnbox(n, /*unrefVars=*/true);
  }

  if (n->type == Type_ideal) {
    dlog_mod("assign type %s to ideal %s %s", fmtnode(t), NodeKindName(n->kind), fmtnode(n));
    n->type = t;
  }

  switch (n->kind) {
  case NIntLit:
    if (t == Type_f32 || t == Type_f64) {
      // IntLit -> FloatLit, e.g. "x = 123 as f64"
      n->val.f = (double)n->val.i;
      n->kind = NFloatLit;
    }
    break;
  case NFloatLit:
    if (t != Type_f32 && t != Type_f64) {
      // FloatLit -> IntLit, e.g. "x = 1.0 as u32"
      n->val.i = (u64)n->val.f;
      n->kind = NIntLit;
    }
    break;
  default:
    break;
  }

  return n;
}


// // binOpOkforType returns true if binary op Tok is available for operands of type TypeCode.
// // Tok => TypeCode => bool
// static bool binOpOkforType[T_PRIM_OPS_END - T_PRIM_OPS_START - 1][TypeCode_NUM_END] = {
//   //                 bool    int8  uint8  int16 uint16 int32 uint32 int64 uint64 f32 f64
//   /* TPlus       */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TMinus      */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TStar       */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TSlash      */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TGt         */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TLt         */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TEqEq       */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TNEq        */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TLEq        */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TGEq        */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TPlusPlus   */{ 0 },
//   /* TMinusMinus */{ 0 },
//   /* TTilde      */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
//   /* TNot        */{ false,  true, true,  true, true,  true, true,  true, true,  true,   true },
// };

// okfor[OADD] = okforadd[:]
// okfor[OAND] = okforand[:]
// okfor[OANDAND] = okforbool[:]
// okfor[OANDNOT] = okforand[:]
// okfor[ODIV] = okforarith[:]
// okfor[OEQ] = okforeq[:]
// okfor[OGE] = okforcmp[:]
// okfor[OGT] = okforcmp[:]
// okfor[OLE] = okforcmp[:]
// okfor[OLT] = okforcmp[:]
// okfor[OMOD] = okforand[:]
// okfor[OMUL] = okforarith[:]
// okfor[ONE] = okforeq[:]
// okfor[OOR] = okforand[:]
// okfor[OOROR] = okforbool[:]
// okfor[OSUB] = okforarith[:]
// okfor[OXOR] = okforand[:]
// okfor[OLSH] = okforand[:]
// okfor[ORSH] = okforand[:]

// func operandType(op Op, t *types.Type) *types.Type {
//   if okfor[op][t.Etype] {
//     return t
//   }
//   return nil
// }

