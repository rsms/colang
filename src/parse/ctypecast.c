#include "../coimpl.h"
#include "ctypecast.h"

Expr* ctypecast(
  BuildCtx* b, Expr* n, Type* t, CTypecastResult* nullable resp, CTypecastFlags fl)
{
  CTypecastResult restmp;
  CTypecastResult* res = resp ? resp : &restmp;

  if (b_typeeq(b, n->type, t)) {
    *res = CTypecastUnchanged;
    return n;
  }

  // TODO
  *res = CTypecastErrCompat;
  return n;
}
