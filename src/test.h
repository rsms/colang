// test -- lightweight unit testing
#pragma once
ASSUME_NONNULL_BEGIN

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

  // testing_main runs all test defined with DEF_TEST or manually added with testing_add_test.
  //   usage: $0 [filter_prefix]
  //   filter_prefix: If given, only run tests which name has this prefix.
  int co_test_main(int argc, const char** argv);

  // testing_add_test explicity adds a test
  void co_test_add(CoTesting*);

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

ASSUME_NONNULL_END
