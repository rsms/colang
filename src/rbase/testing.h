#pragma once
ASSUME_NONNULL_BEGIN

// unit testing
//   R_UNIT_TEST_ENABLED    is defined when (true for "test" and "debug" targets)
//   R_UNIT_TEST(name)body  defines a unit test to be run before main()
//
// Example:
//
//   R_UNIT_TEST(foo) {
//     Foo f = {2};
//     asserteq(f.x == 2);
//   }
//
#if !defined(R_UNIT_TEST_ENABLED) && DEBUG && !defined(NDEBUG)
  #define R_UNIT_TEST_ENABLED 1
#endif

#if R_UNIT_TEST_ENABLED && defined(NDEBUG)
  #warning "R_UNIT_TEST_ENABLED is enabled while NDEBUG is defined; tests will likely fail"
#endif

#ifndef R_UNIT_TEST_ENV_NAME
  #define R_UNIT_TEST_ENV_NAME "R_UNIT_TEST"
#endif

#if R_UNIT_TEST_ENABLED

  #define R_UNIT_TEST(name)                                         \
    static void name##_test();                                      \
    __attribute__((constructor,used)) static void name##_test1() {  \
      Testing t = { #name, __FILE__, __LINE__ };                    \
      if (_testing_start_run(&t)) {                                 \
        name##_test();                                              \
        _testing_end_run(&t);                                       \
      }                                                             \
      return;                                                       \
    }                                                               \
    static void name##_test()

#else
  #define R_UNIT_TEST(name) \
    __attribute__((unused)) inline static void name##_test()
#endif

typedef struct Testing {
  const char* name;
  const char* file;
  int         line;
  u64         startat;
  long        fpos;
  bool        isatty;
} Testing;

// testing_on returns true if the environment variable R_UNIT_TEST_ENV_NAME is 1
// or a test name prefix.
bool testing_on();
bool _testing_start_run(Testing* t);
void _testing_end_run(Testing* t);

ASSUME_NONNULL_END
