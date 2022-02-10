// Pos -- compact representation of source positions
#pragma once
#include "../array.h"
#include "ast.h"
#include "buildctx.h"
ASSUME_NONNULL_BEGIN

// T* resolve_ast(BuildCtx*, Scope* parentscope, T* n)
Node* resolve_ast(BuildCtx*, Scope* parentscope, Node* n);
#define resolve_ast(b, s, n) ( (__typeof__(n)) resolve_ast((b), (s), as_Node(n)) )

ASSUME_NONNULL_END
