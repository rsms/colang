#include <rbase/rbase.h>
#include "parse.h"

void build_init(BuildCtx* b,
  Mem nullable           mem,
  SymPool*               syms,
  Pkg*                   pkg,
  ErrorHandler* nullable errh,
  void*                  userdata)
{
  b->mem      = mem;
  b->syms     = syms;
  b->pkg      = pkg;
  b->errh     = errh;
  b->userdata = userdata;
  b->tmpbuf   = str_new(64);
}

void build_dispose(BuildCtx* b) {
  str_free(b->tmpbuf);
  #if DEBUG
  memset(b, 0, sizeof(*b));
  #endif
}

void build_errf(const BuildCtx* ctx, SrcPos pos, const char* format, ...) {
  if (ctx->errh == NULL)
    return;

  va_list ap;
  va_start(ap, format);
  auto msg = str_new(32);
  if (strlen(format) > 0)
    msg = str_appendfmtv(msg, format, ap);
  va_end(ap);

  ctx->errh(pos, msg, ctx->userdata);
  str_free(msg);
}


#if R_UNIT_TEST_ENABLED

BuildCtx* test_build_new() {
  Mem mem = MemNew(0); // 0 = pagesize

  auto syms = memalloct(mem, SymPool);
  sympool_init(syms, universe_syms(), mem, NULL);

  auto pkg = memalloct(mem, Pkg);
  pkg->dir = ".";

  auto b = memalloct(mem, BuildCtx);
  build_init(b, mem, syms, pkg, NULL, NULL);

  return b;
}

void test_build_free(BuildCtx* b) {
  auto mem = b->mem;
  // sympool_dispose(b->syms); // not needed because of MemFree
  build_dispose(b); // needed for tmpbuf
  MemFree(mem); // drop all memory, thus no memfree calls need above
}

#endif /* R_UNIT_TEST_ENABLED */
