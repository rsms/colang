#include "colib.h"


static void check_str_result(
  const char* name, u32 ti,
  const char* input,
  const char* expected, Str* result, char* nullable retval)
{
  char* actual_cstr = assertnotnull(str_cstr(result));
  if (strcmp(expected, actual_cstr) != 0) {
    assertf(0, "tests[%u]\n%s(\"%s\") =>"
      "\n  expected \"%s\""
      "\n  got      \"%s\""
      "\n",
      ti, name, input, expected, actual_cstr);
  }
  if (strcmp(expected, retval) != 0) {
    assertf(0, "tests[%u]\n%s(\"%s\") =>"
      "\n  expected return value \"%s\""
      "\n  got                   \"%s\""
      "\n",
      ti, name, input, expected, retval);
  }
}

static void check_str_result2(
  const char* name, u32 ti,
  const char* in1, const char* in2,
  const char* expected, Str* result, char* nullable retval)
{
  char* actual_cstr = assertnotnull(str_cstr(result));
  if (strcmp(expected, actual_cstr) != 0) {
    assertf(0, "tests[%u]\n%s(\"%s\", \"%s\") =>"
      "\n  expected \"%s\""
      "\n  got      \"%s\""
      "\n",
      ti, name, in1, in2, expected, actual_cstr);
  }
  if (strcmp(expected, retval) != 0) {
    assertf(0, "tests[%u]\n%s(\"%s\", \"%s\") =>"
      "\n  expected return value \"%s\""
      "\n  got                   \"%s\""
      "\n",
      ti, name, in1, in2, expected, retval);
  }
}


DEF_TEST(path_clean) {
  char buf[64];
  struct { const char* input; const char* expected; } tests[] = {
    { "a/c",                "a/c" },
    { "a/c/",               "a/c" },
    { "/a/c",               "/a/c" },
    { "/a/c//",             "/a/c" },
    { "a//c",               "a/c" },
    { "a/c/.",              "a/c" },
    { "a/c/b/..",           "a/c" },
    { "/../a/c",            "/a/c" },
    { "/../a/b/../././/c",  "/a/c" },
    { "/../a/b/../../c",    "/c" },
    { "/../a/b/../../../c", "/c" },
    { "/../../../a",        "/a" },
    { "/",                  "/" },
    { "////",               "/" },
    { "",                   "." },
  };
  for (u32 i = 0; i < countof(tests); i++) {
    Str result = str_make(buf, sizeof(buf));
    char* res_cstr = path_clean(&result, tests[i].input, strlen(tests[i].input));
    assertf(res_cstr != NULL, "tests[%u] returned NULL", i);
    assertf(result.v == buf, "allocated unexpected memory");
    check_str_result("path_clean", i, tests[i].input, tests[i].expected, &result, res_cstr);
  }
}


DEF_TEST(path_dir) {
  char buf[64];
  struct { const char* input; const char* expected; } tests[] = {
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
  for (u32 i = 0; i < countof(tests); i++) {
    Str result = str_make(buf, sizeof(buf));
    char* res_cstr = path_dir(&result, tests[i].input, strlen(tests[i].input));
    assertf(res_cstr != NULL, "tests[%u] returned NULL", i);
    assertf(result.v == buf, "allocated unexpected memory");
    check_str_result("path_dir", i, tests[i].input, tests[i].expected, &result, res_cstr);
  }
}


DEF_TEST(path_base) {
  char buf[64];
  struct { const char* input; const char* expected; } tests[] = {
    { "/foo/bar/baz.x",  "baz.x"},
    { "/foo/bar/baz",    "baz"},
    { "/foo/bar/baz/",   "baz"},
    { "/foo/bar/baz///", "baz"},
    { "dev.txt",         "dev.txt"},
    { "../todo.txt",     "todo.txt"},
    { "..",              ".."},
    { ".",               "."},
    { "/",               "/"},
    { "////",            "/"},
    { "",                "."},
  };
  for (u32 i = 0; i < countof(tests); i++) {
    Str result = str_make(buf, sizeof(buf));
    char* res_cstr = path_base(&result, tests[i].input, strlen(tests[i].input));
    assertf(res_cstr != NULL, "tests[%u] returned NULL", i);
    assertf(result.v == buf, "allocated unexpected memory");
    check_str_result("path_base", i, tests[i].input, tests[i].expected, &result, res_cstr);
  }
}


DEF_TEST(path_join) {
  char buf[64];
  struct { const char* a; const char* b; const char* expected; } tests[] = {
    { "a",        "b/c",  "a/b/c" },
    { "a/b/",     "c",    "a/b/c" },
    { "a/b//",    "//c",  "a/b/c" },
    { "/a//b/",   "/c",   "/a/b/c" },
    { "/a/./b/",  "/c",   "/a/b/c" },
    { "/a/b//",   "/c/",  "/a/b/c" },
    { "",         "",     "" },
    { "a",        "",     "a" },
    { "",         "a",    "a" },
    { "/",        "",     "/" },
    { "",         "/",    "/" },
  };
  for (u32 i = 0; i < countof(tests); i++) {
    Str result = str_make(buf, sizeof(buf));
    char* res_cstr = path_join(&result, tests[i].a, tests[i].b);
    assertf(result.v == buf, "allocated unexpected memory");
    check_str_result2(
      "path_join", i, tests[i].a, tests[i].b, tests[i].expected, &result, res_cstr);
  }
}
