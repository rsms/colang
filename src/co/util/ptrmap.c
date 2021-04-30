#include <rbase/rbase.h>
#include "ptrmap.h"
#include <math.h> /* log2 */
#include <limits.h> /* *_MAX */

// ptrhash
#if 0
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
  #define XXH_INLINE_ALL
  #include <xxhash/xxhash.h>
  #pragma GCC diagnostic pop
  #if ((ULONG_MAX) > (UINT_MAX))
    // 64-bit address
    #define ptrhash(ptr) ((size_t)XXH3_64bits(&(ptr), 8))
  #else
    // 32-bit address
    #define ptrhash(ptr) ((size_t)XXH32(&(ptr), 4, 0))
  #endif
#else
  #define ptrhash(p) (size_t)((13*((uintptr_t)(p))) ^ (((uintptr_t)(p)) >> 15))
#endif

// hashmap implementation
#define HASHMAP_NAME     PtrMap
#define HASHMAP_KEY      const void*
#define HASHMAP_VALUE    void*
#define HASHMAP_KEY_HASH ptrhash
#include "hashmap.c.h"
#undef HASHMAP_NAME
#undef HASHMAP_KEY
#undef HASHMAP_VALUE
#undef HASHMAP_KEY_HASH


#if DEBUG
static void testMapIterator(const void* key, void* value, bool* stop, void* userdata) {
  // dlog("\"%s\" => %zu", key, (size_t)value);
  size_t* n = (size_t*)userdata;
  (*n)++;
}
#endif


R_UNIT_TEST(ptrmap) {
  auto mem = MemNew(0);
  auto m = PtrMapNew(8, mem);

  assert(m->len == 0);

  #define SYM(cstr) symgeth((const u8*)(cstr), strlen(cstr))
  void* oldval;

  oldval = PtrMapSet(m, "hello", (void*)1);
  // dlog("PtrMapSet(hello) => %zu", (size_t)oldval);
  assert(m->len == 1);

  oldval = PtrMapSet(m, "hello", (void*)2);
  // dlog("PtrMapSet(hello) => %zu", (size_t)oldval);
  assert(m->len == 1);

  assert(PtrMapDel(m, "hello") == (void*)2);
  assert(m->len == 0);

  size_t n = 100;
  PtrMapSet(m, "break",       (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "case",        (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "const",       (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "continue",    (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "default",     (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "defer",       (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "else",        (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "enum",        (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "fallthrough", (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "for",         (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "fun",         (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "go",          (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "if",          (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "import",      (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "in",          (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "interface",   (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "is",          (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "return",      (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "select",      (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "struct",      (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "switch",      (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "symbol",      (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "type",        (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "var",         (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "while",       (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "_",           (void*)n++); assert(m->len == n - 100);
  PtrMapSet(m, "int",         (void*)n++); assert(m->len == n - 100);

  // // print distribution of load on each bucket
  // printf("bucket,load\n");
  // u32* vals = hashmapDebugDistr(m);
  // for (u32 i = 0; i < m.cap; i++) {
  //   printf("%u,%u\n", i+1, vals[i]);
  // }
  // free(vals);

  // counts
  n = 0;
  PtrMapIter(m, testMapIterator, &n);
  assert(n == 27);

  // del
  assert(PtrMapSet(m, "hello", (void*)2) == NULL);
  assert(PtrMapGet(m, "hello") == (void*)2);
  assert(PtrMapDel(m, "hello") == (void*)2);
  assert(PtrMapGet(m, "hello") == NULL);
  assert(PtrMapSet(m, "hello", (void*)2) == NULL);
  assert(PtrMapGet(m, "hello") == (void*)2);

  PtrMapFree(m);
  MemFree(mem);
}
