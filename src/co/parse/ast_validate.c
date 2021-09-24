#include "../common.h"
#include "../util/ptrmap.h"
#include "parse.h"


static Str nodepath1(NodeList* nl, Str s, int depth) {
  if (nl->parent)
    s = nodepath1(nl->parent, s, depth - 1);
  s = str_appendfmt(s, "\n%*.s%s %s [flags: ",
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
  Build*            b;
  NodeValidateFlags fl;
  u32               errcount;
  PtrMap            seenmap; // functions we've already verified
} ValidateCtx;


static void report_error(ValidateCtx* ctx, NodeList* nl, const char* msg) {
  Str npath = nodepath(nl, str_new(64));
  build_errf(ctx->b, NodePosSpan(nl->n),
    "AST validation error: %s at:%s\nsource location:", msg, npath);
  str_free(npath);
  ctx->errcount++;
}


static bool visit(NodeList* nl, void* ctxp) {
  auto ctx = (ValidateCtx*)ctxp;
  auto n = nl->n;

  // skip primitive constants (nil, true, i32, etc.)
  if (NodeIsPrimitiveConst(n))
    return true;

  // skip already visited nodes
  if ((NodeIsType(n) || NodeIsExpr(n)) && PtrMapSet(&ctx->seenmap, n, (void*)n))
    return true;

  bool errors = false;

  // check "unresolved" integrity
  if (nl->parent &&
      NodeIsUnresolved(n) &&
      !NodeIsUnresolved(nl->parent->n) &&
      (n->kind != NVar || n->var.nrefs > 0) )
  {
    // node is marked as unresolved but its parent is not
    report_error(ctx, nl, "inconsitent \"unresolved\" flags");
    errors = true;
  }

  // check for missing types
  if ((ctx->fl & NodeValidateMissingTypes) &&
      n->kind != NTypeType && n->kind != NPkg && n->kind != NFile && !NodeIsType(n) &&
      n->type == NULL)
  {
    report_error(ctx, nl, "missing type");
    errors = true;
  }

  // check for "bad" nodes (placeholders used to recover parsing on syntax error)
  if (NodeKindClass(n->kind) == NodeClassNone && n->kind != NPkg && n->kind != NFile) {
    report_error(ctx, nl, "invalid AST node");
    errors = true;
  }

  // visit type
  if (!errors && n->type) {
    u32 errcount = ctx->errcount;
    if (!NodeVisitp(nl, n->type, ctx, visit))
      return false;
    errors = ctx->errcount > errcount;
  }

  // descend unless there were errors ("true" to keep going)
  return errors ? true : NodeVisitChildren(nl, ctxp, visit);
}

bool NodeValidate(Build* b, Node* n, NodeValidateFlags fl) {
  ValidateCtx ctx = { .b = b, .fl = fl };
  PtrMapInit(&ctx.seenmap, 64, MemHeap);
  NodeVisit(n, &ctx, visit);
  PtrMapDispose(&ctx.seenmap);
  return ctx.errcount == 0;
}
