#include "../common.h"
#include "sym.h"
#include "symmap.h"

// SymMap implementation
#define HASHMAP_NAME     SymMap
#define HASHMAP_KEY      Sym
#define HASHMAP_KEY_HASH symhash
#define HASHMAP_VALUE    void*
#include "../util/hashmap.c.h"
#undef HASHMAP_NAME
#undef HASHMAP_KEY
#undef HASHMAP_KEY_HASH
#undef HASHMAP_VALUE

#if R_TESTING_ENABLED

static void testMapIterator(Sym key, void* value, bool* stop, void* userdata) {
  // dlog("\"%s\" => %zu", key, (size_t)value);
  size_t* n = (size_t*)userdata;
  (*n)++;
}

R_TEST(symmap) {
  auto mem = MemArenaAlloc();
  auto m = SymMapNew(64, mem);
  SymPool syms;
  sympool_init(&syms, NULL, mem, NULL);

  assert(m->len == 0);

  #define SYM(cstr) symgetcstr(&syms, cstr)
  const void* oldval;

  oldval = SymMapSet(m, SYM("hello"), (void*)1);
  // dlog("SymMapSet(hello) => %zu", (size_t)oldval);
  assert(m->len == 1);

  oldval = SymMapSet(m, SYM("hello"), (void*)2);
  // dlog("SymMapSet(hello) => %zu", (size_t)oldval);
  assert(m->len == 1);

  assert(SymMapDel(m, SYM("hello")) == (void*)2);
  assert(m->len == 0);

  size_t n = 100;
  SymMapSet(m, SYM("break"),       (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("case"),        (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("const"),       (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("continue"),    (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("default"),     (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("defer"),       (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("else"),        (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("enum"),        (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("fallthrough"), (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("for"),         (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("fun"),         (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("go"),          (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("if"),          (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("import"),      (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("in"),          (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("interface"),   (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("is"),          (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("return"),      (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("select"),      (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("struct"),      (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("switch"),      (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("symbol"),      (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("type"),        (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("var"),         (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("while"),       (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("_"),           (void*)n++); assert(m->len == n - 100);
  SymMapSet(m, SYM("int"),         (void*)n++); assert(m->len == n - 100);

  n = 0;
  SymMapIter(m, testMapIterator, &n);
  assert(n == 27);

  n = 100;
  assert(SymMapGet(m, SYM("break"))       == (void*)n++);
  assert(SymMapGet(m, SYM("case"))        == (void*)n++);
  assert(SymMapGet(m, SYM("const"))       == (void*)n++);
  assert(SymMapGet(m, SYM("continue"))    == (void*)n++);
  assert(SymMapGet(m, SYM("default"))     == (void*)n++);
  assert(SymMapGet(m, SYM("defer"))       == (void*)n++);
  assert(SymMapGet(m, SYM("else"))        == (void*)n++);
  assert(SymMapGet(m, SYM("enum"))        == (void*)n++);
  assert(SymMapGet(m, SYM("fallthrough")) == (void*)n++);
  assert(SymMapGet(m, SYM("for"))         == (void*)n++);
  assert(SymMapGet(m, SYM("fun"))         == (void*)n++);
  assert(SymMapGet(m, SYM("go"))          == (void*)n++);
  assert(SymMapGet(m, SYM("if"))          == (void*)n++);
  assert(SymMapGet(m, SYM("import"))      == (void*)n++);
  assert(SymMapGet(m, SYM("in"))          == (void*)n++);
  assert(SymMapGet(m, SYM("interface"))   == (void*)n++);
  assert(SymMapGet(m, SYM("is"))          == (void*)n++);
  assert(SymMapGet(m, SYM("return"))      == (void*)n++);
  assert(SymMapGet(m, SYM("select"))      == (void*)n++);
  assert(SymMapGet(m, SYM("struct"))      == (void*)n++);
  assert(SymMapGet(m, SYM("switch"))      == (void*)n++);
  assert(SymMapGet(m, SYM("symbol"))      == (void*)n++);
  assert(SymMapGet(m, SYM("type"))        == (void*)n++);
  assert(SymMapGet(m, SYM("var"))         == (void*)n++);
  assert(SymMapGet(m, SYM("while"))       == (void*)n++);
  assert(SymMapGet(m, SYM("_"))           == (void*)n++);
  assert(SymMapGet(m, SYM("int"))         == (void*)n++);

  n = 200;
  SymMapSet(m, SYM("xbreak"),       (void*)n++);
  SymMapSet(m, SYM("xcase"),        (void*)n++);
  SymMapSet(m, SYM("xconst"),       (void*)n++);
  SymMapSet(m, SYM("xcontinue"),    (void*)n++);
  SymMapSet(m, SYM("xdefault"),     (void*)n++);
  SymMapSet(m, SYM("xdefer"),       (void*)n++);
  SymMapSet(m, SYM("xelse"),        (void*)n++);
  SymMapSet(m, SYM("xenum"),        (void*)n++);
  SymMapSet(m, SYM("xfallthrough"), (void*)n++);
  SymMapSet(m, SYM("xfor"),         (void*)n++);
  SymMapSet(m, SYM("xfun"),         (void*)n++);
  SymMapSet(m, SYM("xgo"),          (void*)n++);
  SymMapSet(m, SYM("xif"),          (void*)n++);
  SymMapSet(m, SYM("ximport"),      (void*)n++);
  SymMapSet(m, SYM("xin"),          (void*)n++);
  SymMapSet(m, SYM("xinterface"),   (void*)n++);
  SymMapSet(m, SYM("xis"),          (void*)n++);
  SymMapSet(m, SYM("xreturn"),      (void*)n++);
  SymMapSet(m, SYM("xselect"),      (void*)n++);
  SymMapSet(m, SYM("xstruct"),      (void*)n++);
  SymMapSet(m, SYM("xswitch"),      (void*)n++);
  SymMapSet(m, SYM("xsymbol"),      (void*)n++);
  SymMapSet(m, SYM("xtype"),        (void*)n++);
  SymMapSet(m, SYM("xvar"),         (void*)n++);
  SymMapSet(m, SYM("xwhile"),       (void*)n++);
  SymMapSet(m, SYM("x_"),           (void*)n++);
  SymMapSet(m, SYM("xint"),         (void*)n++);

  n = 200;
  assert(SymMapGet(m, SYM("xbreak"))       == (void*)n++);
  assert(SymMapGet(m, SYM("xcase"))        == (void*)n++);
  assert(SymMapGet(m, SYM("xconst"))       == (void*)n++);
  assert(SymMapGet(m, SYM("xcontinue"))    == (void*)n++);
  assert(SymMapGet(m, SYM("xdefault"))     == (void*)n++);
  assert(SymMapGet(m, SYM("xdefer"))       == (void*)n++);
  assert(SymMapGet(m, SYM("xelse"))        == (void*)n++);
  assert(SymMapGet(m, SYM("xenum"))        == (void*)n++);
  assert(SymMapGet(m, SYM("xfallthrough")) == (void*)n++);
  assert(SymMapGet(m, SYM("xfor"))         == (void*)n++);
  assert(SymMapGet(m, SYM("xfun"))         == (void*)n++);
  assert(SymMapGet(m, SYM("xgo"))          == (void*)n++);
  assert(SymMapGet(m, SYM("xif"))          == (void*)n++);
  assert(SymMapGet(m, SYM("ximport"))      == (void*)n++);
  assert(SymMapGet(m, SYM("xin"))          == (void*)n++);
  assert(SymMapGet(m, SYM("xinterface"))   == (void*)n++);
  assert(SymMapGet(m, SYM("xis"))          == (void*)n++);
  assert(SymMapGet(m, SYM("xreturn"))      == (void*)n++);
  assert(SymMapGet(m, SYM("xselect"))      == (void*)n++);
  assert(SymMapGet(m, SYM("xstruct"))      == (void*)n++);
  assert(SymMapGet(m, SYM("xswitch"))      == (void*)n++);
  assert(SymMapGet(m, SYM("xsymbol"))      == (void*)n++);
  assert(SymMapGet(m, SYM("xtype"))        == (void*)n++);
  assert(SymMapGet(m, SYM("xvar"))         == (void*)n++);
  assert(SymMapGet(m, SYM("xwhile"))       == (void*)n++);
  assert(SymMapGet(m, SYM("x_"))           == (void*)n++);
  assert(SymMapGet(m, SYM("xint"))         == (void*)n++);

  // del
  assert(SymMapSet(m, SYM("hello"), (void*)2) == NULL);
  assert(SymMapGet(m, SYM("hello")) == (void*)2);
  assert(SymMapDel(m, SYM("hello")) == (void*)2);
  assert(SymMapGet(m, SYM("hello")) == NULL);
  assert(SymMapSet(m, SYM("hello"), (void*)2) == NULL);
  assert(SymMapGet(m, SYM("hello")) == (void*)2);

  n = 100;
  assert(SymMapDel(m, SYM("break"))       == (void*)n++);
  assert(SymMapDel(m, SYM("case"))        == (void*)n++);
  assert(SymMapDel(m, SYM("const"))       == (void*)n++);
  assert(SymMapDel(m, SYM("continue"))    == (void*)n++);
  assert(SymMapDel(m, SYM("default"))     == (void*)n++);
  assert(SymMapDel(m, SYM("defer"))       == (void*)n++);
  assert(SymMapDel(m, SYM("else"))        == (void*)n++);
  assert(SymMapDel(m, SYM("enum"))        == (void*)n++);
  assert(SymMapDel(m, SYM("fallthrough")) == (void*)n++);
  assert(SymMapDel(m, SYM("for"))         == (void*)n++);
  assert(SymMapDel(m, SYM("fun"))         == (void*)n++);
  assert(SymMapDel(m, SYM("go"))          == (void*)n++);
  assert(SymMapDel(m, SYM("if"))          == (void*)n++);
  assert(SymMapDel(m, SYM("import"))      == (void*)n++);
  assert(SymMapDel(m, SYM("in"))          == (void*)n++);
  assert(SymMapDel(m, SYM("interface"))   == (void*)n++);
  assert(SymMapDel(m, SYM("is"))          == (void*)n++);
  assert(SymMapDel(m, SYM("return"))      == (void*)n++);
  assert(SymMapDel(m, SYM("select"))      == (void*)n++);
  assert(SymMapDel(m, SYM("struct"))      == (void*)n++);
  assert(SymMapDel(m, SYM("switch"))      == (void*)n++);
  assert(SymMapDel(m, SYM("symbol"))      == (void*)n++);
  assert(SymMapDel(m, SYM("type"))        == (void*)n++);
  assert(SymMapDel(m, SYM("var"))         == (void*)n++);
  assert(SymMapDel(m, SYM("while"))       == (void*)n++);
  assert(SymMapDel(m, SYM("_"))           == (void*)n++);
  assert(SymMapDel(m, SYM("int"))         == (void*)n++);

  assert(SymMapGet(m, SYM("break"))       == 0);
  assert(SymMapGet(m, SYM("case"))        == 0);
  assert(SymMapGet(m, SYM("const"))       == 0);
  assert(SymMapGet(m, SYM("continue"))    == 0);
  assert(SymMapGet(m, SYM("default"))     == 0);
  assert(SymMapGet(m, SYM("defer"))       == 0);
  assert(SymMapGet(m, SYM("else"))        == 0);
  assert(SymMapGet(m, SYM("enum"))        == 0);
  assert(SymMapGet(m, SYM("fallthrough")) == 0);
  assert(SymMapGet(m, SYM("for"))         == 0);
  assert(SymMapGet(m, SYM("fun"))         == 0);
  assert(SymMapGet(m, SYM("go"))          == 0);
  assert(SymMapGet(m, SYM("if"))          == 0);
  assert(SymMapGet(m, SYM("import"))      == 0);
  assert(SymMapGet(m, SYM("in"))          == 0);
  assert(SymMapGet(m, SYM("interface"))   == 0);
  assert(SymMapGet(m, SYM("is"))          == 0);
  assert(SymMapGet(m, SYM("return"))      == 0);
  assert(SymMapGet(m, SYM("select"))      == 0);
  assert(SymMapGet(m, SYM("struct"))      == 0);
  assert(SymMapGet(m, SYM("switch"))      == 0);
  assert(SymMapGet(m, SYM("symbol"))      == 0);
  assert(SymMapGet(m, SYM("type"))        == 0);
  assert(SymMapGet(m, SYM("var"))         == 0);
  assert(SymMapGet(m, SYM("while"))       == 0);
  assert(SymMapGet(m, SYM("_"))           == 0);
  assert(SymMapGet(m, SYM("int"))         == 0);

  SymMapFree(m);
  MemArenaFree(mem);
}

#endif /* R_TESTING_ENABLED */
