#include "../coimpl.h"
#include "universe.h"
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
