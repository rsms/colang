#pragma once
ASSUME_NONNULL_BEGIN

// unit testing
//   W_TEST_BUILD is defined for the "test" target product (but not for "debug".)
//   R_UNIT_TEST_ENABLED is defined for "test" and "debug" targets (since DEBUG is.)
//   R_UNIT_TEST(name, body) defines a unit test to be run before main()
#ifndef R_UNIT_TEST_ENABLED
  #if DEBUG
    #define R_UNIT_TEST_ENABLED 1
  #else
    #define R_UNIT_TEST_ENABLED 1
  #endif
#endif
#ifndef R_UNIT_TEST_ENV_NAME
  #define R_UNIT_TEST_ENV_NAME "R_UNIT_TEST"
#endif
#if R_UNIT_TEST_ENABLED
  #define R_UNIT_TEST(name, body) \
  __attribute__((constructor)) static void unittest_##name() { \
    if (testing_mode() != TestingNone) {       \
      printf("TEST " #name " %s\n", __FILE__); \
      body                                     \
    }                                          \
  }
#else
  #define R_UNIT_TEST(name, body)
#endif
typedef enum TestingMode {
  TestingNone = 0,  // testing disabled
  TestingOn,        // testing enabled
  TestingExclusive, // only test; don't run main function
} TestingMode;

// testing_mode retrieves the effective TestingMode parsed from
// environment variable {value of R_UNIT_TEST_ENV_NAME}
TestingMode testing_mode();

ASSUME_NONNULL_END
