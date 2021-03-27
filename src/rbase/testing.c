#include "rbase.h"

static auto mode = (TestingMode)-1;

static bool strbool(const char* s) {
  return strcmp(s, "on") == 0
      || strcmp(s, "1") == 0
      || strcmp(s, "true") == 0
      || strcmp(s, "yes") == 0 ;
}

TestingMode testing_mode() {
  if (mode == (TestingMode)-1) {
    auto s = getenv(R_UNIT_TEST_ENV_NAME);
    mode = TestingNone;
    if (s) {
      mode = strbool(s) ? TestingOn :
             strcmp(s, "exclusive") == 0 ? TestingExclusive :
             TestingNone;
    }
  }
  return mode;
}
