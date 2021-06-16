#include "../common.h"
#include "ir.h"

typedef struct {
  Str           buf;
  const PosMap* posmap;
  bool          includeTypes;
} IRRepr;

static void ir_repr_value(IRRepr* r, const IRValue* v) {
  assert(v->op < Op_MAX);

  // vN type = Op
  r->buf = str_appendfmt(r->buf,
    "    v%-2u %-7s = %-*s",
    v->id,
    TypeCodeName(v->type),
    IROpNamesMaxLen,
    IROpNames[v->op]
  );

  // arg arg
  for (u8 i = 0; i < v->args.len; i++) {
    IRValue* arg = (IRValue*)v->args.v[i];
    r->buf = str_appendfmt(r->buf, i+1 < v->args.len ? " v%-2u" : " v%u", arg->id);
  }

  // [auxInt]
  auto opinfo = IROpInfo(v->op);
  switch (opinfo->aux) {
    case IRAuxNone:
      break;
    case IRAuxBool:
    case IRAuxI8:
    case IRAuxI16:
    case IRAuxI32:
      r->buf = str_appendfmt(r->buf, " [0x%X]", (u32)v->auxInt);
      break;
    case IRAuxI64:
      r->buf = str_appendfmt(r->buf, " [0x%llX]", v->auxInt);
      break;
    case IRAuxF32:
      r->buf = str_appendfmt(r->buf, " [%f]", *(f32*)(&v->auxInt));
      break;
    case IRAuxF64:
      r->buf = str_appendfmt(r->buf, " [%f]", *(f64*)(&v->auxInt));
      break;
    case IRAuxPtr:
      r->buf = str_appendfmt(r->buf, " [%p]", (void*)v->auxInt);
      break;
    case IRAuxSym:
      r->buf = str_appendfmt(r->buf, " [%s]", v->auxSym);
      break;
  }

  // {aux}
  // TODO non-numeric aux

  // comment
  const char* use = v->uses == 1 ? "use" : "uses";
  if (v->comment) {
    r->buf = str_appendfmt(r->buf, "\t# %s; %u %s", v->comment, v->uses, use);
  } else {
    r->buf = str_appendfmt(r->buf, "\t# %u %s", v->uses, use);
  }

  if (pos_isknown(v->pos)) {
    r->buf = str_appendcstr(r->buf, " (");
    r->buf = pos_str(r->posmap, v->pos, r->buf);
    r->buf = str_appendc(r->buf, ')');
  }

  r->buf = str_appendc(r->buf, '\n');
}



static void ir_repr_block(IRRepr* r, const IRBlock* b) {
  // start of block header
  r->buf = str_appendfmt(r->buf, "  b%u:", b->id);

  // predecessors
  if (b->preds[0] != NULL) {
    if (b->preds[1] != NULL) {
      r->buf = str_appendfmt(r->buf, " <- b%u b%u", b->preds[0]->id, b->preds[1]->id);
    } else {
      r->buf = str_appendfmt(r->buf, " <- b%u", b->preds[0]->id);
    }
  } else {
    assertf(b->preds[1] == NULL, "preds are not dense");
  }

  // end block header
  if (b->comment)
    r->buf = str_appendfmt(r->buf, "\t # %s", b->comment);
  r->buf = str_appendc(r->buf, '\n');

  // values
  for (u32 i = 0; i < b->values.len; i++)
    ir_repr_value(r, b->values.v[i]);

  // successors
  switch (b->kind) {
  case IRBlockInvalid:
    r->buf = str_appendcstr(r->buf, "  ?\n");
    break;

  case IRBlockCont: {
    auto contb = b->succs[0];
    if (contb != NULL) {
      r->buf = str_appendfmt(r->buf, "  cont -> b%u\n", contb->id);
    } else {
      r->buf = str_appendcstr(r->buf, "  cont -> ?\n");
    }
    break;
  }

  case IRBlockFirst:
  case IRBlockIf: {
    auto thenb = b->succs[0];
    auto elseb = b->succs[1];
    assert(thenb != NULL && elseb != NULL);
    assertf(b->control != NULL, "missing control value");
    r->buf = str_appendfmt(r->buf,
      "  %s v%u -> b%u b%u\n",
      b->kind == IRBlockIf ? "if" : "first",
      b->control->id,
      thenb->id,
      elseb->id
    );
    break;
  }

  case IRBlockRet:
    if (b->control) {
      r->buf = str_appendfmt(r->buf, "  ret v%u\n", b->control->id);
    } else {
      r->buf = str_appendcstr(r->buf, "  ret\n");
    }
    break;

  }

  r->buf = str_appendc(r->buf, '\n');
}


static void ir_repr_fun(IRRepr* r, const IRFun* f) {
  r->buf = str_appendfmt(r->buf, "fun %s %s", f->name, f->typeid);
  if (f->ncalls == 0)
    r->buf = str_appendcstr(r->buf, " nocall");
  if (IRFunIsPure(f))
    r->buf = str_appendcstr(r->buf, " pure");
  r->buf = str_appendc(r->buf, '\n');
  for (u32 i = 0; i < f->blocks.len; i++)
    ir_repr_block(r, f->blocks.v[i]);
}


static void fun_iter(Sym name, IRFun* f, bool* stop, IRRepr* r) {
  ir_repr_fun(r, f);
}

static void ir_repr_pkg(IRRepr* r, const IRPkg* pkg) {
  r->buf = str_appendfmt(r->buf, "package %s\n", pkg->id);
  SymMapIter(&pkg->funs, (SymMapIterator)fun_iter, r);
  // for (u32 i = 0; i < pkg->funs.len; i++)
  //   ir_repr_fun(r, pkg->funs.v[i]);
}


Str IRReprPkgStr(const IRPkg* pkg, const PosMap* posmap, Str init) {
  IRRepr r = {
    .buf = init,
    .posmap = posmap,
    .includeTypes = true,
  };
  ir_repr_pkg(&r, pkg);
  return r.buf;
}
