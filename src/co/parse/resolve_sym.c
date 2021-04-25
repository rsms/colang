// Resolve identifiers in an AST. Usuaully run right after parsing.
#include <rbase/rbase.h>
#include "parse.h"


typedef struct {
  BuildCtx*  build;
  ParseFlags flags;
  u32        funNest;    // level of function nesting. 0 at file level
  u32        assignNest; // level of assignment. Used to avoid early constant folding.
} ResCtx;


static Node* resolve(Node* n, Scope* scope, ResCtx* ctx);


Node* ResolveSym(BuildCtx* build, ParseFlags fl, Node* n, Scope* scope) {
  ResCtx ctx = {
    .build = build,
    .flags = fl,
  };
  return resolve(n, scope, &ctx);
}


static Node* resolveId(Node* n, Scope* scope, ResCtx* ctx) {
  assert(n->kind == NId);
  auto name = n->ref.name;
  // dlog("resolveId BEGIN %s", name);
  while (1) {
    auto target = n->ref.target;
    // dlog("  ITER %s", n->ref.name);

    if (target == NULL) {
      // dlog("  LOOKUP %s", n->ref.name);
      target = (Node*)ScopeLookup(scope, n->ref.name);
      if (target == NULL) {
        build_errf(ctx->build, n->pos, "undefined symbol %s", name);
        n->ref.target = (Node*)NodeBad;
        return n;
      }
      n->ref.target = target;
      // dlog("  UNWIND %s => %s", n->ref.name, NodeKindName(target->kind));
    }

    // dlog("  SWITCH target %s", NodeKindName(target->kind));
    switch (target->kind) {
      case NId:
        // note: all built-ins which are const have targets, meaning the code above will
        // not mutate those nodes.
        n = target;
        break; // continue unwind loop

      case NLet:
        // Unwind let bindings
        assert(target->field.init != NULL);
        if (!NodeKindIsExpr(target->field.init->kind)) {
          // in the case of a let target with a constant or type, resolve to that.
          // Example:
          //   "x = true ; y = x"
          //  parsed as:
          //   (Let (Id x) (BoolLit true))
          //   (Let (Id y) (Id x))
          //  transformed to:
          //   (Let (Id x) (BoolLit true))
          //   (Let (Id y) (BoolLit true))
          //
          return target->field.init;
        }
        return n;

      case NBoolLit:
      case NIntLit:
      case NNil:
      case NFun:
      case NBasicType:
      case NTupleType:
      case NFunType:
        // unwind identifier to constant/immutable value.
        // Example:
        //   (Id true #user) -> (Id true #builtin) -> (Bool true #builtin)
        //
        // dlog("  RET target %s -> %s", NodeKindName(target->kind), fmtnode(target));
        if (ctx->assignNest > 0) {
          // assignNest is >0 when resolving the LHS of an assignment.
          // In this case we do not unwind constants as that would lead to things like this:
          //   (assign (tuple (ident a) (ident b)) (tuple (int 1) (int 2))) =>
          //   (assign (tuple (int 1) (int 2)) (tuple (int 1) (int 2)))
          return n;
        }
        return target;

      default:
        assert(!NodeKindIsConst(target->kind)); // should be covered in case-statements above
        // dlog("resolveId FINAL %s => %s (target %s) type? %d",
        //   n->ref.name, NodeKindName(n->kind), NodeKindName(target->kind),
        //   NodeKindIsType(target->kind));
        // dlog("  RET n %s -> %s", NodeKindName(n->kind), fmtnode(n));
        return n;
    }
  }
}

//
// IMPORTANT: symbol resolution is only run when the parser was unable to resolve all names up-
// front. So, this code should ONLY RESOLVE stuff and apply any required transformations that the
// parser applies after resolution, like for example "Foo(3) ; Foo = int" which is parsed as a call
// to "Foo" ("Foo" is unknown) and must be converted to a TypeCast since Foo denotes a type.
//
static Node* resolve(Node* n, Scope* scope, ResCtx* ctx) {
  // dlog("resolve(%s, scope=%p)", NodeKindName(n->kind), scope);

  if (n->type != NULL && n->type->kind != NBasicType) {
    if (n->kind == NFun) {
      ctx->funNest++;
    }
    n->type = resolve((Node*)n->type, scope, ctx);
    if (n->kind == NFun) {
      ctx->funNest--;
    }
  }

  switch (n->kind) {

  // uses u.ref
  case NId:
    return resolveId(n, scope, ctx);

  // uses u.array
  case NBlock:
  case NTuple:
  case NFile: {
    if (n->array.scope) {
      scope = n->array.scope;
    }
    Node* lastn = NULL;
    NodeListMap(&n->array.a, n,
      /* n <= */ lastn = resolve(n, scope, ctx)
    );
    // simplify blocks with a single expression; (block expr) => expr
    if (n->kind == NBlock && NodeListLen(&n->array.a) == 1) {
      return lastn;
    }
    break;
  }

  // uses u.fun
  case NFun: {
    ctx->funNest++;
    if (n->fun.params) {
      n->fun.params = resolve(n->fun.params, scope, ctx);
    }
    if (n->type) {
      n->type = resolve(n->type, scope, ctx);
    }
    auto body = n->fun.body;
    if (body) {
      if (n->fun.scope) {
        scope = n->fun.scope;
      }
      n->fun.body = resolve(body, scope, ctx);
    }
    ctx->funNest--;
    break;
  }

  // uses u.op
  case NAssign: {
    ctx->assignNest++;
    resolve(n->op.left, scope, ctx);
    ctx->assignNest--;
    assert(n->op.right != NULL);
    n->op.right = resolve(n->op.right, scope, ctx);
    break;
  }
  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
  case NReturn: {
    auto newleft = resolve(n->op.left, scope, ctx);
    if (n->op.left->kind != NId) {
      // note: in case of assignment where the left side is an identifier,
      // avoid replacing the identifier with its value.
      // This branch is taken in all other cases.
      n->op.left = newleft;
    }
    if (n->op.right) {
      n->op.right = resolve(n->op.right, scope, ctx);
    }
    break;
  }

  // uses u.call
  case NTypeCast:
    n->call.args = resolve(n->call.args, scope, ctx);
    n->call.receiver = resolve(n->call.receiver, scope, ctx);
    break;

  case NCall:
    n->call.args = resolve(n->call.args, scope, ctx);
    auto recv = resolve(n->call.receiver, scope, ctx);
    n->call.receiver = recv;
    if (recv->kind != NFun) {
      // convert to type cast, if receiver is a type. e.g. "x = uint8(4)"
      if (recv->kind == NBasicType) {
        n->kind = NTypeCast;
      } else {
        build_errf(ctx->build, n->pos, "cannot call %s", fmtnode(recv));
      }
    }
    break;

  // uses u.field
  case NLet:
  case NArg:
  case NField: {
    if (n->field.init) {
      n->field.init = resolve(n->field.init, scope, ctx);
    }
    break;
  }

  // uses u.cond
  case NIf:
    n->cond.cond = resolve(n->cond.cond, scope, ctx);
    n->cond.thenb = resolve(n->cond.thenb, scope, ctx);
    if (ctx->flags & ParseOpt)
      n = ast_opt_ifcond(n);
    break;

  case NNone:
  case NBad:
  case NBasicType:
  case NFunType:
  case NTupleType:
  case NComment:
  case NNil:
  case NBoolLit:
  case NIntLit:
  case NFloatLit:
  case NZeroInit:
  case _NodeKindMax:
    break;

  } // switch n->kind

  return n;
}

