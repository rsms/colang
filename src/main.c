#include <stdio.h>
#include <err.h>
#include <lualib.h>
#include <lauxlib.h>
// #include <lua.h>
// #include <luajit.h>

#include "coimpl.h"
#include "test.h"
#include "parse/parse.h"
#include "sys.h"


void cli_usage(const char* prog) {
  fprintf(stdout, "usage: %s <lua-file>\n", prog);
}

void on_scan_diag(Diagnostic* d, void* userdata) {
  assert(d->level == DiagError); // scanner only produces error diagnostics
  errlog("scan error: %s", d->message);
}

void print_src_checksum(Mem mem, const Source* src) {
  Str s = str_make_hex_lc(mem, src->sha256, sizeof(src->sha256));
  printf("%s %s\n", s->p, src->filename->p);
  str_free(s);
}

int main(int argc, const char** argv) {
  if (co_test_main(argc, argv)) return 1;
  universe_init();

  dlog("Total: %3lu B", sizeof(union NodeUnion));
  dlog("  Expr %3lu B", sizeof(Expr));
  dlog("  Type %3lu B", sizeof(Type));

  // select a memory allocator
  #ifdef CO_WITH_LIBC
    Mem mem = mem_libc_allocator();
  #else
    static u8 memv[4096*8];
    DEF_MEM_STACK_BUF_ALLOCATOR(mem, memv);
  #endif

  // TODO: simplify this by maybe making syms & pkg fields of BuildCtx, instead of
  // separately allocated data.

  // create a symbol pool to hold all known symbols (keywords and identifiers)
  SymPool syms = {0};
  sympool_init(&syms, universe_syms(), mem, NULL);

  // create a build context
  BuildCtx build = {0};
  BuildCtxInit(&build, mem, &syms, "foo", on_scan_diag, NULL);

  // add a source file to the package
  Source src1 = {0};
  const char* src_text = "fun hello() int\n  4 + 3\n";
  error err = source_open_data(&src1, mem, "input", src_text, strlen(src_text));
  if (err)
    panic("source_open_data: %s", error_str(err));
  b_add_source(&build, &src1);

  // compute and print source checksum
  source_checksum(&src1);
  print_src_checksum(mem, &src1);

  // scan all sources of the package
  Scanner scanner = {0};
  for (Source* src = build.srclist; src != NULL; src = src->next) {
    dlog("scan %s", src->filename->p);
    error err = ScannerInit(&scanner, &build, src, 0);
    if (err)
      panic("ScannerInit: %s", error_str(err));
    while (ScannerNext(&scanner) != TNone) {
      printf(">> %s\n", TokName(scanner.tok));
    }
  }

  // parse
  Parser p = {0};
  auto pkgscope = ScopeNew(mem, universe_scope());
  for (Source* src = build.srclist; src != NULL; src = src->next) {
    FileNode* result;
    err = parse_tu(&p, &build, src, 0, pkgscope, &result);
    if (err)
      panic("parse_tu: %s", error_str(err));
  }

  // -- lua example --

  // create a new execution context
  lua_State* L = luaL_newstate();
  if (!L)
    panic("luaL_newstate");

  // load all standard libraries
  luaL_openlibs(L);

  // load script
  const char* filename = "misc/zs.lua";
  int status = luaL_loadfile(L, filename);
  if (status != 0)
    panic("luaL_loadfile: %s", lua_tostring(L, -1));

  // run script
  printf("[evaluating Lua script %s]\n", filename);
  int ret = lua_pcall(L, 0, 0, 0);
  if (ret != 0)
    panic("lua_pcall: %s", lua_tostring(L, -1));

  // close execution context
  lua_close(L);

  return 0;
}
