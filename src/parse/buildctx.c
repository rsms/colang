#include "../coimpl.h"
#include "buildctx.h"

#ifdef CO_WITH_LIBC
  #include <stdio.h> // vsnprintf
#endif

void buildctx_init(
  BuildCtx*             ctx,
  Mem                   mem,
  SymPool*              syms,
  Pkg*                  pkg,
  DiagHandler* nullable diagh,
  void*                 userdata)
{
  assert(mem != NULL);
  memset(ctx, 0, sizeof(BuildCtx));
  ctx->mem       = mem;
  ctx->safe      = true;
  ctx->syms      = syms;
  ctx->pkg       = pkg;
  ctx->diagh     = diagh;
  ctx->userdata  = userdata;
  ctx->diaglevel = DiagMAX;
  ctx->sint_type = sizeof(long) > 4 ? TC_i64 : TC_i32; // default to host size
  ctx->uint_type = sizeof(long) > 4 ? TC_u64 : TC_u32;
  SymMapInit(&ctx->types, ctx->types_st, countof(ctx->types_st), mem);
  DiagnosticArrayInit(&ctx->diagarray);
  posmap_init(&ctx->posmap, mem);
}

void buildctx_dispose(BuildCtx* ctx) {
  DiagnosticArrayFree(&ctx->diagarray, ctx->mem);
  SymMapDispose(&ctx->types);
  posmap_dispose(&ctx->posmap);
  #if DEBUG
  memset(ctx, 0, sizeof(BuildCtx));
  #endif
}

Diagnostic* buildctx_mkdiag(BuildCtx* ctx) {
  Diagnostic* d = memalloct(ctx->mem, Diagnostic);
  d->build = ctx;
  DiagnosticArrayPush(&ctx->diagarray, d, ctx->mem);
  return d;
}

void buildctx_diag(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* message) {
  if (level <= DiagError)
    ctx->errcount++;
  if (level > ctx->diaglevel || ctx->diagh == NULL)
    return;
  Diagnostic* d = buildctx_mkdiag(ctx);
  d->level = level;
  d->pos = pos;
  d->message = mem_strdup(ctx->mem, message);
  ctx->diagh(d, ctx->userdata);
}

void buildctx_diagv(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* fmt, va_list ap) {
  if (level > ctx->diaglevel || ctx->diagh == NULL) {
    if (level <= DiagError)
      ctx->errcount++;
    return;
  }
  #ifndef CO_WITH_LIBC
    #warning TODO implement buildctx_diagv for non-libc (need vsnprintf)
    // Note: maybe we can implement str_appendfmtv for non-libc and use that?
  #else
    char buf[256];
    va_list ap1;
    va_copy(ap1, ap);
    isize n = vsnprintf(buf, sizeof(buf), fmt, ap1);
    if (n < (isize)sizeof(buf))
      return buildctx_diag(ctx, level, pos, buf);
    // buf too small; heap allocate
    char* buf2 = (char*)memalloc(ctx->mem, n + 1);
    n = vsnprintf(buf2, n + 1, fmt, ap);
    buildctx_diag(ctx, level, pos, buf2);
    memfree(ctx->mem, buf2);
  #endif
}

void buildctx_diagf(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  buildctx_diagv(ctx, level, pos, fmt, ap);
  va_end(ap);
}

void buildctx_errf(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  buildctx_diagv(ctx, DiagError, pos, fmt, ap);
  va_end(ap);
}

void buildctx_warnf(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  buildctx_diagv(ctx, DiagWarn, pos, fmt, ap);
  va_end(ap);
}

void buildctx_notef(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  buildctx_diagv(ctx, DiagNote, pos, fmt, ap);
  va_end(ap);
}


#if defined(CO_TEST) && defined(CO_WITH_LIBC)

BuildCtx* test_buildctx_new() {
  Mem mem = mem_libc_allocator();

  SymPool* syms = memalloct(mem, SymPool);
  sympool_init(syms, universe_syms(), mem, NULL);

  Pkg* pkg = memalloct(mem, Pkg);
  pkg->dir = ".";

  BuildCtx* ctx = memalloct(mem, BuildCtx);
  buildctx_init(ctx, mem, syms, pkg, NULL, NULL);

  return b;
}

void test_buildctx_free(BuildCtx* ctx) {
  auto mem = ctx->mem;
  sympool_dispose(ctx->syms);
  memfree(mem, ctx->pkg);
  memfree(mem, ctx->syms);
  buildctx_dispose(ctx);
  // MemLinearFree(mem);
}

#endif // defined(CO_TEST) && defined(CO_WITH_LIBC)
