// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

typedef struct C {
  BuildCtx*        build;
  CTypecastFlags   flags;
  CTypecastResult* resultp;
} C;

typedef struct IntvalRange { i64 min; u64 max; } IntvalRange;

static const IntvalRange intval_range_tab[TC_NUM_END] = {
  [TC_bool] = { 0,            1,       },
  [TC_i8]   = { I8_MIN,       I8_MAX,  },
  [TC_u8]   = { 0,            U8_MAX,  },
  [TC_i16]  = { I16_MIN,      I16_MAX, },
  [TC_u16]  = { 0,            U16_MAX, },
  [TC_i32]  = { I32_MIN,      I32_MAX, },
  [TC_u32]  = { 0,            U32_MAX, },
  [TC_i64]  = { I64_MIN,      I64_MAX, },
  [TC_u64]  = { 0,            U64_MAX, },
  [TC_f32]  = { 0,            0,       }, // TODO
  [TC_f64]  = { 0,            0,       }, // TODO
  [TC_int]  = { I32_MIN,      I32_MAX, },
  [TC_uint] = { 0,            U32_MAX, },
};


#define FMTNODE(n,bufno) \
  fmtnode(n, c->build->tmpbuf[bufno], sizeof(c->build->tmpbuf[bufno]))

#define RETURN_RES(n, result) return (*c->resultp = (result), as_Expr(n))


static void report_result(C* c, Expr* n, Type* totype, Node* usernode) {
  char buf2[128];
  switch ((enum CTypecastResult)*c->resultp) {
    case CTypecastUnchanged: // no conversion needed
    case CTypecastConverted: // successfully converted to type
      break;
    case CTypecastErrCompat:
      b_errf(c->build, NodePosSpan(usernode),
        "%s (%s) is incompatible with type %s",
        FMTNODE(n,0), FMTNODE(n->type,1), fmtnode(totype, buf2, sizeof(buf2)) );
      break;
    case CTypecastErrRangeOver:
      b_errf(c->build, NodePosSpan(usernode),
        "constant %s is too large for type %s",
        FMTNODE(n,0), FMTNODE(totype,1));
      break;
    case CTypecastErrRangeUnder:
      b_errf(c->build, NodePosSpan(usernode),
        "constant %s is too small for type %s",
        FMTNODE(n,0), FMTNODE(totype,1));
      break;
    case CTypecastErrNoMem:
      b_errf(c->build, NodePosSpan(usernode),
        "failed to allocate internal memory");
      break;
  }
}


static Expr* cast_from_intlit(C* c, IntLitNode* n, Type* totype1) {
  if UNLIKELY(!is_BasicTypeNode(totype1))
    RETURN_RES(n, CTypecastErrCompat);
  BasicTypeNode* totype = (BasicTypeNode*)totype1;

  TypeCode dst_tc = totype->typecode;
  switch (dst_tc) {
    case TC_int:  dst_tc = c->build->sint_type->typecode; break;
    case TC_uint: dst_tc = c->build->uint_type->typecode; break;
  }
  assertf(dst_tc < TC_NUM_END,
    "invalid totype: %d '%c'", dst_tc, TypeCodeEncoding(dst_tc));
  IntvalRange range = intval_range_tab[dst_tc];

  BasicTypeNode* fromtype = totype;
  if (n->type && n->type != kType_ideal)
    fromtype = as_BasicTypeNode(n->type);

  // check range
  if (fromtype->tflags & TF_Signed) {
    // e.g. i32 => u64  or
    // e.g. i32 => i64
    i64 val = (i64)n->ival;
    if UNLIKELY(val < range.min || (val > 0 && n->ival > range.max))
      RETURN_RES(n, val < 0 ? CTypecastErrRangeUnder : CTypecastErrRangeOver);
  } else if (totype->tflags & TF_Signed) {
    // e.g. u32 => i64  or
    // e.g. u32 => u64
    if UNLIKELY(n->ival > range.max)
      RETURN_RES(n, CTypecastErrRangeOver);
  }

  *c->resultp = CTypecastConverted;
  n->type = as_Type(totype);
  return as_Expr(n);
}


static Expr* cast_to_refslice(C* c, Expr* srcn, RefTypeNode* dstt) {
  // totype is a ref slice: &[T]
  //
  //       [T N] │ T mem[N]
  //   mut&[T N] │ T*
  //      &[T N] │ const T*
  //       [T]   │ struct mslice { T* p; uint len; uint cap; }
  //   mut&[T]   │ struct mslice { T* p; uint len; uint cap; }
  //      &[T]   │ struct cslice { const T* p; uint len; }
  //
  Type* srct = unbox_id_type(srcn->type);
  ArrayTypeNode* dstarrayt = as_ArrayTypeNode(dstt->elem);
  ArrayTypeNode* srcarrayt = (ArrayTypeNode*)srct;

  if (srct->kind == NRefType && is_ArrayTypeNode(((RefTypeNode*)srct)->elem)) {
    srcarrayt = (ArrayTypeNode*)((RefTypeNode*)srct)->elem;
  } else if (srct->kind != NArrayType) {
    // incompatible: source is not a slice nor an array
    return srcn;
  }

  if (!b_typeeq(c->build, srcarrayt->elem, dstarrayt->elem)) {
    // incompatible: arrays have incompatible element types, eg "[i8] <> [u32]"
    return srcn;
  }

  if (!NodeIsConst(dstt) && NodeIsConst(srct)) {
    // incompatible: cannot convert immutable ref to mutable
    return srcn;
  }

  TypeCastNode* tc = b_mknode(c->build, TypeCast, srcn->pos);
  tc->expr = srcn;
  tc->type = as_Type(dstt);
  *c->resultp = CTypecastConverted;
  return as_Expr(tc);
}


Expr* _ctypecast(
  BuildCtx* b, Expr* n, Type* totype,
  CTypecastResult* nullable resp, Node* nullable report_usernode, CTypecastFlags flags)
{
  totype = unbox_id_type(totype);

  // if type of n is already totype, stop now
  if (n->type && b_typeeq(b, n->type, totype)) {
    if (resp)
      *resp = CTypecastUnchanged;
    return n;
  }

  CTypecastResult restmp;
  C ctx = {
    .build = b,
    .flags = flags,
    .resultp = resp ? resp : &restmp,
  };
  C* c = &ctx;
  *c->resultp = CTypecastErrCompat; // default result

  // #if DEBUG
  //   char totype_str[256];
  //   fmtnode(totype, totype_str, sizeof(totype_str));
  //   dlog("ctypecast [%s] %s %s of type %s as %s",
  //     (flags & CTypecastFExplicit) ? "explicit" : "implicit",
  //     nodename(n), FMTNODE(n,0), FMTNODE(n->type,1), totype_str);
  // #endif

  if (n->kind == NIntLit) {
    n = cast_from_intlit(c, (IntLitNode*)n, totype);
  } else if (n->type) {
    RefTypeNode* dstreft = (RefTypeNode*)totype;
    if (dstreft->kind == NRefType && is_ArrayTypeNode(dstreft->elem)) {
      // totype is a ref slice: &[T]
      n = cast_to_refslice(c, n, dstreft);
    } else {
      Type* srct = unbox_id_type(n->type);
      dlog("TODO ctypecast: (%s -> %s) %s -> %s",
        nodename(srct), nodename(totype), FMTNODE(srct,0), FMTNODE(totype,1));
    }
  }

  report_result(c, n, totype, report_usernode ? report_usernode : as_Node(n));
  return n;
}
