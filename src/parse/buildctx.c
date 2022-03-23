#include "../coimpl.h"
#include "../sys.h"
#include "buildctx.h"
#include "universe.h"
#include "typeid.h"

#ifndef CO_NO_LIBC
  #include <stdio.h> // vsnprintf
#endif

void BuildCtxInit(
  BuildCtx*             ctx,
  Mem                   mem,
  SymPool*              syms,
  const char*           pkgid,
  DiagHandler* nullable diagh,
  void*                 userdata)
{
  assertnotnull(mem);
  assert(strlen(pkgid) > 0);
  memset(ctx, 0, sizeof(BuildCtx));
  ctx->mem       = mem;
  ctx->safe      = true;
  ctx->syms      = syms;
  ctx->pkgid     = symget(syms, pkgid, strlen(pkgid));
  ctx->diagh     = diagh;
  ctx->userdata  = userdata;
  ctx->diaglevel = DiagMAX;
  ctx->sint_type = sizeof(long) > 4 ? TC_i64 : TC_i32; // default to host size
  ctx->uint_type = sizeof(long) > 4 ? TC_u64 : TC_u32;
  map_init_small(&ctx->types);
  DiagnosticArrayInit(&ctx->diagarray);
  posmap_init(&ctx->posmap, mem);
}

void BuildCtxDispose(BuildCtx* ctx) {
  DiagnosticArrayFree(&ctx->diagarray, ctx->mem);
  symmap_free(&ctx->types, ctx->mem);
  posmap_dispose(&ctx->posmap);
  #if DEBUG
  memset(ctx, 0, sizeof(BuildCtx));
  #endif
}

Diagnostic* b_mkdiag(BuildCtx* ctx) {
  Diagnostic* d = memalloct(ctx->mem, Diagnostic);
  d->build = ctx;
  DiagnosticArrayPush(&ctx->diagarray, d, ctx->mem);
  return d;
}

void b_diag(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* message) {
  if (level <= DiagError)
    ctx->errcount++;
  if (level > ctx->diaglevel || ctx->diagh == NULL)
    return;
  Diagnostic* d = b_mkdiag(ctx);
  d->level = level;
  d->pos = pos;
  d->message = mem_strdup(ctx->mem, message);
  ctx->diagh(d, ctx->userdata);
}

void b_diagv(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* fmt, va_list ap) {
  if (level > ctx->diaglevel || ctx->diagh == NULL) {
    if (level <= DiagError)
      ctx->errcount++;
    return;
  }
  #ifdef CO_NO_LIBC
    #warning TODO implement b_diagv for non-libc (need vsnprintf)
    // Note: maybe we can implement str_appendfmtv for non-libc and use that?
  #else
    char buf[256];
    va_list ap1;
    va_copy(ap1, ap);
    isize n = vsnprintf(buf, sizeof(buf), fmt, ap1);
    if (n < (isize)sizeof(buf))
      return b_diag(ctx, level, pos, buf);
    // buf too small; heap allocate
    char* buf2 = (char*)memalloc(ctx->mem, n + 1);
    n = vsnprintf(buf2, n + 1, fmt, ap);
    b_diag(ctx, level, pos, buf2);
    memfree(ctx->mem, buf2);
  #endif
}

void b_diagf(BuildCtx* ctx, DiagLevel level, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, level, pos, fmt, ap);
  va_end(ap);
}

void b_errf(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, DiagError, pos, fmt, ap);
  va_end(ap);
}

void b_warnf(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, DiagWarn, pos, fmt, ap);
  va_end(ap);
}

void b_notef(BuildCtx* ctx, PosSpan pos, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  b_diagv(ctx, DiagNote, pos, fmt, ap);
  va_end(ap);
}


void b_add_source(BuildCtx* b, Source* src) {
  if (b->srclist)
    src->next = b->srclist;
  b->srclist = src;
}

error b_add_file(BuildCtx* b, const char* filename) {
  Source* src = memalloct(b->mem, Source);
  error err = source_open_file(src, b->mem, filename);
  if (err) {
    memfree(b->mem, src);
    return err;
  }
  b_add_source(b, src);
  return 0;
}

error b_add_dir(BuildCtx* b, const char* filename) {
  FSDir d;
  error err = sys_dir_open(filename, &d);
  if (err)
    return err;

  FSDirEnt e;
  error err1;
  while ((err = sys_dir_read(d, &e)) > 0) {
    switch (e.type) {
      case FSDirEnt_REG:
      case FSDirEnt_LNK:
      case FSDirEnt_UNKNOWN:
        if (e.namlen < 4 || e.name[0] == '.' || strcmp(&e.name[e.namlen-2], ".co") != 0)
          break; // skip file
        if ((err = b_add_file(b, e.name)))
          goto end;
        break;
      default:
        break;
    }
  }
end:
  err1 = sys_dir_close(d);
  return err < 0 ? err : err1;
}


Node* nullable b_mknodex(BuildCtx* b, NodeKind kind) {
  Node* n = NodeAlloc(b->mem);
  if (LIKELY(n != NULL)) {
    memset(n, 0, sizeof(union NodeUnion));
    NodeInit(n, kind);
  }
  return n;
}


PkgNode* nullable b_mkpkgnode(BuildCtx* b, Scope* pkgscope) {
  auto pkg = b_mknode(b, Pkg);
  if (LIKELY(pkg != NULL)) {
    pkg->name = b->pkgid;
    pkg->scope = pkgscope;
  }
  return pkg;
}


Sym b_typeid_assign(BuildCtx* b, Type* t) {
  // Note: built-in types have predefined type ids (defined in universe)
  char buf[128];
  u32 len = typeid_make(buf, sizeof(buf), t);
  if (LIKELY( len < sizeof(buf) ))
    return t->tid = symget(b->syms, buf, len);
  // didn't fit in stack buffer; resort to heap allocation
  len++;
  char* s = memalloc(b->mem, len);
  if (!s) {
    b_errf(b, (PosSpan){0}, "out of memory");
    return kSym__;
  }
  len = typeid_make(s, len, t);
  t->tid = symget(b->syms, s, len);
  memfree(b->mem, s);
  return t->tid;
}

bool _b_typeeq(BuildCtx* b, Type* x, Type* y) {
  // invariant: x != y
  assertnotnull(x);
  assertnotnull(y);
  if (x->kind != y->kind)
    return false;
  if (is_BasicTypeNode(x)) // all BasicTypeNodes have precomputed type id
    return x->tid == y->tid;
  return b_typeid(b, x) == b_typeid(b, y);
}

#if 0
// TODO Type* InternASTType(Build* b, Type* t) {
  if (t->kind == NBasicType)
    return t;
  auto tid = GetTypeID(b, t);
  auto t2 = SymMapGet(&b->types, tid);
  if (t2)
    return t2;
  SymMapSet(&b->types, tid, t);
  return t;
}
#endif


// --- functions to aid unit tests

#if defined(CO_TEST) && !defined(CO_NO_LIBC)

BuildCtx* b_testctx_new() {
  Mem mem = mem_libc_allocator();

  SymPool* syms = memalloct(mem, SymPool);
  sympool_init(syms, universe_syms(), mem, NULL);

  Pkg* pkg = memalloct(mem, Pkg);
  pkg->dir = ".";

  BuildCtx* b = memalloct(mem, BuildCtx);
  b_init(b, mem, syms, pkg, NULL, NULL);

  return b;
}

void b_testctx_free(BuildCtx* b) {
  auto mem = b->mem;
  sympool_dispose(b->syms);
  memfree(mem, b->pkg);
  memfree(mem, b->syms);
  b_dispose(b);
  // MemLinearFree(mem);
}

#endif // defined(CO_TEST) && !defined(CO_NO_LIBC)
