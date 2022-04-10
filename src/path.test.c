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
    Str result = str_make(buf, sizeof(buf));

    assertf( path_dir(&result, tests[i].input, strlen(tests[i].input)), "tests[%zu]", i);

    assert(result.v == buf);
    assertcstreq(tests[i].expected_output, str_cstr(&result));
  }
}


DEF_TEST(path_join) {
  char buf[64];
  struct { const char* a; const char* b; const char* expected_output; } tests[] = {
    { "a1",      "b/c",  "a1/b/c" },
    { "a2/b/",   "c",    "a2/b/c" },
    { "a3/b//",  "//c",  "a3/b/c" },
    { "/a4//b/", "/c",   "/a4//b/c" },
    { "/a5/b//", "/c/",  "/a5/b/c" },
    { "",        "",     "" },
    { "a6",      "",     "a6" },
    { "",        "a7",   "a7" },
    { "/",       "",     "/" },
    { "",        "/",    "" },
  };
  for (usize i = 0; i < countof(tests); i++) {
    Str result = str_make(buf, sizeof(buf));

    assertf( path_join(&result, tests[i].a, tests[i].b), "tests[%zu]", i);

    assert(result.v == buf);
    assertcstreq(tests[i].expected_output, str_cstr(&result));
  }
}
