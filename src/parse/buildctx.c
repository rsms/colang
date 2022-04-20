// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

#ifndef CO_NO_LIBC
  #include <stdio.h> // vsnprintf
#endif

error BuildCtxInit(
  BuildCtx*             b,
  Mem                   mem,
  const char*           pkgid,
  DiagHandler* nullable diagh,
  void*                 userdata)
{
  assert(strlen(pkgid) > 0);
  bool recycle = b->pkg.a.v != NULL;

  b->opt       = OptNone;
  b->safe      = true;
  b->debug     = false;
  b->mem       = mem;
  b->srclist   = NULL;
  b->diagh     = diagh;
  b->userdata  = userdata;
  b->diaglevel = DiagMAX;
  b->errcount  = 0;

  Type* si;
  Type* ui;
  if (sizeof(long) <= 1)       { si = kType_i8;   ui = kType_u8; }
  else if (sizeof(long) == 2)  { si = kType_i16;  ui = kType_u16; }
  else if (sizeof(long) <= 4)  { si = kType_i32;  ui = kType_u32; }
  else if (sizeof(long) <= 8)  { si = kType_i64;  ui = kType_u64; }
  else if (sizeof(long) <= 16) { si = kType_i128; ui = kType_u128; }
  b->sint_type = as_BasicTypeNode(si);
  b->uint_type = as_BasicTypeNode(ui);

  if (recycle) {
    symmap_clear(&b->types);
    array_clear(&b->diagarray);
    posmap_clear(&b->posmap);
    array_clear(&b->pkg.a);
    // note: leaving b->syms as-is
    for (NodeSlab* s = &b->nodeslab_head; s; s = s->next) {
      s->len = 0;
      memset(s->data, 0, sizeof(s->data));
    }
    b->pkg.a.len = 0;
  } else {
    if UNLIKELY(symmap_init(&b->types, mem, 1) == NULL)
      return err_nomem;
    sympool_init(&b->syms, universe_syms(), mem, NULL);
    array_init(&b->diagarray, NULL, 0);
    posmap_init(&b->posmap);
    b->pkg.kind = NPkg;
    array_init(&b->pkg.a, NULL, 0);
  }

  if (!ScopeInit(&b->pkgscope, mem, universe_scope()))
    return err_nomem;

  b->pkgid     = symget(&b->syms, pkgid, strlen(pkgid)); // note: panics on nomem
  b->pkg.name  = b->pkgid;
  b->pkg.scope = &b->pkgscope;

  b->nodeslab_curr = &b->nodeslab_head;

  return 0;
}

void BuildCtxDispose(BuildCtx* ctx) {
  array_free(&ctx->diagarray);
  symmap_free(&ctx->types);
  posmap_dispose(&ctx->posmap);
  #if DEBUG
  memset(ctx, 0, sizeof(BuildCtx));
  #endif
}

static Diagnostic* b_mkdiag(BuildCtx* ctx) {
  Diagnostic* d = memalloct(Diagnostic);
  d->build = ctx;
  array_push(&ctx->diagarray, d);
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
    if LIKELY(n < (isize)sizeof(buf))
      return b_diag(ctx, level, pos, buf);
    // buf too small; heap allocate
    char* buf2 = (char*)mem_alloc(ctx->mem, n + 1);
    n = vsnprintf(buf2, n + 1, fmt, ap);
    b_diag(ctx, level, pos, buf2);
    mem_free(ctx->mem, buf2, n + 1);
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

Node* b_err_nomem(BuildCtx* b, PosSpan ps) {
  b_errf(b, ps, "out of memory");
  return (Node*)&b->tmpnode;
}


void b_add_source(BuildCtx* b, Source* src) {
  if (b->srclist)
    src->next = b->srclist;
  b->srclist = src;
}

error b_add_file(BuildCtx* b, const char* filename) {
  Source* src = mem_alloczt(b->mem, Source);
  error err = source_open_file(src, filename);
  if (err) {
    mem_free(b->mem, src, sizeof(Source));
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


#if rstrstrstrstrstt
gdata* d = GSLAB_ALLOC(g, datavhead, datavcurr);
#define GSLAB_ALLOC(g, HEADFIELD, CURRFIELD, ERRRET...) ({                          \
  if UNLIKELY(g->CURRFIELD->len == countof(g->HEADFIELD.data)) {                    \
    if (g->CURRFIELD->next) {                                                       \
      g->CURRFIELD = g->CURRFIELD->next;                                            \
      assert(g->CURRFIELD->len == 0);                                               \
    } else {                                                                        \
      __typeof__(g->CURRFIELD) tmp__ = rmem_alloc(g->a->mem, sizeof(g->HEADFIELD)); \
      if (check_alloc(g, tmp__))                                                    \
        return ERRRET;                                                              \
      tmp__->len = 0;                                                               \
      tmp__->next = g->CURRFIELD;                                                   \
      g->CURRFIELD = tmp__;                                                         \
    }                                                                               \
  }                                                                                 \
  &g->CURRFIELD->data[g->CURRFIELD->len++];                                         \
})
#endif


static NodeSlab* nullable nodeslab_grow(BuildCtx* b) {
  if (b->nodeslab_curr->next) {
    // use recycled slab
    b->nodeslab_curr = b->nodeslab_curr->next;
    assert(b->nodeslab_curr->len == 0);
    return b->nodeslab_curr;
  }
  NodeSlab* newslab = mem_alloc(b->mem, sizeof(NodeSlab));
  if UNLIKELY(!newslab)
    return NULL;
  memset(newslab, 0, sizeof(NodeSlab));
  newslab->next = b->nodeslab_curr;
  b->nodeslab_curr = newslab;
  return newslab;
}


static Node* nodeslab_alloc(BuildCtx* b, usize ptr_count) {
  NodeSlab* slab = b->nodeslab_curr;
  usize avail = countof(b->nodeslab_head.data) - slab->len;
  if (UNLIKELY(avail < ptr_count) && (slab = nodeslab_grow(b)) == NULL)
    return b_err_nomem(b, NoPosSpan);
  void* p = &slab->data[slab->len];
  slab->len += ptr_count;
  return p;
}


Node* _b_mknode(BuildCtx* b, NodeKind kind, Pos pos, usize nptrs) {
  safecheckf(nptrs < USIZE_MAX/sizeof(void*), "overflow");
  Node* n = nodeslab_alloc(b, nptrs);
  n->pos = pos;
  n->kind = kind;
  return n;
}


Node* nullable _b_mknodev(BuildCtx* b, NodeKind kind, Pos pos, usize nptrs) {
  Node* n = _b_mknode(b, kind, pos, nptrs);
  return UNLIKELY(n == (void*)&b->tmpnode) ? NULL : n;
}


Node* nullable _b_mknode_array(
  BuildCtx* b, NodeKind kind, Pos pos,
  usize structsize, uintptr array_offs, usize elemsize, u32 cap)
{
  // note: structsize is aligned to sizeof(void*) already
  usize nbytes;
  if (check_mul_overflow(elemsize, (usize)cap, &nbytes) ||
      check_add_overflow(nbytes, structsize, &nbytes))
  {
    nbytes = USIZE_MAX;
    safecheckf(nbytes < USIZE_MAX, "overflow");
  }
  nbytes = ALIGN2(nbytes, sizeof(void*));
  Node* n = nodeslab_alloc(b, nbytes / sizeof(void*));
  if UNLIKELY(n == (void*)&b->tmpnode)
    return NULL;

  n->kind = kind;
  n->pos = pos;

  cap = (u32)((nbytes - structsize) / elemsize);
  if (cap) {
    void* p = n;
    VoidArray* a = p + array_offs;
    a->v = p + structsize;
    a->cap = (u32)cap;
    a->ext = true;
  }

  return n;
}


Node* _b_copy_node(BuildCtx* b, const Node* src) {
  assert(src->kind < countof(kNodeStructSizeTab));

  usize structsize = kNodeStructSizeTab[src->kind];
  usize elemsize;
  usize arrayoffs = 0;
  usize arraysize = 0;

  #define ARRAY(ARRAY_FIELD) { \
    arrayoffs = (usize)offsetof(__typeof__(*n), ARRAY_FIELD); \
    elemsize = sizeof(n->ARRAY_FIELD.v[0]); \
    arraysize = (usize)n->ARRAY_FIELD.cap * elemsize; \
  }

  const Node* np = src;
  switch ((enum NodeKind)np->kind) { case NBad: {
    GNCASE(CUnit)           ARRAY(a);
    GNCASE(ListExpr)        ARRAY(a);
    NCASE(Fun)              ARRAY(params);
    NCASE(Call)             ARRAY(args);
    NCASE(Template)         ARRAY(params);
    NCASE(TemplateInstance) ARRAY(args);
    NCASE(Selector)         ARRAY(indices);
    NCASE(TupleType)        ARRAY(a);
    NCASE(StructType)       ARRAY(fields);

    // other nodes don't have any array field
    NCASE(AliasType)
    NCASE(ArrayType)
    NCASE(Assign)
    NCASE(BasicType)
    NCASE(BinOp)
    NCASE(BoolLit)
    NCASE(Comment)
    NCASE(Const)
    NCASE(Field)
    NCASE(FloatLit)
    NCASE(FunType)
    NCASE(Id)
    NCASE(IdType)
    NCASE(If)
    NCASE(Index)
    NCASE(IntLit)
    NCASE(NamedArg)
    NCASE(Nil)
    NCASE(Param)
    NCASE(PostfixOp)
    NCASE(PrefixOp)
    NCASE(Ref)
    NCASE(RefType)
    NCASE(Return)
    NCASE(Slice)
    NCASE(StrLit)
    NCASE(TemplateParam)
    NCASE(TemplateParamType)
    NCASE(TemplateType)
    NCASE(TypeCast)
    NCASE(TypeExpr)
    NCASE(TypeType)
    NCASE(Var)
  }}

  usize nptrs = _PTRCOUNT(structsize + arraysize);
  safecheckf(nptrs < USIZE_MAX/sizeof(void*), "overflow");
  Node* n = nodeslab_alloc(b, nptrs);

  memcpy(n, src, nptrs * sizeof(void*));

  if (arrayoffs == 0)
    return n;

  #if DEBUG
  const VoidArray* src_array = ((void*)src) + arrayoffs;
  #endif

  arraysize = (nptrs * sizeof(void*)) - structsize;

  void* p = n;
  VoidArray* a = p + arrayoffs;
  a->v = p + structsize;
  a->cap = (u32)(arraysize / elemsize);
  a->ext = true;

  assert(a->cap > 0);
  assert(a->cap >= src_array->cap);
  assert(a->len == src_array->len);

  // dlog("~~xx__xx~~"
  //   "\n  cap %u => %u"
  //   "\n  len %u => %u"
  //   "\n  ext %d => %d"
  //   ,src_array->cap, a->cap
  //   ,src_array->len, a->len
  //   ,src_array->ext, a->ext
  //   );

  return n;
}


void _b_free_node(BuildCtx* b, Node* n, usize ptr_count) {
  NodeSlab* slab = b->nodeslab_curr;
  // if n was the most recently allocated node, reclaim that space
  if (slab->len < ptr_count || (slab->data + slab->len - ptr_count) != (void*)n)
    return;
  slab->len -= ptr_count;
}


PkgNode* b_mkpkgnode(BuildCtx* b, Scope* pkgscope) {
  auto pkg = b_mknode(b, Pkg, 0);
  pkg->name = b->pkgid;
  pkg->scope = pkgscope;
  return pkg;
}


Sym b_typeid_assign(BuildCtx* b, Type* t) {
  // Note: built-in types have predefined type ids (defined in universe)
  char buf[128];
  Str s = str_make(buf, sizeof(buf));
  if UNLIKELY(!typeid_append(&s, t)) {
    b_err_nomem(b, NoPosSpan);
    return kSym__;
  }
  t->tid = symget(&b->syms, s.v, s.len);
  str_free(&s);
  return t->tid;
}


bool _b_typeeq(BuildCtx* b, Type* x, Type* y) {
  // invariant: x != y
  assertnotnull(x);
  assertnotnull(y);
  x = unbox_id_type(x);
  y = unbox_id_type(y);
  if (x->kind != y->kind)
    return false;
  if (is_BasicTypeNode(x)) // all BasicTypeNodes have precomputed type id
    return x->tid == y->tid;
  return b_typeid(b, x) == b_typeid(b, y);
}


bool _b_typelteq(BuildCtx* b, Type* dst, Type* src) {
  dst = unbox_id_type(dst);
  src = unbox_id_type(src);

  NodeKind k = dst->kind;
  if (k != src->kind)
    return false;

  // &[T] <= &[T N]
  if (k == NRefType) {
    ArrayTypeNode* larray = (ArrayTypeNode*)((RefTypeNode*)dst)->elem;
    ArrayTypeNode* rarray = (ArrayTypeNode*)((RefTypeNode*)src)->elem;
    if (larray->kind == NArrayType) {
      return (
        (rarray->kind == NArrayType) &&
        ( b_typeeq(b, larray->elem, rarray->elem) &
          (larray->size == 0) & (larray->sizeexpr == NULL) )
      );
    }
  }

  return b_typeeq(b, dst, src);
}


bool _b_intern_type(BuildCtx* b, Type** tp) {
  if ((*tp)->kind == NBasicType)
    return false;
  auto tid = b_typeid(b, *tp);
  void** vp = symmap_assign(&b->types, tid);
  if UNLIKELY(vp == NULL) {
    b_err_nomem(b, NoPosSpan);
    return false;
  }
  if (*vp) {
    // use existing
    *tp = *vp;
    return true;
  }
  // add to intern map
  *vp = *tp;
  return false;
}

//———————————————————————————————————————————————————————————————————————————————————————

static const char* const _DiagLevelName[DiagMAX + 1] = {
  "error",
  "warn",
  "note",
};

const char* DiagLevelName(DiagLevel l) {
  return _DiagLevelName[MAX(0, MIN(l, DiagMAX))];
}

bool diag_fmt(const Diagnostic* d, Str* dst) {
  assert(d->level <= DiagMAX);
  return pos_fmt(&d->build->posmap, d->pos, dst,
    "%s: %s", DiagLevelName(d->level), d->message);
}

void diag_free(Diagnostic* d) {
  assert(d->build != NULL);
  memfree((void*)d->message, strlen(d->message) + 1);
  memfree(d, sizeof(Diagnostic));
}


// --- functions to aid unit tests
// #if CO_TESTING_ENABLED && !defined(CO_NO_LIBC)
// BuildCtx* b_testctx_new() {
//   Mem mem = mem_libc_allocator();
//   SymPool* syms = memalloct(mem, SymPool);
//   sympool_init(syms, universe_syms(), mem, NULL);
//   Pkg* pkg = memalloct(mem, Pkg);
//   pkg->dir = ".";
//   BuildCtx* b = memalloct(mem, BuildCtx);
//   b_init(b, mem, syms, pkg, NULL, NULL);
//   return b;
// }
// void b_testctx_free(BuildCtx* b) {
//   auto mem = b->mem;
//   sympool_dispose(b->syms);
//   memfree(mem, b->pkg);
//   memfree(mem, b->syms);
//   b_dispose(b);
//   // MemLinearFree(mem);
// }
// #endif // CO_TESTING_ENABLED && !defined(CO_NO_LIBC)
