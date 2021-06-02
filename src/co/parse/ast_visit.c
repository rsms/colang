#include "../common.h"
#include "parse.h"

// NodeVisitor is used with NodeVisit to traverse an AST.
// Call NodeVisitChildren to visit a nodes children.
// Return false to stop iteration.


// typedef struct NodeList NodeList;
// struct NodeList { NodeList* parent, Node* n };

// typedef bool(*NodeVisitor)(NodeList* n, void* nullable data);


bool NodeVisitChildren(NodeList* parent, NodeVisitor f, void* nullable data) {
  #define CALLBACK(child) ({                               \
    NodeList nl = (NodeList){ .parent = parent, .n = (child) }; \
    f(&nl, data);                                          \
  })

  Node* n = parent->n;
  switch (n->kind) {

  // uses u.ref
  case NId:
    if (n->ref.target)
      return CALLBACK(n->ref.target);
    break;

  // uses u.op
  case NBinOp:
  case NPostfixOp:
  case NPrefixOp:
  case NAssign:
  case NReturn:
    if (!CALLBACK(n->op.left))
      return false;
    if (n->op.right)
      return CALLBACK(n->op.right);
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
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  // uses u.field
  case NLet:
  case NArg:
  case NField:
    if (n->field.init)
      return CALLBACK(n->field.init);
    break;

  // uses u.fun
  case NFun:
    if (n->fun.params && !CALLBACK(n->fun.params))
      return false;
    if (n->fun.result && !CALLBACK(n->fun.result))
      return false;
    if (n->fun.body)
      return CALLBACK(n->fun.body);
    break;

  // uses u.call
  case NTypeCast:
  case NCall:
    if (!CALLBACK(n->call.receiver))
      return false;
    if (n->call.args)
      return CALLBACK(n->call.args);
    break;

  // uses u.cond
  case NIf:
    if (!CALLBACK(n->cond.cond))
      return false;
    if (!CALLBACK(n->cond.thenb))
      return false;
    if (n->cond.elseb)
      return CALLBACK(n->cond.elseb);
    break;

  // uses t.fun
  case NFunType:
    if (n->t.fun.params && !CALLBACK(n->t.fun.params))
      return false;
    if (n->t.fun.result)
      return CALLBACK(n->t.fun.result);
    break;

  // uses t.list
  case NTupleType: {
    NodeList nl = (NodeList){ .parent = parent };
    for (u32 i = 0; i < n->t.list.a.len; i++) {
      nl.n = n->t.list.a.v[i];
      if (!f(&nl, data))
        return false;
    }
    break;
  }

  // uses t.array
  case NArrayType:
    return CALLBACK(n->t.array.subtype);

  // Remaining nodes has no children.
  // Note: No default case, so that the compiler warns us about missing cases.
  case NBad:
  case NBasicType:
  case NBoolLit:
  case NComment:
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
