#include <rbase/rbase.h>
#include "ir.h"


IRValue* IRValueNew(IRFun* f, IRBlock* b, IROp op, TypeCode type, const SrcPos* pos) {
  assert(f->vid < 0xFFFFFFFF); // too many block IDs generated
  auto v = (IRValue*)memalloc(f->mem, sizeof(IRValue));
  v->id = f->vid++;
  v->op = op;
  v->type = type;
  if (pos)
    v->pos = *pos;
  if (b != NULL) {
    ArrayPush(&b->values, v, b->f->mem);
  } else {
    dlog("WARN IRValueNew b=NULL");
  }
  return v;
}

void IRValueAddComment(IRValue* v, Mem mem, ConstStr comment) {
  if (comment != NULL) { // allow passing NULL to do nothing
    auto commentLen = str_len(comment);
    if (commentLen > 0) {
      if (v->comment == NULL) {
        v->comment = memdup(mem, comment, commentLen + 1);
      } else {
        const char* delim = "; ";
        u32 len = strlen(v->comment);
        auto ptr = (char*)memalloc_raw(mem, len + strlen(delim) + commentLen + 1);
        memcpy(ptr, v->comment, len);
        memcpy(&ptr[len], delim, strlen(delim));
        memcpy(&ptr[len + strlen(delim)], comment, commentLen + 1); // +1 include nul
        v->comment = ptr;
      }
    }
  }
}

void IRValueAddArg(IRValue* v, IRValue* arg) {
  assert(v->argslen < countof(v->args));
  v->args[v->argslen++] = arg;
  arg->uses ++;
}
