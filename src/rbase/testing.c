#include "rbase.h"
#include <unistd.h>  // for isatty()

static int on = -1;
static const char* filter_prefix = NULL;
static size_t filter_prefix_len = 0;

// typedef struct Testing {
//   const char* testname;
//   const char* filename;
//   u64         starttime;
//   long        fpos;
// } Testing;

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

bool _testing_start_run(Testing* t) {
  if (!testing_on())
    return false;
  if (filter_prefix &&
      ( filter_prefix_len > strlen(t->name) ||
        memcmp(filter_prefix, t->name, filter_prefix_len) != 0) )
  {
    return false;
  }
  t->isatty = isatty(2);
  fprintf(stderr, "TEST   %s %s ...\n", t->name, t->file);
  if (t->isatty)
    t->fpos = ftell(stderr);
  t->startat = nanotime();
  return true;
}

void _testing_end_run(Testing* t) {
  auto timespent = nanotime() - t->startat;
  char durbuf[128];
  fmtduration(durbuf, sizeof(durbuf), timespent);

  if (t->isatty) {
    long fpos = ftell(stderr);
    if (fpos == t->fpos) {
      // nothing has been printed since _testing_start_run; clear line
      // \33[A    = move to previous line
      // \33[2K\r = clear line
      fprintf(stderr, "\33[A\33[2K\r");
    }
  }

  const char* greenstyle = "";
  const char* nostyle = "";
  if (t->isatty) {
    greenstyle = "\e[1;32m";
    nostyle = "\e[0m";
  }

  fprintf(stderr, "TEST ✓ %s%s%s %s (%s)\n", greenstyle, t->name, nostyle, t->file, durbuf);
}
