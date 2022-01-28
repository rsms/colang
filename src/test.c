#if CO_TESTING_ENABLED
#include "coimpl.h"
#include "test.h"
#include "str.h"
#include "path.h"
#include "time.h"

#ifdef CO_WITH_LIBC
  #include <unistd.h>  // isatty
  #include <stdlib.h>  // getenv
#endif

static CoTesting* testlist_head = NULL;
static CoTesting* testlist_tail = NULL;

static bool stderr_isatty = false;
static long stderr_fpos = 0;
static const char* waitstyle = "";
static const char* okstyle = "";
static const char* failstyle = "";
static const char* dimstyle = "";
static const char* nostyle = "";


void co_test_add(CoTesting* t) {
  t->file = path_cwdrel(t->file);
  if (testlist_head == NULL) {
    testlist_head = t;
  } else {
    testlist_tail->_next = t;
  }
  testlist_tail = t;
}


static bool should_run_test(CoTesting* t, const char* nullable filter_prefix) {
  if (!filter_prefix)
    return true; // no filter
  usize len = strlen(filter_prefix);
  return (len <= strlen(t->name) && memcmp(filter_prefix, t->name, len) == 0);
}

static void print_status(CoTesting* t, bool done, const char* msg) {
  const char* marker_wait = "• ";
  const char* marker_ok   = "✓ ";
  const char* marker_fail = "✗ ";
  if (!stderr_isatty) {
    marker_wait = "";
    marker_ok   = "OK ";
    marker_fail = "FAIL ";
  }
  const char* status = done ? (t->failed ? marker_fail : marker_ok) : marker_wait;
  const char* style = done ? (t->failed ? failstyle : okstyle) : waitstyle;
  fprintf(stderr, "TEST %s%s%s%s %s%s:%d%s %s\n",
    style, status, t->name, nostyle,
    dimstyle, t->file, t->line, nostyle,
    msg);
}

static void testrun_start(CoTesting* t) {
  print_status(t, false, "...");
  if (stderr_isatty)
    stderr_fpos = ftell(stderr);
}

static void testrun_end(CoTesting* t, u64 startat) {
  auto timespent = nanotime() - startat;
  if (stderr_isatty) {
    long fpos = ftell(stderr);
    if (fpos == stderr_fpos) {
      // nothing has been printed since _testing_start_run; clear line
      // \33[A    = move to previous line
      // \33[2K\r = clear line
      fprintf(stderr, "\33[A\33[2K\r");
    }
  }
  char durbuf[16];
  fmtduration(durbuf, timespent);
  print_status(t, true, durbuf);
}


int co_test_main(int argc, const char** argv) {
  // usage: $0 [filter_prefix]
  // note: if env R_TEST_FILTER is set, it is used as the default value for argv[1]

  if (!testlist_head)
    return 0;

  const char* filter_prefix;

  #ifdef CO_WITH_LIBC
    stderr_isatty = isatty(2);
    if (stderr_isatty) {
      waitstyle  = "";
      okstyle    = "\e[1;32m"; // green
      failstyle  = "\e[1;31m"; // red
      dimstyle   = "\e[2m";
      nostyle    = "\e[0m";
    }
    filter_prefix = getenv("R_TEST_FILTER");
  #else
    filter_prefix = "";
  #endif

  if (argc > 1 && strlen(argv[1]) > 0)
    filter_prefix = argv[1];

  int failcount = 0;
  int runcount = 0;
  for (CoTesting* t = testlist_head; t; t = t->_next) {
    if (should_run_test(t, filter_prefix)) {
      u64 startat = nanotime();
      testrun_start(t);
      t->fn(t);
      testrun_end(t, startat);
      runcount++;
      if (t->failed)
        failcount++;
    }
  }

  if (runcount == 0) {
    assertnotnull(filter_prefix);
    fprintf(stderr, "no tests with prefix %s\n", filter_prefix);
    return 0;
  }

  const char* progname = path_base(argv[0]);

  if (failcount > 0) {
    fprintf(stderr, "%sFAILED:%s %s (%d)\n",
      failstyle, nostyle, progname, failcount);
    for (CoTesting* t = testlist_head; t; t = t->_next) {
      if (t->failed)
        fprintf(stderr, "  %s\tat %s:%d\n", t->name, t->file, t->line);
    }
  // } else {
  //   fprintf(stderr, "%sPASS:%s %s\n", okstyle, nostyle, progname);
  }

  return failcount > 0 ? 1 : 0;
}


#endif /* R_TEST_MAIN, CO_TESTING_ENABLED */
