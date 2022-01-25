#include "coimpl.h"
#include "coparse.h"

#include "parse_universe_data.h"

static struct {
  Scope        s;
  SymMapBucket bindings_storage[32];
} g_scope = {0};

static SymPool g_syms = {0};


static void universe_init_scope() {
  assert(g_scope.s.bindings.buckets == NULL);

  SymMapInit(
    &g_scope.s.bindings,
    g_scope.bindings_storage,
    countof(g_scope.bindings_storage),
    mem_nil_allocator());

  // #define X(name, ...) SymMapSet(&s->bindings, sym_##name, (void**)&Type_##name);
  // TYPE_SYMS(X)
  // #undef X

  // #define X(name, _typ, _val) SymMapSet(&s->bindings, sym_##name, (void**)&Const_##name);
  // PREDEFINED_CONSTANTS(X)
  // #undef X
}

void universe_init() {
  static bool init = false;
  if (init)
    return;
  init = true;

  // _symroot is defined by parse_universe_data.h
  sympool_init(&g_syms, NULL, mem_nil_allocator(), _symroot);
  universe_init_scope();
}

const Scope* universe_scope() {
  return &g_scope.s;
}

const SymPool* universe_syms() {
  return &g_syms;
}
