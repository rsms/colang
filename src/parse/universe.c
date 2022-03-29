// language built-in symbols and AST nodes
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PARSE_UNIVERSE_IMPLEMENTATION
#endif
#include "ast.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

// DEF_CONST_NODES_PUB: predefined named constant AST Nodes, exported in universe_scope,
// included in universe_syms
//   const Sym sym_##name
//   Node*     kNode_##name
#define DEF_CONST_NODES_PUB(_) /* (name, AST_TYPE, typecode_suffix, structinit) */ \
  _( nil,   Nil,     nil,  "" ) \
  _( true,  BoolLit, bool, ".ival=1" ) \
  _( false, BoolLit, bool, ".ival=0" ) \
// end DEF_CONST_NODES_PUB

// DEF_SYMS_PUB: predefined additional symbols, included in universe_syms
//   const Sym sym_##name
#define DEF_SYMS_PUB(X) /* (name) */ \
  X( _ ) \
// end DEF_SYMS_PUB

// precompiled constants, defined in universe_data.h
extern const Sym kSym__;
extern const Sym kSym_nil;
extern Node* kNode_bad;
extern Type* kType_type;
extern Type* kType_bool;
extern Type* kType_i8;
extern Type* kType_u8;
extern Type* kType_i16;
extern Type* kType_u16;
extern Type* kType_i32;
extern Type* kType_u32;
extern Type* kType_i64;
extern Type* kType_u64;
extern Type* kType_f32;
extern Type* kType_f64;
extern Type* kType_int;
extern Type* kType_uint;
extern Type* kType_nil;
extern Type* kType_ideal;
extern Type* kType_str;
extern Type* kType_auto;
extern Expr* kExpr_nil;
extern Expr* kExpr_true;
extern Expr* kExpr_false;

void universe_init();
const Scope* universe_scope();
const SymPool* universe_syms();


//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PARSE_UNIVERSE_IMPLEMENTATION

#include "universe_data.h"

// DEBUG_UNIVERSE_DUMP_SCOPE -- define to log universe_scope state
//#define DEBUG_UNIVERSE_DUMP_SCOPE

static Scope   g_scope = {0};
static SymPool g_universe_syms = {0};


#ifdef DEBUG_UNIVERSE_DUMP_SCOPE
  static void symmap_iter(Sym key, void* valp, bool* stop, void* nullable ctx) {
    auto n = (const Node*)valp;
    log("  %.*s\t%p\t=> N%s\t%p", (int)symlen(key), key, key, nodename(n), n);
  }
#endif


static void universe_init_scope() {
#if !RUN_GENERATOR

  static void* g_scope_storage[2048/sizeof(void*)];
  // size = map_bucketsize(kSymMapType, kUniverseScopeLen*2, kFixBufAllocatorOverhead)

  usize memcap = sizeof(g_scope_storage) - MEM_BUFALLOC_OVERHEAD;
  Mem mem = mem_mkalloc_buf(g_scope_storage, sizeof(g_scope_storage));

  SymMap* h = symmap_init(&g_scope.bindings, mem, kUniverseScopeLen);
  h->hash0 = 0xfeedface;
  assertnotnull(h);
  void** vp;

  // note: kType_nil is not exported as it would shadow kExpr_nil
  #define _(name, ...) \
    if (kType_##name != kType_nil) { \
      /*dlog("set \"%s\"\t=> kType_%s (%p)", kSym_##name, #name, kType_##name);*/ \
      vp = symmap_assign(h, kSym_##name); \
      assertf(vp != NULL, "ran out of memory (memcap %zu)", memcap); \
      assertf(*vp == NULL, "duplicate universe symbol %s", #name); \
      *vp = kType_##name; \
    }
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  #undef _

  #define _(name, ...) \
    /*dlog("set \"%s\"\t=> kExpr_%s (%p)", kSym_##name, #name, kExpr_##name);*/ \
    vp = symmap_assign(h, kSym_##name); \
    assertf(vp != NULL, "ran out of memory (memcap %zu)", memcap); \
    assertf(*vp == NULL, "duplicate universe symbol %s", #name); \
    *vp = kExpr_##name;
  DEF_CONST_NODES_PUB(_)
  #undef _

  // dlog("appox  %4zu B", map_bucketsize(
  //   kSymMapType, kUniverseScopeLen*1.5, kFixBufAllocatorOverhead));
  // dlog("ma.len %4zu B", ma.len);

  #ifdef DEBUG_UNIVERSE_DUMP_SCOPE
    dlog("[DEBUG_UNIVERSE_DUMP_SCOPE] universe_scope() has %zu bindings:", map_len(h));
    HMapIter it = {0};
    map_iter_init(&it, kSymMapType, h);
    while (it.key) {
      dlog("  %-6s => %p", *(Sym*)it.key, it.val);
      map_iter_next(&it);
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

#endif // PARSE_UNIVERSE_IMPLEMENTATION
