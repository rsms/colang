// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

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
