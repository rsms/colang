// ctypecast converts the type of a constant expressions
#pragma once
#include "buildctx.h"
#include "ast.h"
ASSUME_NONNULL_BEGIN

typedef u32 CTypecastFlags;
typedef u32 CTypecastResult;

enum CTypecastFlags {
  CTypecastFImplicit = 0,      // implicit conversion
  CTypecastFExplicit = 1 << 0, // explicit conversion (greater range of conversions)
} END_TYPED_ENUM(CTypecastFlags)

enum CTypecastResult {
  CTypecastUnchanged = 0, // no conversion needed
  CTypecastConverted = 1, // type was successfully converted
  // conversion failed because...
  CTypecastErrCompat = -0x80, // ...type of value is not convertible to destination type
  CTypecastErrOverflow,       // ...the value would overflow the destination type
} END_TYPED_ENUM(CTypecastResult)

// ctypecast converts the type of the constant expressions n to t.
// On failure, a diagnostic error message is emitted in the build context.
// If res is non-null, a result code is stored to *res.
// Returns n or a replacement node allocated in the provided build context.
Expr* _ctypecast(BuildCtx*, Expr*, Type*, CTypecastResult* nullable resp, CTypecastFlags);
#define ctypecast(b, expr, t, resp, fl) _ctypecast((b),as_Expr(expr),as_Type(t),(resp),(fl))
#define ctypecast_implicit(bctx,n,t,resp) ctypecast((bctx),(n),(t),(resp),0)
#define ctypecast_explicit(bctx,n,t,resp) ctypecast((bctx),(n),(t),(resp),CTypecastFExplicit)

ASSUME_NONNULL_END
