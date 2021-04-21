#pragma once
ASSUME_NONNULL_BEGIN

// unit testing
//   W_TEST_BUILD is defined for the "test" target product (but not for "debug".)
//   R_UNIT_TEST_ENABLED is defined for "test" and "debug" targets (since DEBUG is.)
//   R_UNIT_TEST(name, body) defines a unit test to be run before main()
#ifndef R_UNIT_TEST_ENABLED
  #if DEBUG && !defined(NDEBUG)
    #define R_UNIT_TEST_ENABLED 1
  #else
    #define R_UNIT_TEST_ENABLED 1
  #endif
#endif
#if R_UNIT_TEST_ENABLED && defined(NDEBUG)
  #warning "R_UNIT_TEST_ENABLED is enabled while NDEBUG is defined; tests will likely fail"
#endif
#ifndef R_UNIT_TEST_ENV_NAME
  #define R_UNIT_TEST_ENV_NAME "R_UNIT_TEST"
#endif
#if R_UNIT_TEST_ENABLED
  #define R_UNIT_TEST(name, body) \
  __attribute__((constructor,used)) static void unittest_##name() {  \
    for (auto t = _testing_start_run(#name, __FILE__); t != 0 ;) {   \
      body                                                           \
      _testing_end_run(#name, t);                                    \
      break;                                                         \
    }                                                                \
    return;                                                          \
  }
#else
  #define R_UNIT_TEST(name, body)
#endif

// testing_on returns true if the environment variable R_UNIT_TEST_ENV_NAME is 1
// or a test name prefix.
bool testing_on();
u64 _testing_start_run(const char* testname, const char* filename);
void _testing_end_run(const char* testname, u64 starttime);

ASSUME_NONNULL_END
