// resolution of identifiers & types and semantic analysis
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// T* resolve_ast(BuildCtx*, Scope* lookupscope, T* n)
// lookupscope is the outer scope to use for looking up unresolved identifiers.
Node* resolve_ast(BuildCtx*, Scope* lookupscope, Node* n);
#define resolve_ast(b, s, n) ( (__typeof__(n)) resolve_ast((b), (s), as_Node(n)) )

// resolve_id resolves an identifier by setting id->target and id->type=TypeOfNode(target).
//
// This function may be called on an already-resolved id with NodeIsRValue(id)
// to simplify the id, in which case the "target" argument is ignored and must be NULL.
//
// If target is NULL and the id has no existing target, the id is marked as "unresolved"
// using NodeSetUnresolved(id).
//
// Returns id or a simplification e.g. (Id true -> (BoolLit true)) returns (BoolLit true).
Node* resolve_id(IdNode* id, Node* nullable target);


END_INTERFACE
