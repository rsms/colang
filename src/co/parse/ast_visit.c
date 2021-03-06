#include "../common.h"
#include "parse.h"


bool NodeVisitChildren(NodeList* parent, void* nullable data, NodeVisitor f) {
  #define CALLBACK(child, fieldname_) ({ \
    auto _tmp_cn = (child);              \
    NodeList nl = (NodeList){            \
      .n = _tmp_cn ? _tmp_cn : Const_nil,\
      .parent = parent,                  \
      .fieldname = fieldname_,           \
    };                                   \
    f(&nl, data);                        \
  })

  auto n = parent->n;
  switch (n->kind) {

  case NId:
    return CALLBACK(n->id.target, "target");
    break;

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

  case NBlock:
  case NArray:
  case NTuple: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->array.a.len; i++) {
      nl.n = n->array.a.v[i];
      nl.index = i;
      if (!nl.n)
        nl.n = Const_nil;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  case NVar:
    if (!NodeIsParam(n) || n->var.init)
      return CALLBACK(n->var.init, "init");
    break;

  case NRef:
    return CALLBACK(n->ref.target, "target");

  case NField:
    return CALLBACK(n->field.init, "init");

  case NNamedVal:
    return CALLBACK(n->namedval.value, "value");

  case NFun:
    return (
      CALLBACK(n->fun.params, "params") &&
      CALLBACK(n->fun.result, "result") &&
      CALLBACK(n->fun.body, "body") );

  case NMacro:
    return CALLBACK(n->macro.params, "params") && CALLBACK(n->macro.template, "template");

  case NTypeCast:
  case NCall:
    return CALLBACK(n->call.receiver, "recv") && CALLBACK(n->call.args, "args");

  case NIf:
    if (!CALLBACK(n->cond.cond, "cond") || !CALLBACK(n->cond.thenb, "then"))
      return false;
    if (n->cond.elseb)
      return CALLBACK(n->cond.elseb, "else");
    break;

  case NSelector:
    return CALLBACK(n->sel.operand, "operand");

  case NIndex:
    return CALLBACK(n->index.operand, "operand") &&
           CALLBACK(n->index.indexexpr, "index");

  case NSlice:
    return (
      CALLBACK(n->slice.operand, "operand") &&
      CALLBACK(n->slice.start, "start") &&
      CALLBACK(n->slice.end, "end") );

  case NRefType:
    return CALLBACK(n->t.ref, "elem");

  case NFunType:
    return CALLBACK(n->t.fun.params ? n->t.fun.params->type : NULL, "params") &&
           CALLBACK(n->t.fun.result, "result");

  case NTupleType: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->t.tuple.a.len; i++) {
      nl.n = n->t.tuple.a.v[i];
      nl.index = i;
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  case NArrayType:
    if (n->t.array.size == 0 && !CALLBACK(n->t.array.sizeexpr, "sizeexpr"))
      return false;
    return CALLBACK(n->t.array.subtype, "subtype");

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

  case NTypeType:
    return CALLBACK(assertnotnull_debug(n->t.type), "type");

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
