#include "../common.h"
#include "../util/ptrmap.h"
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
  PtrMap funmap;    // functions we've already verified
} ValidateCtx;


static bool visit(NodeList* nl, void* ctxp) {
  auto ctx = (ValidateCtx*)ctxp;
  auto n = nl->n;

  switch (n->kind) {
    // ignore unused Let
    case NLet:
      if (n->field.nrefs == 0)
        return true;
      break;

    case NFun:
      if (PtrMapSet(&ctx->funmap, n, n)) {
        // already visited (replaced value in map)
        return true;
      }
      break;

    default:
      break;
  }

  // check "unresolved" integrity
  if (nl->parent && NodeIsUnresolved(n) && !NodeIsUnresolved(nl->parent->n)) {
    // node is marked as unresolved but its parent is not
    Str npath = nodepath(nl, str_new(64));
    build_errf(ctx->b, NodePosSpan(n),
      "inconsitent \"unresolved\" flags at:%s\nsource location:", npath);
    str_free(npath);
    ctx->errcount++;
    return true; // true = keep going
  }

  // descend
  return NodeVisitChildren(nl, ctxp, visit);
}

bool NodeValidate(Build* b, Node* n) {
  ValidateCtx ctx = { .b = b };
  PtrMapInit(&ctx.funmap, 64, MemHeap);
  NodeVisit(n, &ctx, visit);
  PtrMapDispose(&ctx.funmap);
  return ctx.errcount == 0;
}
