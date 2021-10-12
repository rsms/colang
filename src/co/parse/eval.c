#include "../common.h"
#include "../util/array.h"
#include "parse.h"

static Node* nullable _eval(
  Build* b, NodeEvalFlags fl, Type* nullable targetType, Node* nullable n);


Node* nullable NodeEval(Build* b, Node* n, Type* nullable targetType, NodeEvalFlags fl) {
  assertnotnull(n);
  return _eval(b, fl, targetType, n);
}


static void report_invalid_op(Build* b, Node* n, Type* t) {
  build_errf(b, NodePosSpan(n),
    "unsupported compile-time operation %s on type %s",
    fmtnode(n), fmtnode(t));
}

#define INTEGER_TYPES(_) \
  _(i8)  \
  _(u8)  \
  _(i16) \
  _(u16) \
  _(i32) \
  _(u32) \
  _(i64) \
  _(u64) \
  /* end INTEGER_TYPES */

#define FLOAT_TYPES(_) \
  _(f32) \
  _(f64) \
  /* end FLOAT_TYPES */


#define EACH(T)                                              \
  static bool _eval_binop_##T(Build* b, Tok op, T* x, T y) { \
    switch (op) {                                            \
      case TStar:    *x = *x * y; return true;               \
      case TSlash:   *x = *x / y; return true;               \
      case TPercent: *x = *x % y; return true;               \
      case TShl:     *x = *x << y; return true;              \
      case TShr:     *x = *x >> y; return true;              \
      case TPlus:    *x = *x + y; return true;               \
      case TMinus:   *x = *x - y; return true;               \
      case THat:     *x = *x ^ y; return true;               \
      case TAnd:     *x = *x & y; return true;               \
      case TPipe:    *x = *x | y; return true;               \
      default: return false;                                 \
    }                                                        \
  }                                                          \
  static bool _eval_prefixop_##T(Build* b, Tok op, T* x) {   \
    switch (op) {                                            \
      case TPlus:  *x = +(*x); return true;                  \
      case TMinus: *x = -(*x); return true;                  \
      default: return false;                                 \
    }                                                        \
  }
INTEGER_TYPES(EACH)
#undef EACH


#define EACH(T)                                              \
  static bool _eval_binop_##T(Build* b, Tok op, T* x, T y) { \
    switch (op) {                                            \
      case TStar:    *x = *x * y; return true;               \
      case TSlash:   *x = *x / y; return true;               \
      case TPlus:    *x = *x + y; return true;               \
      case TMinus:   *x = *x - y; return true;               \
      default: return false;                                 \
    }                                                        \
  }                                                          \
  static bool _eval_prefixop_##T(Build* b, Tok op, T* x) {   \
    switch (op) {                                            \
      case TPlus:  *x = +(*x); return true;                  \
      case TMinus: *x = -(*x); return true;                  \
      default: return false;                                 \
    }                                                        \
  }
FLOAT_TYPES(EACH)
#undef EACH


static Node* make_intlit(Build* b, u64 value, Node* origin, Type* t) {
  Node* n = NewNode(b->mem, NIntLit);
  n->type = t;
  n->val.ct = CType_int;
  n->val.i = value;
  n->pos = origin->pos;
  n->endpos = origin->endpos;
  return n;
}


static Node* make_floatlit(Build* b, double value, Node* origin, Type* t) {
  Node* n = NewNode(b->mem, NFloatLit);
  n->type = t;
  n->val.ct = CType_float;
  n->val.f = value;
  n->pos = origin->pos;
  n->endpos = origin->endpos;
  return n;
}


static Node* nullable _eval_binop_int(
  Build* b, NodeEvalFlags fl, Node* n, Node* left, Node* right)
{
  Type* t = left->type; // intentionally ignore n->type
  asserteq_debug(t->kind, NBasicType); // sanity
  u64 x = left->val.i;
  bool ok = false;
  TypeCode tc = t->t.basic.typeCode;
tc_switch:
  switch (tc) {
    case TypeCode_int:  tc = b->sint_type; goto tc_switch;
    case TypeCode_uint: tc = b->uint_type; goto tc_switch;
    #define EACH(T)                                                 \
      case TypeCode_##T:                                            \
        ok = _eval_binop_##T(b, n->op.op, (T*)&x, (T)right->val.i); \
        break;
    INTEGER_TYPES(EACH)
    #undef EACH
    default: break;
  }
  if (ok)
    return make_intlit(b, x, n, t);
  if (R_UNLIKELY(fl & NodeEvalMustSucceed))
    report_invalid_op(b, n, t);
  return NULL;
}


static Node* nullable _eval_prefixop_int(Build* b, NodeEvalFlags fl, Node* n, Node* val) {
  Type* t = val->type;
  asserteq_debug(t->kind, NBasicType); // sanity
  u64 x = val->val.i;
  bool ok = false;
  TypeCode tc = t->t.basic.typeCode;
tc_switch:
  switch (tc) {
    case TypeCode_int:  tc = b->sint_type; goto tc_switch;
    case TypeCode_uint: tc = b->uint_type; goto tc_switch;
    #define EACH(T)                                   \
      case TypeCode_##T:                              \
        ok = _eval_prefixop_##T(b, n->op.op, (T*)&x); \
        break;
    INTEGER_TYPES(EACH)
    #undef EACH
    default:
      break;
  }
  if (ok)
    return make_intlit(b, x, n, t);
  if (R_UNLIKELY(fl & NodeEvalMustSucceed))
    report_invalid_op(b, n, t);
  return NULL;
}


static Node* nullable _eval_binop_float(
  Build* b, NodeEvalFlags fl, Node* n, Node* left, Node* right)
{
  Type* t = left->type; // intentionally ignore n->type
  asserteq_debug(t->kind, NBasicType); // sanity
  double x = left->val.f;
  bool ok = false;
  switch (t->t.basic.typeCode) {
    case TypeCode_f32: {
      float x2 = (float)x;
      ok = _eval_binop_f32(b, n->op.op, &x2, (f32)right->val.f);
      x = (double)x2;
      break;
    }
    case TypeCode_f64:
      ok = _eval_binop_f64(b, n->op.op, &x, right->val.f);
      break;
    default:
      break;
  }
  if (ok)
    return make_floatlit(b, x, n, t);
  if (R_UNLIKELY(fl & NodeEvalMustSucceed))
    report_invalid_op(b, n, t);
  return NULL;
}


static Node* nullable _eval_prefixop_float(Build* b, NodeEvalFlags fl, Node* n, Node* val) {
  Type* t = val->type;
  asserteq_debug(t->kind, NBasicType); // sanity
  double x = val->val.f;
  bool ok = false;
  switch (t->t.basic.typeCode) {
    case TypeCode_f32: {
      float x2 = (float)x;
      ok = _eval_prefixop_f32(b, n->op.op, &x2);
      x = (double)x2;
      break;
    }
    case TypeCode_f64:
      ok = _eval_prefixop_f64(b, n->op.op, &x);
      break;
    default:
      break;
  }
  if (ok)
    return make_floatlit(b, x, n, t);
  if (R_UNLIKELY(fl & NodeEvalMustSucceed))
    report_invalid_op(b, n, t);
  return NULL;
}


static Node* nullable _eval_binop(
  Build* b, NodeEvalFlags fl, Node* n, Node* left, Node* right)
{
  assertnotnull_debug(left->type);
  assertnotnull_debug(right->type);
  if (R_UNLIKELY( left->kind != right->kind || !TypeEquals(b, left->type, right->type) )) {
    // Note: This error is caught by resolve_type()
    if (fl & NodeEvalMustSucceed)
      build_errf(b, NodePosSpan(n), "mixed types in operation %s", fmtnode(n));
    return NULL;
  }
  switch (left->kind) {
    case NIntLit:
      R_MUSTTAIL return _eval_binop_int(b, fl, n, left, right);
    case NFloatLit:
      R_MUSTTAIL return _eval_binop_float(b, fl, n, left, right);
    default:
      break;
  }
  if (R_UNLIKELY(fl & NodeEvalMustSucceed))
    report_invalid_op(b, n, left->type);
  return NULL;
}


static Node* nullable _eval_prefixop(Build* b, NodeEvalFlags fl, Node* n, Node* val) {
  switch (val->kind) {
    case NIntLit:
      R_MUSTTAIL return _eval_prefixop_int(b, fl, n, val);
    case NFloatLit:
      R_MUSTTAIL return _eval_prefixop_float(b, fl, n, val);
    default:
      break;
  }
  if (fl & NodeEvalMustSucceed)
    report_invalid_op(b, n, assertnotnull_debug(val->type));
  return NULL;
}


static Node* nullable _eval(
  Build* b, NodeEvalFlags fl, Type* nullable targetType, Node* nullable n)
{
  if (n == NULL)
    return NULL;

  Node* n_orig = n;

  //dlog("eval %s %s", NodeKindName(n->kind), fmtast(n));

  switch (n->kind) {

    case NId:
      return _eval(b, fl, targetType, n->id.target);

    case NVar:
      return _eval(b, fl, targetType, n->var.init);

    case NBoolLit:
    case NIntLit:
    case NFloatLit:
    case NStrLit:
      break;

    case NBinOp: {
      Node* left = _eval(b, fl, targetType, n->op.left);
      if (!left)
        return NULL;
      Node* right = _eval(b, fl, targetType, n->op.right);
      if (!right)
        return NULL;
      n = _eval_binop(b, fl, n, left, right);
      break;
    }

    case NPrefixOp: {
      Node* operand = _eval(b, fl, targetType, n->op.left);
      if (!operand)
        return NULL;
      n = _eval_prefixop(b, fl, n, operand);
      break;
    }

    default:
      if (fl & NodeEvalMustSucceed)
        build_errf(b, NodePosSpan(n), "%s is not a compile-time expression", fmtnode(n));
      return NULL;

  } // switch (n->kind)

  if (n) {
    n->pos = n_orig->pos;
    n->endpos = n_orig->endpos;
    if (targetType)
      return convlit(b, n, targetType, ConvlitImplicit);
  }

  return n;
}
