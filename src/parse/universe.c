// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"
#include "universe_data.h"

// DEBUG_UNIVERSE_DUMP_SCOPE -- define to log universe_scope state
#define DEBUG_UNIVERSE_DUMP_SCOPE

static Scope   g_scope = {0};
static SymPool g_universe_syms = {0};
const FunNode* kBuiltin_to_rawptr;


static void add_global(Sym name, Node* n) {
  void** vp = symmap_assign(&g_scope.bindings, name);
  assertf(vp, "out of memory");
  assertf(*vp == NULL, "duplicate universe symbol %s", name);
  *vp = n;
}


static void add_rawptr_fun() {
  //
  // unsafe fun to_rawptr<T>(_ &[T]) rawptr
  //
  static const ArrayTypeNode u8array_type = {
    .kind = NArrayType,
    .tflags = TF_KindArray,
    .size = 0,
    .elem = (Type*)&_kType_u8,
  };
  static const RefTypeNode u8array_ref_type = {
    .kind = NRefType,
    .flags = NF_Const,
    .tflags = TF_KindPointer,
    .elem = (Type*)&u8array_type,
  };
  static const ParamNode param1 = {
    .kind=NParam, .name=kSym_a, .index=0, .type=(Type*)&u8array_ref_type,
  };
  // static const ParamNode param2 = {
  //   .kind=NParam, .name=kSym_b, .index=1, .type=(Type*)&_kType_int,
  // };
  static const ParamNode* paramsv[] = {
    &param1,
    // &param2,
  };
  static const Type* paramstypev[countof(paramsv)] = {
    (Type*)&u8array_ref_type,
    // (Type*)&_kType_int,
  };
  static const TupleTypeNode paramstype = {
    .kind = NTupleType,
    .tflags = TF_KindStruct,
    .a = {
      .v = (Type**)paramstypev,
      .len = countof(paramstypev),
      .cap = countof(paramstypev),
      .ext = true,
    },
  };
  static const TupleNode params = {
    .kind = NTuple,
    .type = (Type*)&paramstype,
    .a = {
      .v = (Expr**)paramsv,
      .len = countof(paramsv),
      .cap = countof(paramsv),
      .ext = true,
    },
  };
  static const FunTypeNode fntype = {
    .kind = NFunType,
    .tflags = TF_KindFunc,
    .tid = kSym__, // TODO FIXME generate type id for this function signature
    .params = (TupleNode*)&params,
    .result = (Type*)&_kType_rawptr,
  };
  static const FunNode fn = {
    .kind = NFun,
    .name = kSym_to_rawptr,
    .type = (Type*)&fntype,
    .params = (TupleNode*)&params,
    .result = (Type*)&_kType_rawptr,
  };
  kBuiltin_to_rawptr = &fn;
  add_global(kSym_to_rawptr, (Node*)&fn);

  // // xxx
  // static char tmpbuf[4096];
  // Str str = str_make(tmpbuf, sizeof(tmpbuf));
  // fmtast((const Node*)&fn, &str, 0);
  // dlog("%s", str_cstr(&str));
  // panic("TODO");
}


static void universe_init_scope() {
#if !RUN_GENERATOR

  static void* g_scope_storage[2048/sizeof(void*)];
  UNUSED usize memcap = sizeof(g_scope_storage) - MEM_BUFALLOC_OVERHEAD;
  Mem mem = mem_mkalloc_buf(g_scope_storage, sizeof(g_scope_storage));

  SymMap* m = symmap_init(&g_scope.bindings, mem, kUniverseScopeLen);
  assertnotnull(m);
  m->hash0 = 0xfeedface;

  // note: kType_nil is not exported as it would shadow kExpr_nil
  #define _(name, ...)  add_global(kSym_##name, (Node*)kType_##name);
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_PUB(_)
  #undef _

  #define _(name, ...)  add_global(kSym_##name, (Node*)kExpr_##name);
  DEF_CONST_NODES_PUB(_)
  #undef _

  add_rawptr_fun();

  #ifdef DEBUG_UNIVERSE_DUMP_SCOPE
    dlog("[DEBUG_UNIVERSE_DUMP_SCOPE] universe_scope() has %u bindings:", m->len);
    char tmpbuf[256];
    for (const PMapEnt* e = hmap_citstart(m); hmap_citnext(m, &e); ) {
      const Sym k = e->key;
      const Node* n = (const void*)e->value;
      log("%.*s\t%p\t=> N%s\t%p\t%s",
        (int)symlen(k), k, k, nodename(n), n, fmtnode(n, tmpbuf, sizeof(tmpbuf)));
    }
  #endif
#endif
}


void universe_init() {
  static bool init = false;
  if (init)
    return;
  init = true;

  // _symroot is defined by parse_universe_data.h
  sympool_init(&g_universe_syms, NULL, mem_mkalloc_null(), _symroot);
  universe_init_scope();
}

const Scope* universe_scope() {
  return &g_scope;
}

const SymPool* universe_syms() {
  return &g_universe_syms;
}
