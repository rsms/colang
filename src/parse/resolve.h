// Pos -- compact representation of source positions
#pragma once
#include "ast.h"
#include "buildctx.h"
ASSUME_NONNULL_BEGIN

// T* resolve_ast(BuildCtx*, Scope* lookupscope, T* n)
// lookupscope is the outer scope to use for looking up unresolved identifiers.
Node* resolve_ast(BuildCtx*, Scope* lookupscope, Node* n);
#define resolve_ast(b, s, n) ( (__typeof__(n)) resolve_ast((b), (s), as_Node(n)) )

ASSUME_NONNULL_END
