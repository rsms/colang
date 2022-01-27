#include "../coimpl.h"
#include "universe.h"

#include "universe_data.h"

static struct {
  Scope        s;
  SymMapBucket bindings_storage[32];
} g_scope = {0};

static SymPool g_universe_syms = {0};


static void universe_init_scope() {
  SymMapInit(
    &g_scope.s.bindings,
    g_scope.bindings_storage,
    countof(g_scope.bindings_storage),
    mem_nil_allocator());

  #ifndef RUN_GENERATOR
  #define _(name, ...) SymMapSet(&g_scope.s.bindings, kSym_##name, (void**)&kType_##name);
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  #undef _
  #define _(name, ...) SymMapSet(&g_scope.s.bindings, kSym_##name, (void**)&kExpr_##name);
  DEF_CONST_NODES_PUB(_)
  #undef _
  #endif
}

void universe_init() {
  static bool init = false;
  if (init)
    return;
  init = true;

  // _symroot is defined by parse_universe_data.h
  sympool_init(&g_universe_syms, NULL, mem_nil_allocator(), _symroot);
  universe_init_scope();
}

const Scope* universe_scope() {
  return &g_scope.s;
}

const SymPool* universe_syms() {
  return &g_universe_syms;
}
