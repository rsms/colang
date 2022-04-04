#include "colib.h"

DEF_TEST(path_dir) {
  char buf[64];

  struct { const char* input; const char* expected_output; } tests[] = {
    { "/foo/bar/baz.js",  "/foo/bar" },
    { "/foo/bar/baz",     "/foo/bar" },
    { "/foo/bar/baz/",    "/foo/bar/baz" },
    { "/extra//seps///",  "/extra//seps" },
    { "dev.txt",          "." },
    { "../todo.txt",      ".." },
    { "..",               "." },
    { ".",                "." },
    { "/",                "/" },
    { "",                 "." },
  };
  for (usize i = 0; i < countof(tests); i++) {
    usize n = path_dir(tests[i].input, buf, sizeof(buf));
    assert(n < sizeof(buf));
    assertcstreq(tests[i].expected_output, buf);
  }
}
