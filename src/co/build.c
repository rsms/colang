#include <rbase/rbase.h>
#include "build.h"
#include "parse/parse.h" // universe_syms

void build_init(Build* b,
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
  b->errcount = 0;
}

void build_dispose(Build* b) {
  #if DEBUG
  memset(b, 0, sizeof(*b));
  #endif
}

void build_errf(Build* b, SrcPos pos, const char* format, ...) {
  b->errcount++;
  if (b->errh == NULL)
    return;

  va_list ap;
  va_start(ap, format);
  auto msg = str_new(32);
  if (strlen(format) > 0)
    msg = str_appendfmtv(msg, format, ap);
  va_end(ap);

  b->errh(pos, msg, b->userdata);
  str_free(msg);
}

const Source* nullable build_get_source(const Build* b, SrcPos pos) {
  // TODO: considering implementing something like lico and Pos/XPos from go
  //       https://golang.org/src/cmd/internal/src/pos.go
  //       https://golang.org/src/cmd/internal/src/xpos.go
  //       https://github.com/rsms/co/blob/master/src/pos.ts
  return pos.src;
}


#if R_UNIT_TEST_ENABLED

Build* test_build_new() {
  Mem mem = MemNew(0); // 0 = pagesize

  auto syms = memalloct(mem, SymPool);
  sympool_init(syms, universe_syms(), mem, NULL);

  auto pkg = memalloct(mem, Pkg);
  pkg->dir = ".";

  auto b = memalloct(mem, Build);
  build_init(b, mem, syms, pkg, NULL, NULL);

  return b;
}

void test_build_free(Build* b) {
  auto mem = b->mem;
  // sympool_dispose(b->syms); // not needed because of MemFree
  build_dispose(b);
  MemFree(mem); // drop all memory, thus no memfree calls need above
}

#endif /* R_UNIT_TEST_ENABLED */
