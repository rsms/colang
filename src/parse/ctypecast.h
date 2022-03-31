// ctypecast converts the type of a constant expressions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef u32 CTypecastFlags;
typedef u32 CTypecastResult;

// ctypecast converts the type of the constant expressions n to t.
// On failure, a diagnostic error message is emitted in the build context.
// If res is non-null, a result code is stored to *res.
// Returns n or a replacement node allocated in the provided build context.
#define ctypecast(b, expr, t, resp, usernode, fl) \
  _ctypecast((b),as_Expr(expr),as_Type(t),(resp),as_Node(usernode),(fl))

#define ctypecast_implicit(b, expr, t, resp, usernode) \
  _ctypecast((b),as_Expr(expr),as_Type(t),(resp),as_Node(usernode),0)

#define ctypecast_explicit(b, expr, t, resp, usernode) \
  _ctypecast((b),as_Expr(expr),as_Type(t),(resp),as_Node(usernode),CTypecastFExplicit)

Expr* _ctypecast(
  BuildCtx*, Expr*, Type*,
  CTypecastResult* nullable resp, Node* nullable report_usernode, CTypecastFlags);

enum CTypecastFlags {
  CTypecastFImplicit = 0,      // implicit conversion
  CTypecastFExplicit = 1 << 0, // explicit conversion (greater range of conversions)
} END_ENUM(CTypecastFlags)

enum CTypecastResult {
  CTypecastUnchanged, // no conversion needed
  CTypecastConverted, // type was successfully converted
  // conversion failed because...
  CTypecastErrCompat, // ...type of value is not convertible to destination type
  CTypecastErrRangeOver,      // ...the value is too large for the destination type
  CTypecastErrRangeUnder,     // ...the value is too small for the destination type
  CTypecastErrNoMem,          // ...memory allocation for node copy failed
} END_ENUM(CTypecastResult)


END_INTERFACE
