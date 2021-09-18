#include "../common.h"
#include "../util/array.h"
#include "parse.h"


static bool _eval_binop_uint(Build* b, Node* n, Tok op, u64 x, u64 y, u64* res) {
  switch (op) {
    case TStar:    *res = x * y; return true;
    case TSlash:   *res = x / y; return true;
    case TPercent: *res = x % y; return true;
    case TShl:     *res = x << y; return true;
    case TShr:     *res = x >> y; return true;
    case TPlus:    *res = x + y; return true;
    case TMinus:   *res = x - y; return true;
    case THat:     *res = x ^ y; return true;
    case TAnd:     *res = x & y; return true;
    case TPipe:    *res = x | y; return true;
    default: return false;
  }
}

static bool _eval_binop_sint(Build* b, Node* n, Tok op, i64 x, i64 y, i64* res) {
  switch (op) {
    case TStar:    *res = x * y; return true;
    case TSlash:   *res = x / y; return true;
    case TPercent: *res = x % y; return true;
    case TShl:     *res = x << y; return true;
    case TShr:     *res = x >> y; return true;
    case TPlus:    *res = x + y; return true;
    case TMinus:   *res = x - y; return true;
    case THat:     *res = x ^ y; return true;
    case TAnd:     *res = x & y; return true;
    case TPipe:    *res = x | y; return true;
    default: return false;
  }
}

static bool _eval_binop_float32(Build* b, Node* n, Tok op, float x, float y, double* res) {
  switch (op) {
    case TStar:    *res = x * y; return true;
    case TSlash:   *res = x / y; return true;
    case TPlus:    *res = x + y; return true;
    case TMinus:   *res = x - y; return true;
    default: return false;
  }
}

static bool _eval_binop_float64(Build* b, Node* n, Tok op, double x, double y, double* res) {
  switch (op) {
    case TStar:    *res = x * y; return true;
    case TSlash:   *res = x / y; return true;
    case TPlus:    *res = x + y; return true;
    case TMinus:   *res = x - y; return true;
    default: return false;
  }
}

static Node* nullable _eval_binop(Build* b, Node* n, Node* left, Node* right) {
  assertnotnull_debug(left->type);
  assertnotnull_debug(right->type);
  if (left->kind != right->kind || !TypeEquals(b, left->type, right->type)) {
    // Note: This error is caught by resolve_type()
    build_errf(b, NodePosSpan(n), "mixed types in operation %s", fmtnode(n));
    return NULL;
  }

  Type* t = left->type; // intentionally ignore n->type

  switch (left->kind) {
    case NIntLit: {
      asserteq_debug(t->kind, NBasicType); // sanity
      u64 L = (i64)left->val.i;
      u64 R = (i64)right->val.i;
      u64 res;
      if (TypeCodeIsSigned(t->t.basic.typeCode)) {
        if (!_eval_binop_sint(b, n, n->op.op, (i64)L, (i64)R, (i64*)&res))
          break;
      } else if (!_eval_binop_uint(b, n, n->op.op, L, R, &res)) {
        break;
      }
      Node* resn = NewNode(b->mem, NIntLit);
      resn->type = t;
      resn->val.ct = CType_int;
      resn->val.i = res;
      return resn;
    }
    case NFloatLit: {
      double L = (i64)left->val.f;
      double R = (i64)right->val.f;
      double res;
      if (t->t.basic.typeCode == TypeCode_f32) {
        if (!_eval_binop_float32(b, n, n->op.op, (float)L, (float)R, &res))
          break;
      } else if (!_eval_binop_float64(b, n, n->op.op, L, R, &res)) {
        break;
      }
      Node* resn = NewNode(b->mem, NFloatLit);
      resn->type = t;
      resn->val.ct = CType_float;
      resn->val.f = res;
      return resn;
    }
    default:
      break;
  }

  build_errf(b, NodePosSpan(n), "unsupported compile-time operation %s on type %s",
    fmtnode(n), fmtnode(t));
  return NULL;
}


static Node* nullable _eval(Build* b, Type* nullable targetType, Node* nullable n) {
  if (n == NULL)
    return NULL;

  //dlog("eval %s %s", NodeKindName(n->kind), fmtast(n));

  switch (n->kind) {

    case NId:
      return _eval(b, targetType, n->ref.target);

    case NLet:
      return _eval(b, targetType, n->var.init);

    case NBoolLit:
    case NIntLit:
    case NFloatLit:
    case NStrLit:
      if (targetType)
        return convlit(b, n, targetType, ConvlitImplicit);
      return n;

    case NBinOp: {
      auto left = _eval(b, targetType, n->op.left);
      if (!left)
        return NULL;
      auto right = _eval(b, targetType, n->op.right);
      if (!right)
        return NULL;
      n = _eval_binop(b, n, left, right);
      if (n && targetType)
        n = convlit(b, n, targetType, ConvlitImplicit);
      return n;
    }

    default:
      build_errf(b, NodePosSpan(n), "%s is not a compile-time expression", fmtnode(n));
      return NULL;

  } // switch (n->kind)
  UNREACHABLE;
}


Node* nullable NodeEval(Build* b, Node* n, Type* nullable targetType) {
  assertnotnull(n);
  return _eval(b, targetType, n);
}
