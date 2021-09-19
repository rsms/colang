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

  // ref
  case NId:
    if (n->ref.target)
      return CALLBACK(n->ref.target, "target");
    break;

  // op
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

  // cunit
  case NFile:
  case NPkg: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->cunit.a.len; i++) {
      nl.n = n->cunit.a.v[i];
      nl.index = i;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  // array
  case NBlock:
  case NArray:
  case NTuple: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->array.a.len; i++) {
      nl.n = n->array.a.v[i];
      nl.index = i;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  // var
  case NVar:
  case NParam:
    if (n->var.init)
      return CALLBACK(n->var.init, "init");
    break;

  // field
  case NField:
    if (n->field.init)
      return CALLBACK(n->field.init, "init");
    break;

  // fun
  case NFun:
    if (n->fun.params && !CALLBACK(n->fun.params, "params"))
      return false;
    if (n->fun.result && !CALLBACK(n->fun.result, "result"))
      return false;
    if (n->fun.body)
      return CALLBACK(n->fun.body, "body");
    break;

  // call
  case NTypeCast:
  case NStructCons:
  case NCall:
    if (!CALLBACK(n->call.receiver, "recv"))
      return false;
    if (n->call.args)
      return CALLBACK(n->call.args, "args");
    break;

  // cond
  case NIf:
    if (!CALLBACK(n->cond.cond, "cond"))
      return false;
    if (!CALLBACK(n->cond.thenb, "then"))
      return false;
    if (n->cond.elseb)
      return CALLBACK(n->cond.elseb, "else");
    break;

  case NSelector:
    if (n->sel.target && !CALLBACK(n->sel.target, "target"))
      return false;
    return CALLBACK(n->sel.operand, "operand");

  case NIndex:
    return CALLBACK(n->index.operand, "operand") && CALLBACK(n->index.index, "index");

  case NSlice:
    return (
      CALLBACK(n->slice.operand, "operand") &&
      (!n->slice.start || CALLBACK(n->slice.start, "start")) &&
      (!n->slice.end || CALLBACK(n->slice.end, "end"))
    );

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
    if (n->t.array.size == 0 && n->t.array.sizeExpr &&
        !CALLBACK(n->t.array.sizeExpr, "sizeexpr"))
    {
      return false;
    }
    return CALLBACK(n->t.array.subtype, "subtype");

  // uses t.struc
  case NStructType: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->t.struc.a.len; i++) {
      nl.n = n->t.struc.a.v[i];
      nl.index = i;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

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
