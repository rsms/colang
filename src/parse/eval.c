// AST expression evaluation
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PARSE_EVAL_IMPLEMENTATION
#endif
#include "ast.c"
#include "buildctx.c"
#include "universe.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

typedef enum {
  NodeEvalDefault     = 0,
  NodeEvalMustSucceed = 1 << 0, // if evaluation fails, build_errf is used to report error
} NodeEvalFlags;

// NodeEval attempts to evaluate expr.
// Returns NULL on failure, or the resulting value on success.
// If targettype is provided, the result is implicitly converted to that type.
// In that case it is an error if the result can't be converted to targettype.
Expr* nullable _NodeEval(BuildCtx*, Expr* expr, Type* nullable targettype, NodeEvalFlags fl);
#define NodeEval(b, expr, tt, fl) _NodeEval((b),as_Expr(expr),((tt)?as_Type(tt):NULL),(fl))

// NodeEvalUint calls NodeEval with Type_uint.
// result u64 in returnvalue->ival
static IntLitNode* nullable NodeEvalUint(BuildCtx* bctx, Expr* expr);


//———————————————————————————————————————————————————————————————————————————————————————
// internal

inline static IntLitNode* nullable NodeEvalUint(BuildCtx* bctx, Expr* expr) {
  Expr* n = NodeEval(bctx, expr, kType_uint, NodeEvalMustSucceed);
  return n ? as_IntLitNode(n) : NULL;
}

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PARSE_EVAL_IMPLEMENTATION

#include "ctypecast.c"


typedef struct E {
  BuildCtx*     b;
  NodeEvalFlags fl;
} E;


static Expr* nullable _eval(E, Type* nullable targetType, Expr* nullable n);


Expr* nullable _NodeEval(BuildCtx* b, Expr* n, Type* nullable targetType, NodeEvalFlags fl) {
  assertnotnull(n);
  return _eval((E){b, fl}, targetType, n);
}


#define report_invalid_op(e, expr, typ) _report_invalid_op((e).b,as_Expr(expr),as_Type(typ))

static void _report_invalid_op(BuildCtx* b, Expr* n, Type* t) {
  b_errf(b, NodePosSpan(n),
    "unsupported compile-time operation %s on type %s",
    fmtnode(n, b->tmpbuf[0], sizeof(b->tmpbuf[0])),
    fmtnode(t, b->tmpbuf[1], sizeof(b->tmpbuf[1])));
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

// integer types
#define EACH(T)                                    \
  static bool _eval_binop_##T(Tok op, T* x, T y) { \
    switch (op) {                                  \
      case TStar:    *x = *x * y; return true;     \
      case TSlash:   *x = *x / y; return true;     \
      case TPercent: *x = *x % y; return true;     \
      case TShl:     *x = *x << y; return true;    \
      case TShr:     *x = *x >> y; return true;    \
      case TPlus:    *x = *x + y; return true;     \
      case TMinus:   *x = *x - y; return true;     \
      case THat:     *x = *x ^ y; return true;     \
      case TAnd:     *x = *x & y; return true;     \
      case TPipe:    *x = *x | y; return true;     \
      default: return false;                       \
    }                                              \
  }                                                \
  static bool _eval_prefixop_##T(Tok op, T* x) {   \
    switch (op) {                                  \
      case TPlus:  *x = +(*x); return true;        \
      case TMinus: *x = -(*x); return true;        \
      default: return false;                       \
    }                                              \
  }
INTEGER_TYPES(EACH)
#undef EACH
// float types
#define EACH(T)                                    \
  static bool _eval_binop_##T(Tok op, T* x, T y) { \
    switch (op) {                                  \
      case TStar:    *x = *x * y; return true;     \
      case TSlash:   *x = *x / y; return true;     \
      case TPlus:    *x = *x + y; return true;     \
      case TMinus:   *x = *x - y; return true;     \
      default: return false;                       \
    }                                              \
  }                                                \
  static bool _eval_prefixop_##T(Tok op, T* x) {   \
    switch (op) {                                  \
      case TPlus:  *x = +(*x); return true;        \
      case TMinus: *x = -(*x); return true;        \
      default: return false;                       \
    }                                              \
  }
FLOAT_TYPES(EACH)
#undef EACH


static Expr* nullable make_intlit(E e, u64 value, Node* origin, BasicTypeNode* t) {
  auto n = b_mknode(e.b, IntLit);
  if (UNLIKELY(!n))
    return NULL;
  n->type = as_Type(t);
  n->ival = value;
  n->pos = origin->pos;
  n->endpos = origin->endpos;
  return as_Expr(n);
}


static Expr* nullable make_floatlit(E e, double value, Node* origin, BasicTypeNode* t) {
  auto n = b_mknode(e.b, FloatLit);
  if (UNLIKELY(!n))
    return NULL;
  n->type = as_Type(t);
  n->fval = value;
  n->pos = origin->pos;
  n->endpos = origin->endpos;
  return as_Expr(n);
}


static Expr* nullable _eval_binop_int(
  E e, BinOpNode* op, IntLitNode* left, IntLitNode* right)
{
  auto t = as_BasicTypeNode(left->type); // intentionally ignore op->type
  u64 x = left->ival;
  bool ok = false;
  TypeCode tc = t->typecode;
tc_switch:
  switch (tc) {
    case TC_int:  tc = e.b->sint_type; goto tc_switch;
    case TC_uint: tc = e.b->uint_type; goto tc_switch;
    #define EACH(T) \
      case TC_##T: ok = _eval_binop_##T(op->op, (T*)&x, (T)right->ival); break;
    INTEGER_TYPES(EACH)
    #undef EACH
    default: break;
  }
  if (ok)
    return make_intlit(e, x, as_Node(op), t);
  if (UNLIKELY(e.fl & NodeEvalMustSucceed))
    report_invalid_op(e, op, t);
  return NULL;
}


static Expr* nullable _eval_prefixop_int(E e, PrefixOpNode* op, IntLitNode* val) {
  auto t = as_BasicTypeNode(val->type);
  u64 x = val->ival;
  bool ok = false;
  TypeCode tc = t->typecode;
tc_switch:
  switch (tc) {
    case TC_int:  tc = e.b->sint_type; goto tc_switch;
    case TC_uint: tc = e.b->uint_type; goto tc_switch;
    #define EACH(T) \
      case TC_##T: ok = _eval_prefixop_##T(op->op, (T*)&x); break;
    INTEGER_TYPES(EACH)
    #undef EACH
    default:
      break;
  }
  if (ok)
    return as_Expr(make_intlit(e, x, as_Node(op), t));
  if (UNLIKELY(e.fl & NodeEvalMustSucceed))
    report_invalid_op(e, op, t);
  return NULL;
}


static Expr* nullable _eval_binop_float(
  E e, BinOpNode* op, FloatLitNode* left, FloatLitNode* right)
{
  auto t = as_BasicTypeNode(left->type);
  double x = left->fval;
  bool ok = false;
  switch (t->typecode) {
    case TC_f32: {
      float x2 = (float)x;
      ok = _eval_binop_f32(op->op, &x2, (f32)right->fval);
      x = (double)x2;
      break;
    }
    case TC_f64:
      ok = _eval_binop_f64(op->op, &x, right->fval);
      break;
    default:
      break;
  }
  if (ok)
    return make_floatlit(e, x, as_Node(op), t);
  if (UNLIKELY(e.fl & NodeEvalMustSucceed))
    report_invalid_op(e, op, t);
  return NULL;
}


static Expr* nullable _eval_prefixop_float(E e, PrefixOpNode* op, FloatLitNode* val) {
  auto t = as_BasicTypeNode(val->type);
  double x = val->fval;
  bool ok = false;
  switch (t->typecode) {
    case TC_f32: {
      float x2 = (float)x;
      ok = _eval_prefixop_f32(op->op, &x2);
      x = (double)x2;
      break;
    }
    case TC_f64:
      ok = _eval_prefixop_f64(op->op, &x);
      break;
    default:
      break;
  }
  if (ok)
    return as_Expr(make_floatlit(e, x, as_Node(op), t));
  if (UNLIKELY(e.fl & NodeEvalMustSucceed))
    report_invalid_op(e, op, t);
  return NULL;
}


static Expr* nullable _eval_binop(
  E e, BinOpNode* op, Expr* left, Expr* right)
{
  assertnotnull(left->type);
  assertnotnull(right->type);
  if (UNLIKELY( left->kind != right->kind || !b_typeeq(e.b, left->type, right->type) )) {
    // Note: This error is caught by resolve_type()
    if (e.fl & NodeEvalMustSucceed) {
      b_errf(e.b, NodePosSpan(op), "mixed types in operation %s",
        fmtnode(op, e.b->tmpbuf[0], sizeof(e.b->tmpbuf[0])));
    }
    return NULL;
  }
  switch (left->kind) {
    case NIntLit:
      return _eval_binop_int(e, op, as_IntLitNode(left), as_IntLitNode(right));
    case NFloatLit:
      return _eval_binop_float(e, op, as_FloatLitNode(left), as_FloatLitNode(right));
    default:
      break;
  }
  if (UNLIKELY(e.fl & NodeEvalMustSucceed))
    report_invalid_op(e, op, left->type);
  return NULL;
}


static Expr* nullable _eval_prefixop(E e, PrefixOpNode* op, Expr* val) {
  switch (val->kind) {
    case NIntLit:
      return _eval_prefixop_int(e, op, as_IntLitNode(val));
    case NFloatLit:
      return _eval_prefixop_float(e, op, as_FloatLitNode(val));
    default:
      break;
  }
  if (e.fl & NodeEvalMustSucceed)
    report_invalid_op(e, op, assertnotnull(val->type));
  return NULL;
}


static Expr* nullable _eval(E e, Type* nullable targetType, Expr* nullable n) {
  if (n == NULL)
    return NULL;

  Expr* n_orig = n;

  //dlog("eval %s %s", NodeKindName(n->kind), fmtast(n));

  switch (n->kind) {

    case NId:
      return _eval(e, targetType, as_Expr(((IdNode*)(n))->target));

    case NLocal_BEG ... NLocal_END:
      return _eval(e, targetType, LocalInitField((LocalNode*)n));

    case NBoolLit:
    case NIntLit:
    case NFloatLit:
    case NStrLit:
      break;

    case NBinOp: {
      auto op = (BinOpNode*)n;
      Expr* left = _eval(e, targetType, op->left);
      if (!left)
        return NULL;
      Expr* right = _eval(e, targetType, op->right);
      if (!right)
        return NULL;
      n = _eval_binop(e, op, left, right);
      break;
    }

    case NPrefixOp: {
      auto op = (PrefixOpNode*)n;
      Expr* operand = _eval(e, targetType, op->expr);
      if (!operand)
        return NULL;
      n = _eval_prefixop(e, op, operand);
      break;
    }

    default:
      if (e.fl & NodeEvalMustSucceed) {
        b_errf(e.b, NodePosSpan(n), "%s is not a compile-time expression",
          fmtnode(n, e.b->tmpbuf[0], sizeof(e.b->tmpbuf[0])));
      }
      return NULL;

  } // switch (n->kind)

  if (UNLIKELY( !n ))
    return NULL;

  n->pos = n_orig->pos;
  n->endpos = n_orig->endpos;

  if (targetType)
    return ctypecast_implicit(e.b, n, targetType, NULL);

  return n;
}

#endif // PARSE_EVAL_IMPLEMENTATION
