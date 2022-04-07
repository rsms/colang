// test -- lightweight unit testing
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef struct CoTesting CoTesting;

#ifdef CO_TESTING_ENABLED
  // make sure "#if CO_TESTING_ENABLED" works
  #undef CO_TESTING_ENABLED
  #define CO_TESTING_ENABLED 1

  #define DEF_TEST(NAME)                                                \
    static void NAME##_test(CoTesting*);                                \
    __attribute__((constructor,used)) static void NAME##_test_init() {  \
      static CoTesting t = { #NAME, __FILE__, __LINE__, &NAME##_test }; \
      co_test_add(&t);                                                  \
    }                                                                   \
    static void NAME##_test(CoTesting* unittest)

  // CoTesting holds information about a specific unit test. DEF_TEST creates it for you.
  // If you use testing_add_test you'll need to provide it yourself.
  typedef void(*CoTestingFunc)(CoTesting*);
  struct CoTesting {
    const char*   name;
    const char*   file;
    int           line;
    CoTestingFunc fn;
    bool          failed; // set this to true to signal failure
    CoTesting*    _next;
  };

  // co_test_runall runs all test defined with DEF_TEST (or manually added with
  // testing_add_test.)
  // If filter_prefix is set, only tests which name starts with that string are run.
  // If filter_prefix is NULL, the environment variable CO_TEST_FILTER is used if present.
  // Returns number of failed tests.
  usize co_test_runall(const char* nullable filter_prefix);

  // testing_add_test explicity adds a test
  void co_test_add(CoTesting*);

  // memory backed libc FILE
  #ifndef CO_NO_LIBC
    FILE* test_fmemopen(void* restrict buf, usize bufcap, const char* restrict mode);
    isize test_fmemclose(FILE* fp);
  #endif

#else
  #if __has_attribute(unused) && __has_attribute(pure)
    #define TEST_ATTRS __attribute__((unused,pure))
  #elif __has_attribute(unused)
    #define TEST_ATTRS __attribute__((unused))
  #elif __has_attribute(pure)
    #define TEST_ATTRS __attribute__((pure))
  #else
    #define TEST_ATTRS
  #endif
  #define DEF_TEST(NAME)                                    \
    _Pragma("GCC diagnostic ignored \"-Wunused-variable\"") \
    TEST_ATTRS static void NAME##_test(CoTesting* unittest)
  inline static int co_test_main(int argc, const char** argv) { return 0; }
#endif // CO_TESTING_ENABLED

END_INTERFACE
