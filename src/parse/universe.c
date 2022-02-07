#include "../coimpl.h"
#include "universe.h"
#include "universe_data.h"

// DEBUG_UNIVERSE_DUMP_SCOPE -- define to log universe_scope state
//#define DEBUG_UNIVERSE_DUMP_SCOPE

static struct {
  Scope        s;
  SymMapBucket bindings_storage[32];
} g_scope = {0};

static SymPool g_universe_syms = {0};


#ifdef DEBUG_UNIVERSE_DUMP_SCOPE
  static void symmap_iter(Sym key, void* valp, bool* stop, void* nullable ctx) {
    auto n = (const Node*)valp;
    log("  %.*s\t%p\t=> N%s\t%p", (int)symlen(key), key, key, nodename(n), n);
  }
#endif


static void universe_init_scope() {
  dlog("TODO");
  // HMap* h = map_make(&g_scope.s.bindings, HMap* nullable h, Mem, usize hint);

  // SymMapInit(
  //   &g_scope.s.bindings,
  //   g_scope.bindings_storage,
  //   countof(g_scope.bindings_storage),
  //   mem_nil_allocator());

  // #if !RUN_GENERATOR
  //   #define _(name, ...) \
  //     assert(SymMapSet(&g_scope.s.bindings, kSym_##name, (void**)&kType_##name) == 0);
  //   DEF_TYPE_CODES_BASIC_PUB(_)
  //   DEF_TYPE_CODES_BASIC(_)
  //   DEF_TYPE_CODES_PUB(_)
  //   #undef _
  //   #define _(name, ...) \
  //     assert(SymMapSet(&g_scope.s.bindings, kSym_##name, (void**)&kExpr_##name) == 0);
  //   DEF_CONST_NODES_PUB(_)
  //   #undef _
  // #endif

  // #ifdef DEBUG_UNIVERSE_DUMP_SCOPE
  //   log("[DEBUG_UNIVERSE_DUMP_SCOPE] universe_scope() %p, %u bindings:",
  //     universe_scope(), SymMapLen(&g_scope.s.bindings));
  //   SymMapIter(&g_scope.s.bindings, &symmap_iter, NULL);
  // #endif
}

// static void universe_init_scope() {
//   SymMapInit(
//     &g_scope.s.bindings,
//     g_scope.bindings_storage,
//     countof(g_scope.bindings_storage),
//     mem_nil_allocator());
//
//   #if !RUN_GENERATOR
//     #define _(name, ...) \
//       assert(SymMapSet(&g_scope.s.bindings, kSym_##name, (void**)&kType_##name) == 0);
//     DEF_TYPE_CODES_BASIC_PUB(_)
//     DEF_TYPE_CODES_BASIC(_)
//     DEF_TYPE_CODES_PUB(_)
//     #undef _
//     #define _(name, ...) \
//       assert(SymMapSet(&g_scope.s.bindings, kSym_##name, (void**)&kExpr_##name) == 0);
//     DEF_CONST_NODES_PUB(_)
//     #undef _
//   #endif
//
//   #ifdef DEBUG_UNIVERSE_DUMP_SCOPE
//     log("[DEBUG_UNIVERSE_DUMP_SCOPE] universe_scope() %p, %u bindings:",
//       universe_scope(), SymMapLen(&g_scope.s.bindings));
//     SymMapIter(&g_scope.s.bindings, &symmap_iter, NULL);
//   #endif
// }

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
