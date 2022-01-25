#include <stdio.h>
#include <lualib.h>
#include <lauxlib.h>

// Useful lua headers (not needed for this simple example)
// #include <lua.h>
// #include <luajit.h>

void cli_usage(const char* prog) {
  fprintf(stdout, "usage: %s <lua-file>\n", prog);
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    cli_usage(argv[0]);
    return 1;
  }

  // create a new execution context
  lua_State* L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "luaL_newstate failed\n");
    return 1;
  }

  // load all standard libraries
  luaL_openlibs(L);

  // load script
  int status = luaL_loadfile(L, argv[1]);
  if (status != 0) {
    fprintf(stderr, "error: %s\n", lua_tostring(L, -1));
    return 1;
  }

  // run script
  int ret = lua_pcall(L, 0, 0, 0);
  if (ret != 0) {
    fprintf(stderr, "error: %s\n", lua_tostring(L, -1)); // tell us what mistake we made
    return 1;
  }

  // close execution context
  lua_close(L);

  return 0;
}
