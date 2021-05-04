#include <rbase/rbase.h>
#include "ir.h"

// ===============================================================================================
// pkg

IRPkg* IRPkgNew(Mem nullable mem, const char* id) {
  size_t idlen = id ? strlen(id) : 0;
  auto pkg = (IRPkg*)memalloc(mem, sizeof(IRPkg) + idlen + 1);
  pkg->mem = mem;
  SymMapInit(&pkg->funs, 32, mem);
  if (id == NULL) {
    pkg->id = "_";
  } else {
    char* id2 = ((char*)pkg) + sizeof(IRPkg);
    memcpy(id2, id, idlen);
    id2[idlen] = 0;
    pkg->id = id2;
  }
  return pkg;
}


void IRPkgAddFun(IRPkg* pkg, IRFun* f) {
  assertnotnull(f->name);
  SymMapSet(&pkg->funs, f->name, f);
}


IRFun* nullable IRPkgGetFun(IRPkg* pkg, Sym name) {
  assertnotnull(name);
  return SymMapGet(&pkg->funs, name);
}

// ===============================================================================================
// fun

IRFun* IRFunNew(Mem mem, Sym typeid, Sym name, Pos pos, u32 nparams) {
  auto f = (IRFun*)memalloc(mem, sizeof(IRFun));
  f->mem = mem;
  f->typeid = typeid;
  f->name = name; assertnotnull(name);
  f->pos = pos; // copy
  f->nparams = nparams;
  ArrayInitWithStorage(&f->blocks, f->blocksStorage, sizeof(f->blocksStorage)/sizeof(void*));
  return f;
}


static IRValue* getConst64(IRFun* f, TypeCode t, u64 value) {

  // TODO: simplify const cache to just hold int32 and int64 since we can store all
  // values in these.

  // dlog("getConst64 t=%s value=%llX", TypeCodeName(t), value);
  int addHint = 0;
  auto v = IRConstCacheGet(f->consts, f->mem, t, value, &addHint);
  if (v == NULL) {
    auto op = IROpConstFromAST(t);
    assert(IROpInfo(op)->aux != IRAuxNone);
    // Create const operation and add it to the entry block of function f
    v = IRValueNew(f, f->blocks.v[0], op, t, NoPos);
    v->auxInt = value;
    f->consts = IRConstCacheAdd(f->consts, f->mem, t, value, v, addHint);
    // dlog("getConst64 add new const op=%s value=%llX => v%u", IROpNames[op], value, v->id);
  } else {
    // dlog("getConst64 use cached const op=%s value=%llX => v%u", IROpNames[v->op], value, v->id);
  }
  return v;
}

// returns a constant IRValue representing n for type t
IRValue* IRFunGetConstBool(IRFun* f, bool value) {
  // TODO: as there are just two values; avoid using the const cache.
  return getConst64(f, TypeCode_bool, value ? 1 : 0);
}

// returns a constant IRValue representing n for type t
IRValue* IRFunGetConstInt(IRFun* f, TypeCode t, u64 value) {
  assert(TypeCodeIsInt(t));
  return getConst64(f, t, value);
}

IRValue* IRFunGetConstFloat(IRFun* f, TypeCode t, double value) {
  assert(TypeCodeIsFloat(t));
  // reintrepret bits (double is IEEE 754 in C11)
  u64 ivalue = *(u64*)(&value);
  return getConst64(f, t, ivalue);
}

void IRFunMoveBlockToEnd(IRFun* f, u32 blockIndex) {
  // moves block at index to end of f->blocks
  assert(f->blocks.len > blockIndex);
  if (f->blocks.len > blockIndex + 1) {
    // not last
    auto b = assertnotnull(f->blocks.v[blockIndex]);

    // shift all blocks after blockIndex one step to the left
    // e.g. given blockIndex=2:
    //  0 1 2 3 4
    // [a,b,c,d,e]
    // [a,b,d,d,e]
    // [a,b,d,e,e]
    u32 end = f->blocks.len - 1;
    u32 i = blockIndex;
    for (; i < end; i++) {
      f->blocks.v[i] = f->blocks.v[i + 1];
    }
    f->blocks.v[i] = b;
  }
}

void IRFunInvalidateCFG(IRFun* f) {
  // TODO
  // f->cachedPostorder = NULL;
  // f->cachedLoopnest = NULL;
  // f->cachedIdom = NULL;
  // f->cachedSdom = NULL;
}

// ===============================================================================================
// block

IRBlock* IRBlockNew(IRFun* f, IRBlockKind kind, Pos pos) {
  assert(f->bid < 0xFFFFFFFF); // too many block IDs generated
  auto b = memalloct(f->mem, IRBlock);
  b->f = f;
  b->id = f->bid++;
  b->kind = kind;
  b->pos = pos;
  ArrayInitWithStorage(&b->values, b->valuesStorage, sizeof(b->valuesStorage)/sizeof(void*));
  ArrayPush(&f->blocks, b, b->f->mem);
  return b;
}


void IRBlockDiscard(IRBlock* b) {
  assert(b->f != NULL);
  auto blocks = &b->f->blocks;

  #if DEBUG
  // make sure no other block refers to this block
  for (u32 i = 0; i < blocks->len; i++) {
    auto b2 = (IRBlock*)blocks->v[i];
    if (b2 == b) {
      continue;
    }
    assertf(b2->preds[0] != b, "b%u holds a reference to b%u (preds[0])", b2->id, b->id);
    assertf(b2->preds[1] != b, "b%u holds a reference to b%u (preds[1])", b2->id, b->id);
    assertf(b2->succs[0] != b, "b%u holds a reference to b%u (succs[0])", b2->id, b->id);
    assertf(b2->succs[1] != b, "b%u holds a reference to b%u (succs[1])", b2->id, b->id);
  }
  #endif

  if (blocks->v[blocks->len - 1] == b) {
    blocks->len--;
  } else {
    auto i = ArrayIndexOf(blocks, b);
    assert(i > -1);
    ArrayRemove(blocks, i, 1);
  }
  memfree(b->f->mem, b);
}


void IRBlockSetControl(IRBlock* b, IRValue* v) {
  auto prev = b->control;
  b->control = v;
  if (v)
    v->uses++;
  if (prev)
    prev->uses--;
}


static void IRBlockAddPred(IRBlock* b, IRBlock* pred) {
  assert(!b->sealed); // cannot modify preds after block is sealed
  // pick first available hole in fixed-size array:
  for (u32 i = 0; i < countof(b->preds); i++) {
    if (b->preds[i] == NULL) {
      b->preds[i] = pred;
      return;
    }
  }
  assert(0 && "trying to add more than countof(IRBlock.preds) blocks");
}

static void IRBlockAddSucc(IRBlock* b, IRBlock* succ) {
  // pick first available hole in fixed-size array:
  for (u32 i = 0; i < countof(b->succs); i++) {
    if (b->succs[i] == NULL) {
      b->succs[i] = succ;
      return;
    }
  }
  assert(0 && "trying to add more than countof(IRBlock.succs) blocks");
}

void IRBlockAddEdgeTo(IRBlock* b1, IRBlock* b2) {
  assert(!b1->sealed); // cannot modify preds after block is sealed
  IRBlockAddSucc(b1, b2); // b1 -> b2
  IRBlockAddPred(b2, b1); // b2 <- b1
  assert(b1->f != NULL);
  assert(b1->f == b2->f); // blocks must be part of the same function
  IRFunInvalidateCFG(b1->f);
}


void IRBlockSetPred(IRBlock* b, u32 index, IRBlock* pred) {
  assert(!b->sealed);
  assert(index < countof(b->preds));
  b->preds[index] = pred;
  assert(b->f != NULL);
  IRFunInvalidateCFG(b->f);
}

void IRBlockDelPred(IRBlock* b, u32 index) {
  assert(!b->sealed);
  assert(index < countof(b->preds));
  if (b->preds[index] != NULL) {
    b->preds[index] = NULL;
    assert(b->f != NULL);
    IRFunInvalidateCFG(b->f);
  }
}


void IRBlockSetSucc(IRBlock* b, u32 index, IRBlock* succ) {
  assert(index < countof(b->succs));
  b->succs[index] = succ;
  assert(b->f != NULL);
  IRFunInvalidateCFG(b->f);
}

void IRBlockDelSucc(IRBlock* b, u32 index) {
  assert(index < countof(b->succs));
  if (b->succs[index] != NULL) {
    b->succs[index] = NULL;
    assert(b->f != NULL);
    IRFunInvalidateCFG(b->f);
  }
}

// ===============================================================================================
// value

IRValue* IRValueAlloc(Mem mem, IROp op, TypeCode type, Pos pos) {
  auto v = (IRValue*)memalloc(mem, sizeof(IRValue));
  v->id = IRValueNoID;
  v->op = op;
  v->type = type;
  v->pos = pos;
  ArrayInitWithStorage(&v->args, v->argsStorage, countof(v->argsStorage));
  return v;
}

IRValue* IRValueNew(IRFun* f, IRBlock* b, IROp op, TypeCode type, Pos pos) {
  auto v = IRValueAlloc(f->mem, op, type, pos);
  if (b)
    IRBlockAddValue(b, v);
  return v;
}

IRValue* IRValueClone(Mem mem, IRValue* v1) {
  auto v2 = (IRValue*)memalloc_raw(mem, sizeof(IRValue));
  *v2 = *v1;
  v2->uses = 0;
  ArrayInitWithStorage(&v2->args, v2->argsStorage, countof(v2->argsStorage));
  for (u32 i = 0; i < v1->args.len; i++)
    IRValueAddArg(v2, mem, v1->args.v[i]);
  if (v2->comment)
    v1->comment = str_cpy(v2->comment, str_len(v2->comment));
  return v2;
}

void IRValueAddComment(IRValue* v, Mem mem, const char* comment, u32 len) {
  if (len == 0)
    return;
  if (v->comment) {
    v->comment = str_appendcstr(v->comment, "; ");
    v->comment = str_append(v->comment, comment, len);
  } else {
    v->comment = str_cpy(comment, len);
  }
}

void IRValueAddArg(IRValue* v, Mem mem, IRValue* arg) {
  arg->uses++;
  ArrayPush(&v->args, arg, mem);
}

