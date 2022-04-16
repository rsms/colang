// resolution of identifiers & types and semantic analysis
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

Node* resolve_ast(BuildCtx* b, Node* n);
#define resolve_ast(b, n) ( (__typeof__(n)) resolve_ast((b), as_Node(n)) )

// resolve_id resolves an identifier by setting id->target and id->type=TypeOfNode(target).
// If target is NULL and the id has no existing target, the id is marked as "unresolved"
// using NodeSetUnresolved(id).
Expr* resolve_id_expr(IdNode* id, Expr* target);
Type* resolve_id_type(IdTypeNode* id, Type* target);


END_INTERFACE
