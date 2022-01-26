#include <stdio.h>
#include <err.h>
#include <lualib.h>
#include <lauxlib.h>
// #include <lua.h>
// #include <luajit.h>

#include "parse/parse.h"


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
  universe_init();

  dlog("Total: %3lu B (Node: %lu B)", NODE_UNION_SIZE, sizeof(Node));
  dlog("  Stmt %3lu B", sizeof(Stmt));
  dlog("  Expr %3lu B", sizeof(Expr));
  dlog("  Type %3lu B", sizeof(Type));

  // select a memory allocator
  #ifdef CO_WITH_LIBC
    Mem mem = mem_libc_allocator();
  #else
    static u8 memv[4096*8];
    DEF_MEM_STACK_BUF_ALLOCATOR(mem, memv);
  #endif

  Type t = {0};
  t.irval = NULL;

  // TODO: simplify this by maybe making syms & pkg fields of BuildCtx, instead of
  // separately allocated data.

  // create a symbol pool to hold all known symbols (keywords and identifiers)
  SymPool syms = {0};
  sympool_init(&syms, universe_syms(), mem, NULL);

  // define what package we are parsing
  Pkg pkg = { .id = str_make_cstr(mem, "foo") };

  // add a source file to the package
  Source src1 = {0};
  const char* src_text = "fun hello() int\n  4 + 3\n";
  error err = source_open_data(&src1, mem, "input", src_text, strlen(src_text));
  if (err)
    panic("source_open_data: %s", error_str(err));
  pkg_add_source(&pkg, &src1);

  // compute and print source checksum
  source_checksum(&src1);
  print_src_checksum(mem, &src1);

  // create a build context
  BuildCtx build = {0};
  buildctx_init(&build, mem, &syms, &pkg, on_scan_diag, NULL);

  // scan all sources of the package
  Scanner scanner = {0};
  for (Source* src = build.pkg->srclist; src != NULL; src = src->next) {
    dlog("scan %s", src->filename->p);
    error err = scan_init(&scanner, &build, src, ParseFlagsDefault);
    if (err)
      panic("scan_init: %s", error_str(err));
    while (scan_next(&scanner) != TNone) {
      printf(">> %s\n", tokname(scanner.tok));
    }
  }

  // -- lua example --

  // create a new execution context
  lua_State* L = luaL_newstate();
  if (!L)
    panic("luaL_newstate");

  // load all standard libraries
  luaL_openlibs(L);

  // load script
  int status = luaL_loadfile(L, "cmd/zs/zs.lua");
  if (status != 0)
    panic("luaL_loadfile: %s", lua_tostring(L, -1));

  // run script
  printf("[evaluating Lua script cmd/zs/zs.lua]\n");
  int ret = lua_pcall(L, 0, 0, 0);
  if (ret != 0)
    panic("lua_pcall: %s", lua_tostring(L, -1));

  // close execution context
  lua_close(L);

  return 0;
}
