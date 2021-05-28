#include "../common.h"
#include "parse.h"

// #include "ir/op.h"

// DEBUG_MODULE: define to enable debug logging
//#define DEBUG_MODULE "convlit"

#ifdef DEBUG_MODULE
  #define dlog_mod(format, ...) dlog("[" DEBUG_MODULE "] " format, ##__VA_ARGS__)
#else
  #define dlog_mod(...) do{}while(0)
#endif


static void err_invalid_binop(Build* b, Node* n) {
  assert(n->kind == NBinOp);
  auto ltype = NodeEffectiveType(n->op.left);
  auto rtype = NodeEffectiveType(n->op.right);
  build_errf(b, NodePosSpan(n),
    "invalid operation: %s (mismatched types %s and %s)",
    TokName(n->op.op),
    fmtnode(ltype),
    fmtnode(rtype));
}


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
  (i64)0,                   // TODO float32
  (i64)0,                   // TODO float64
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
  0,                   // TODO float32
  0,                   // TODO float64
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
        auto nval = NValFmt(str_new(16), v);
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
      assert(0 && "unexpected CType");
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
static bool convval(Build* b, Node* srcnode, NVal* v, Node* targetType, bool explicit) {
  // TODO use 'explicit' to allow "greater" conversions like for example int -> str.
  if (targetType->kind != NBasicType) {
    dlog("convlit TODO targetType->kind %s", NodeKindName(targetType->kind));
    return false;
  }

  auto tc = targetType->t.basic.typeCode;
  auto tcfl = TypeCodeFlags(tc);

  // * -> integer
  if (tcfl & TypeCodeFlagInt) {
    return convval_to_int(b, srcnode, v, tc);
  }

  // * -> float
  if (tcfl & TypeCodeFlagFloat) {
    return convval_to_float(b, srcnode, v, tc);
  }

  dlog("convlit TODO * -> BasicType(%s)", TypeCodeName(tc));
  return false;
}


// convlit converts an expression n to type t.
// If n is already of type t, n is simply returned.
Node* convlit(Build* b, Node* n, Node* t, bool explicit) {
  assert(t != NULL);
  assert(t != Type_ideal);
  assert(NodeKindIsType(t->kind));

  dlog_mod("[%s] %s of type %s as %s",
    explicit ? "explicit" : "implicit",
    fmtnode(n), fmtnode(n->type), fmtnode(t));

  if (n->type != NULL && n->type != Type_nil && n->type != Type_ideal) {
    if (!explicit) {
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
    if (convval(b, n, &n->val, t, explicit)) {
      n->type = t;
      return n;
    }
    break;

  case NId:
    assert(n->ref.target != NULL);
    n->ref.target = convlit(b, (Node*)n->ref.target, t, /* explicit */ false);
    break;

  case NLet:
    assert(n->field.init != NULL);
    n->field.init = convlit(b, n->field.init, t, /* explicit */ false);
    break;

  case NBinOp:
    if (t->kind == NBasicType) {
      //
      // TODO: IROpFromAST
      // // check to see if there is an operation on t; if the type cast is valid
      // auto tc = t->t.basic.typeCode;
      // if (IROpFromAST(n->op.op, tc, tc) == OpNil) {
      //   err_invalid_binop(b, n);
      //   break;
      // }
      n->op.left  = convlit(b, n->op.left, t, /* explicit */ false);
      n->op.right = convlit(b, n->op.right, t, /* explicit */ false);
      if (R_UNLIKELY(!TypeEquals(b, n->op.left->type, n->op.right->type))) {
        err_invalid_binop(b, n);
        break;
      }
      n->type = n->op.left->type;
    } else {
      dlog("TODO NBinOp %s as %s", fmtnode(n), fmtnode(t));
    }
    break;

  default:
    dlog("TODO n->kind %s", NodeKindName(n->kind));
    break;
  }

  if (n->type == Type_ideal) {
    dlog_mod("assign type %s to ideal %s %s", fmtnode(t), NodeKindName(n->kind), fmtnode(n));
    n->type = t;
  }

  switch (n->kind) {
  case NIntLit:
    if (t == Type_float32 || t == Type_float64) {
      // IntLit -> FloatLit, e.g. "x = 123 as float64"
      n->val.f = (double)n->val.i;
      n->kind = NFloatLit;
    }
    break;
  case NFloatLit:
    if (t != Type_float32 && t != Type_float64) {
      // FloatLit -> IntLit, e.g. "x = 1.0 as u32"
      n->val.i = (u64)n->val.f;
      n->kind = NIntLit;
    }
    break;
  }

  return n;
}


// // binOpOkforType returns true if binary op Tok is available for operands of type TypeCode.
// // Tok => TypeCode => bool
// static bool binOpOkforType[T_PRIM_OPS_END - T_PRIM_OPS_START - 1][TypeCode_NUM_END] = {
//   //                 bool    int8  uint8  int16 uint16 int32 uint32 int64 uint64 float32 float64
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

