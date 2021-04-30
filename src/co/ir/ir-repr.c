#include <rbase/rbase.h>
#include "ir.h"

typedef struct {
  Str  buf;
  bool includeTypes;
} IRRepr;

static void reprValue(IRRepr* r, const IRValue* v) {
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
  for (u8 i = 0; i < v->argslen; i++) {
    r->buf = str_appendfmt(r->buf, i+1 < v->argslen ? " v%-2u " : " v%u", v->args[i]->id);
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
    case IRAuxF32:
      r->buf = str_appendfmt(r->buf, " [%f]", *(f32*)(&v->auxInt));
      break;
    case IRAuxI64:
      r->buf = str_appendfmt(r->buf, " [0x%llX]", v->auxInt);
      break;
    case IRAuxF64:
      r->buf = str_appendfmt(r->buf, " [%f]", *(f64*)(&v->auxInt));
      break;
  }

  // {aux}
  // TODO non-numeric aux

  // comment
  if (v->comment != NULL) {
    r->buf = str_appendfmt(r->buf, "\t# %u use ; %s", v->uses, v->comment);
  } else {
    r->buf = str_appendfmt(r->buf, "\t# %u use", v->uses);
  }

  r->buf = str_appendc(r->buf, '\n');
}



static void reprBlock(IRRepr* r, const IRBlock* b) {
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
  if (b->comment != NULL) {
    r->buf = str_appendfmt(r->buf, "\t # %s", b->comment);
  }
  r->buf = str_appendc(r->buf, '\n');

  // values
  for (u32 i = 0; i < b->values.len; i++)
    reprValue(r, b->values.v[i]);

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
    assert(b->control != NULL);
    r->buf = str_appendfmt(r->buf, "  ret v%u\n", b->control->id);
    break;

  }

  r->buf = str_appendc(r->buf, '\n');
}


static void reprFun(IRRepr* r, const IRFun* f) {
  r->buf = str_appendfmt(r->buf,
    "fun %s %s %p\n",
    f->name == NULL ? "_" : f->name,
    f->typeid == NULL ? "()" : f->typeid,
    f
  );
  for (u32 i = 0; i < f->blocks.len; i++)
    reprBlock(r, f->blocks.v[i]);
}


static void reprPkg(IRRepr* r, const IRPkg* pkg) {
  r->buf = str_appendfmt(r->buf, "package %s\n", pkg->id);
  for (u32 i = 0; i < pkg->funs.len; i++)
    reprFun(r, pkg->funs.v[i]);
}


Str IRReprPkgStr(const IRPkg* pkg, Str init) {
  IRRepr r = { .buf=init, .includeTypes=true };
  reprPkg(&r, pkg);
  return r.buf;
}
