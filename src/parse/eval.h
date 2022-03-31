// AST expression evaluation
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

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


END_INTERFACE
