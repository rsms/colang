#include "../coimpl.h"
#include "../test.h"
#include "ast.h"
#include "universe.h"


DEF_TEST(ast_typecast) {
  asserteq(false, NodeKindIsStmt(NBad));
  asserteq(true, NodeKindIsStmt(NFile));
  asserteq(false, NodeKindIsStmt(NBinOp));
  asserteq(false, NodeKindIsStmt(NBasicType));

  asserteq(false, NodeKindIsExpr(NBad));
  asserteq(false, NodeKindIsExpr(NFile));
  asserteq(true, NodeKindIsExpr(NBinOp));
  asserteq(false, NodeKindIsExpr(NBasicType));

  // as_TYPE
  Expr nst = {0};
  Node* n = (Node*)&nst;
  const Node* cn = (const Node*)&nst;

  {
    UNUSED Node* a = as_Node(n);
    UNUSED const Node* b = as_Node(n);
    UNUSED const Node* c = as_Node(cn);
    // this should cause warning -Wincompatible-pointer-types-discards-qualifiers
    // because d is non-const while the result of as_Node is const (since cn is const.)
    //UNUSED Node* d = as_Node(cn);
  }

  // assert_is_Stmt(n) should fail for Bad (NodeKind 0)
  NodeKind nk = assertnotnull(n)->kind;
  assert(nk == 0);
  asserteq(false, NodeKindIsStmt(nk));

  // node of a deeply nested type
  n->kind = NBinOp;
  {
    UNUSED Node* a = as_Node(n); // BinOp is a node of course
    UNUSED Expr* b = as_Expr(n); // BinOp is an expression (based on Expr struct)
    UNUSED BinOpNode* c = as_BinOpNode(n);
    UNUSED const BinOpNode* d = as_BinOpNode(cn);
  }

  // TypeOfNode
  {
    BinOpNode* op = (BinOpNode*)n;
    const BinOpNode* cop = (const BinOpNode*)cn;
    UNUSED Type* t = TypeOfNode(op);
    UNUSED const Type* ct = TypeOfNode(cop);
    // this should cause warning -Wincompatible-pointer-types-discards-qualifiers:
    //UNUSED Type* ct2 = TypeOfNode(cop);

    // Type* => kType_type
    t = TypeOfNode(t);
    asserteq(t, kType_type);

    // kType_type => kType_type
    t = TypeOfNode(kType_type);
    asserteq(t, kType_type);
  }
}
