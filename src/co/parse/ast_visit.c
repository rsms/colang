#include "../common.h"
#include "parse.h"


bool NodeVisitChildren(NodeList* parent, void* nullable data, NodeVisitor f) {
  #define CALLBACK(child, fieldname_) ({ \
    NodeList nl = (NodeList){            \
      .n = (child),                      \
      .parent = parent,                  \
      .fieldname = fieldname_,           \
    };                                   \
    f(&nl, data);                        \
  })

  auto n = parent->n;
  switch (n->kind) {

  // uses u.ref
  case NId:
    if (n->ref.target)
      return CALLBACK(n->ref.target, "target");
    break;

  // uses u.op
  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
  case NAssign:
  case NReturn:
    if (!CALLBACK(n->op.left, "left"))
      return false;
    if (n->op.right)
      return CALLBACK(n->op.right, "right");
    break;

  // uses u.array
  case NBlock:
  case NTuple:
  case NFile:
  case NPkg:
  {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->array.a.len; i++) {
      nl.n = n->array.a.v[i];
      nl.index = i;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  case NLet:
    if (n->let.init)
      return CALLBACK(n->let.init, "init");
    break;

  case NArg:
  case NField:
    if (n->field.init)
      return CALLBACK(n->field.init, "init");
    break;

  // uses u.fun
  case NFun:
    if (n->fun.params && !CALLBACK(n->fun.params, "params"))
      return false;
    if (n->fun.result && !CALLBACK(n->fun.result, "result"))
      return false;
    if (n->fun.body)
      return CALLBACK(n->fun.body, "body");
    break;

  // uses u.call
  case NTypeCast:
  case NCall:
    if (!CALLBACK(n->call.receiver, "recv"))
      return false;
    if (n->call.args)
      return CALLBACK(n->call.args, "args");
    break;

  // uses u.cond
  case NIf:
    if (!CALLBACK(n->cond.cond, "cond"))
      return false;
    if (!CALLBACK(n->cond.thenb, "then"))
      return false;
    if (n->cond.elseb)
      return CALLBACK(n->cond.elseb, "else");
    break;

  // uses t.fun
  case NFunType:
    if (n->t.fun.params && !CALLBACK(n->t.fun.params, "params"))
      return false;
    if (n->t.fun.result)
      return CALLBACK(n->t.fun.result, "result");
    break;

  // uses t.list
  case NTupleType: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->t.list.a.len; i++) {
      nl.n = n->t.list.a.v[i];
      nl.index = i;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  // uses t.array
  case NArrayType:
    return CALLBACK(n->t.array.subtype, "subtype");

  // Remaining nodes has no children.
  // Note: No default case, so that the compiler warns us about missing cases.
  case NBad:
  case NBasicType:
  case NBoolLit:
  case NFloatLit:
  case NIntLit:
  case NNil:
  case NNone:
  case NStrLit:
  case _NodeKindMax:
    break;

  } // switch(n->kind)

  #undef NODELIST
  return true;
}
