#include "../common.h"
#include "parse.h"


static Str nodepath1(NodeList* nl, Str s, int depth) {
  if (nl->parent)
    s = nodepath1(nl->parent, s, depth - 1);
  s = str_appendfmt(s, "\n%*.s%s %s [flags ",
    (int)(depth * 2), "",
    NodeKindName(nl->n->kind), fmtnode(nl->n));
  s = NodeFlagsStr(nl->n->flags, s);
  s = str_appendc(s, ']');
  return s;
}


static Str nodepath(NodeList* nl, Str s) {
  NodeList* parent = nl->parent;
  int depth = 0;
  while (parent) {
    depth++;
    parent = parent->parent;
  }
  return nodepath1(nl, s, depth);
}


typedef struct ValidateCtx {
  Build* b;
  u32    errcount;
} ValidateCtx;


static bool visit(NodeList* nl, void* ctxp) {
  auto ctx = (ValidateCtx*)ctxp;
  auto n = nl->n;

  // ignore unused Let
  if (nl->n->kind == NLet && nl->n->field.nrefs == 0)
    return true;

  if (nl->parent && NodeIsUnresolved(n) && !NodeIsUnresolved(nl->parent->n)) {
    // error: node is marked as unresolved but its parent is not
    Str npath = nodepath(nl, str_new(64));
    build_errf(ctx->b, NodePosSpan(n),
      "inconsitent \"unresolved\" flags at:%s\nsource location:", npath);
    str_free(npath);
    ctx->errcount++;
    return true; // true = keep going
  }

  // descend
  return NodeVisitChildren(nl, visit, ctx);
}

bool NodeValidate(Build* b, Node* n) {
  ValidateCtx ctx = { .b = b };
  NodeVisit(n, visit, &ctx);
  return ctx.errcount == 0;
}
