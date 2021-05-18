#include "common.h"
#include "build.h"
#include "parse/parse.h" // universe_syms

void build_init(Build* b,
  Mem                    mem,
  SymPool*               syms,
  Pkg*                   pkg,
  DiagHandler* nullable  diagh,
  void*                  userdata)
{
  b->mem       = mem;
  b->syms      = syms;
  b->pkg       = pkg;
  b->diagh     = diagh;
  b->userdata  = userdata;
  b->errcount  = 0;
  b->diaglevel = DiagMAX;
  ArrayInit(&b->diagarray);
  posmap_init(&b->posmap, mem);
}

void build_dispose(Build* b) {
  #if DEBUG
  memset(b, 0, sizeof(*b));
  #endif
}

Diagnostic* build_mkdiag(Build* b) {
  auto d = memalloct(b->mem, Diagnostic);
  d->build = b;
  ArrayPush(&b->diagarray, d, b->mem);
  return d;
}

void diag_free(Diagnostic* d) {
  assertnotnull(d->build);
  memfree(d->build->mem, (void*)d->message);
  memfree(d->build->mem, d);
}

void build_diag(Build* b, DiagLevel level, PosSpan pos, const char* message) {
  if (level <= DiagError)
    b->errcount++;
  if (level > b->diaglevel || b->diagh == NULL)
    return;
  auto d = build_mkdiag(b);
  d->level = level;
  d->pos = pos;
  d->message = memstrdup(b->mem, message);
  build_emit_diag(b, d);
}

void build_diagv(Build* b, DiagLevel level, PosSpan pos, const char* fmt, va_list ap) {
  if (level > b->diaglevel || b->diagh == NULL) {
    if (level <= DiagError)
      b->errcount++;
    return;
  }
  char buf[256];
  ssize_t n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n < (ssize_t)sizeof(buf))
    return build_diag(b, level, pos, buf);
  // buf too small; heap allocate
  auto msg = str_new(512);
  if (strlen(fmt) > 0)
    msg = str_appendfmtv(msg, fmt, ap);
  build_diag(b, level, pos, msg);
  str_free(msg);
}

void build_diagf(Build* b, DiagLevel level, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  build_diagv(b, level, pos, fmt, ap);
  va_end(ap);
}

static const char* const _DiagLevelName[DiagMAX + 1] = {
  "error",
  "warn",
  "info",
};

const char* DiagLevelName(DiagLevel l) {
  return _DiagLevelName[MAX(0, MIN(l, DiagMAX))];
}

Str diag_fmt(Str s, const Diagnostic* d) {
  assert(d->level <= DiagMAX);
  return pos_fmt(&d->build->posmap, d->pos, s,
    "%s: %s", DiagLevelName(d->level), d->message);
}


#if R_TESTING_ENABLED

Build* test_build_new() {
  Mem mem = MemArenaAlloc();

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
  MemArenaFree(mem);
}

#endif /* R_TESTING_ENABLED */
