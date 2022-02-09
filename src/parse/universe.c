#include "../coimpl.h"
#include "universe.h"
#include "universe_data.h"

// DEBUG_UNIVERSE_DUMP_SCOPE -- define to log universe_scope state
//#define DEBUG_UNIVERSE_DUMP_SCOPE

static struct {
  Scope s;
  u8    bindings_storage[592];
  // size from: map_bucketsize(kSymMapType, entries_count, kFixBufAllocatorOverhead)
} g_scope = {0};

static SymPool g_universe_syms = {0};


#ifdef DEBUG_UNIVERSE_DUMP_SCOPE
  static void symmap_iter(Sym key, void* valp, bool* stop, void* nullable ctx) {
    auto n = (const Node*)valp;
    log("  %.*s\t%p\t=> N%s\t%p", (int)symlen(key), key, key, nodename(n), n);
  }
#endif


static void universe_init_scope() {
#if !RUN_GENERATOR

  // count entries
  // TODO: do this in universe generator once
  usize count = 0;
  #define _(...) count++;
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  DEF_CONST_NODES_PUB(_)
  #undef _

  count--; // don't count kType_nil

  // TODO: look into creating the map at compile time or by the universe generator.
  // Maybe as simple as building the map and generating a corresponding byte array
  // for h->buckets (and state of h fields like count, flags, B and hash0.)

  FixBufAllocator ma;
  Mem mem = FixBufAllocatorInitz(
    &ma, g_scope.bindings_storage, sizeof(g_scope.bindings_storage));

  HMap* h = symmap_make(&g_scope.s.bindings, mem, count);
  void** vp;

  // note: kType_nil is not exported as it would shadow kExpr_nil
  #define _(name, ...) \
    if (kType_##name != kType_nil) { \
      /*dlog("set \"%s\"\t=> kType_%s (%p)", kSym_##name, #name, kType_##name);*/ \
      vp = assertnotnull(symmap_assign(h, kSym_##name, mem)); \
      assertf(*vp == NULL, "duplicate universe symbol %s", #name); \
      *vp = kType_##name; \
    }
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  #undef _

  #define _(name, ...) \
    /*dlog("set \"%s\"\t=> kExpr_%s (%p)", kSym_##name, #name, kExpr_##name);*/ \
    vp = assertnotnull(symmap_assign(h, kSym_##name, mem)); \
    assertf(*vp == NULL, "duplicate universe symbol %s", #name); \
    *vp = kExpr_##name;
  DEF_CONST_NODES_PUB(_)
  #undef _

  // TODO: run map_bucketsize in universe generator to define bindings_storage size
  //dlog("%zu B", map_bucketsize(kSymMapType, count, kFixBufAllocatorOverhead));
  //dlog("ma.len %zu", ma.len);

  // TODO: DEBUG_UNIVERSE_DUMP_SCOPE
  // #ifdef DEBUG_UNIVERSE_DUMP_SCOPE
  //   log("[DEBUG_UNIVERSE_DUMP_SCOPE] universe_scope() %p, %u bindings:",
  //     universe_scope(), SymMapLen(&g_scope.s.bindings));
  //   SymMapIter(&g_scope.s.bindings, &symmap_iter, NULL);
  // #endif
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
