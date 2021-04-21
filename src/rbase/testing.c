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

u64 _testing_start_run(const char* testname, const char* filename) {
  if (!testing_on())
    return 0;
  if (filter_prefix &&
      ( filter_prefix_len > strlen(testname) ||
        memcmp(filter_prefix, testname, filter_prefix_len) != 0) )
  {
    return 0;
  }
  fprintf(stderr, "TEST %s %s\n", testname, filename);
  return MAX(1, nanotime());
}

void _testing_end_run(const char* testname, u64 starttime) {
  auto timespent = nanotime() - starttime;
  char buf[128];
  fmtduration(buf, sizeof(buf), timespent);
  fprintf(stderr, "TEST %s \e[1;32mOK\e[0m (%s)\n", testname, buf);
}
