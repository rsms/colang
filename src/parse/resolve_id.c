#include "../coimpl.h"
#include "universe.h"
#include "resolve_id.h"

static Node* simplify_id(IdNode* id, Node* nullable target);


Node* resolve_id(IdNode* id, Node* nullable target) {
  assertnotnull(id->name);

  if (target == NULL) {
    if (id->target) {
      // simplify already-resolved id, which must be flagged as an rvalue
      assert(NodeIsRValue(id));
      MUSTTAIL return simplify_id(id, target);
    }
    // mark id as unresolved
    NodeSetUnresolved(id);
    return as_Node(id);
  }

  assertnull(id->target);
  id->target = target;
  id->type = TypeOfNode(target);

  switch (target->kind) {
    case NMacro:
    case NFun:
      // Note: Don't transfer "unresolved" attribute of functions
      break;
    case NVar:
      // increment variable ref count
      NodeRefVar((VarNode*)target);
      FALLTHROUGH;
    default:
      NodeTransferUnresolved(id, target);
  }

  // set id const == target const
  id->flags = (id->flags & ~NF_Const) | (target->flags & NF_Const);

  MUSTTAIL return simplify_id(id, target);
}

// simplify_id returns an Id's target if it's a simple constant, or the id itself.
// This reduces the complexity of common code.
//
// For example:
//   var x bool
//   x = true
// Without simplifying these ids the AST would look like this:
//   (Var x (Id bool -> (BasicType bool)))
//   (Assign (Id x -> (Var x)) (Id true -> (BoolLit true)))
// With simplify_id, the AST would instead look like this:
//   (Var (Id x) (BasicType bool))
//   (Assign (Var x) (BoolLit true))
//
// Note:
//   We would need to make sure post_resolve_id uses the same algorithm to get the
//   same outcome for cases like this:
//     fun foo()   | (Fun foo
//       x = true  |   (Assign (Id x -> ?) (BoolLit true)) )
//     var x bool  | (Var x (BasicType bool))
//
static Node* simplify_id(IdNode* id, Node* nullable _ign) {
  assertnotnull(id->target);

  // unwind var targeting a type
  Node* tn = id->target;
  while (tn->kind == NVar && NodeIsConst(tn) && !NodeIsParam(tn) && ((VarNode*)tn)->init) {
    tn = ((VarNode*)tn)->init;
    // Note: no NodeUnrefVar here
  }

  // when the id is an rvalue, simplify its target no matter what kind it is
  if (NodeIsRValue(id)) {
    id->target = tn;
    id->type = TypeOfNode(tn);
  }

  // if the target is a pimitive constant, use that instead of the id node
  switch (id->target->kind) {
    case NNil:
    case NBasicType:
    case NBoolLit:
      return id->target;
    default:
      return (Node*)id;
  }
}
