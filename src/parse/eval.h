// eval -- AST evaluation
#pragma once
#include "ast.h"
ASSUME_NONNULL_BEGIN

typedef enum {
  NodeEvalDefault     = 0,
  NodeEvalMustSucceed = 1 << 0, // if evaluation fails, build_errf is used to report error
} NodeEvalFlags;

// NodeEval attempts to evaluate expr.
// Returns NULL on failure, or the resulting value on success.
// If targetType is provided, the result is implicitly converted to that type.
// In that case it is an error if the result can't be converted to targetType.
Node* nullable NodeEval(BuildCtx*, Node* expr, Type* nullable targetType, NodeEvalFlags fl);

// NodeEvalUint calls NodeEval with Type_uint.
// result u64 in returnvalue->ival
inline static IntLitNode* nullable NodeEvalUint(BuildCtx* bctx, Node* expr) {
  Node* n = NodeEval(bctx, expr, kType_uint, NodeEvalMustSucceed);
  return n ? as_IntLitNode(n) : NULL;
}

ASSUME_NONNULL_END
