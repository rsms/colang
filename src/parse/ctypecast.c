// ctypecast converts the type of a constant expressions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PARSE_CTYPECAST_IMPLEMENTATION
#endif
#include "buildctx.c"
#include "ast.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

typedef u32 CTypecastFlags;
typedef u32 CTypecastResult;

// ctypecast converts the type of the constant expressions n to t.
// On failure, a diagnostic error message is emitted in the build context.
// If res is non-null, a result code is stored to *res.
// Returns n or a replacement node allocated in the provided build context.
#define ctypecast(b, expr, t, resp, fl) _ctypecast((b),as_Expr(expr),as_Type(t),(resp),(fl))
#define ctypecast_implicit(bctx,n,t,resp) ctypecast((bctx),(n),(t),(resp),0)
#define ctypecast_explicit(bctx,n,t,resp) ctypecast((bctx),(n),(t),(resp),CTypecastFExplicit)

Expr* _ctypecast(BuildCtx*, Expr*, Type*, CTypecastResult* nullable resp, CTypecastFlags);

enum CTypecastFlags {
  CTypecastFImplicit = 0,      // implicit conversion
  CTypecastFExplicit = 1 << 0, // explicit conversion (greater range of conversions)
} END_ENUM(CTypecastFlags)

enum CTypecastResult {
  CTypecastUnchanged = 0, // no conversion needed
  CTypecastConverted = 1, // type was successfully converted
  // conversion failed because...
  CTypecastErrCompat = -0x80, // ...type of value is not convertible to destination type
  CTypecastErrOverflow,       // ...the value would overflow the destination type
} END_ENUM(CTypecastResult)


//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PARSE_CTYPECAST_IMPLEMENTATION


Expr* _ctypecast(
  BuildCtx* b, Expr* n, Type* t, CTypecastResult* nullable resp, CTypecastFlags fl)
{
  assertnotnull(t);
  CTypecastResult restmp;
  CTypecastResult* res = resp ? resp : &restmp;

  if (n->type && b_typeeq(b, n->type, t)) {
    *res = CTypecastUnchanged;
    return n;
  }

  dlog("TODO");
  *res = CTypecastErrCompat;
  return n;
}

#endif // PARSE_CTYPECAST_IMPLEMENTATION
