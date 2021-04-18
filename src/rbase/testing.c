#include "rbase.h"

static int on = -1;
static const char* filter_prefix = NULL;
static size_t filter_prefix_len = 0;

bool testing_on() {
  if (on == -1) {
    auto s = getenv(R_UNIT_TEST_ENV_NAME);
    on = 0;
    if (s) {
      if (strcmp(s, "1") == 0) {
        on = 1;
      } else if (strcmp(s, "0") != 0) {
        on = 1;
        filter_prefix = s;
        filter_prefix_len = strlen(s);
      }
    }
  }
  return (bool)on;
}

bool testing_should_run(const char* testname) {
  if (!testing_on())
    return false;
  if (filter_prefix &&
      ( filter_prefix_len > strlen(testname) ||
        memcmp(filter_prefix, testname, filter_prefix_len) != 0) )
  {
    return false;
  }
  return true;
}
