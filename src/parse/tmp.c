#include "parse.h"

#define NODE_STRUCT_END(NAME) \
  static_assert(sizeof(struct NAME) <= 96, "Node structs grew! (" #NAME ")");

#define auto __auto_type

#define AS_BinOpNode(n) ({ assert(n->kind == NBinOp); (struct BinOpNode*)n; })

static void tmp(Node* n) {
  dlog("left: %p", AS_BinOpNode(n)->left);
}

