// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"

#ifndef CO_NO_LIBC
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


#ifndef CO_NO_LIBC
  FILE* test_fmemopen(void* restrict buf, usize bufcap, const char* restrict mode) {
    return assertnotnull(fmemopen(buf, bufcap, mode));
  }

  isize test_fmemclose(FILE* fp) {
    long fpi = ftell(fp);
    fclose(fp);
    return (isize)fpi;
  }
#endif // CO_NO_LIBC


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


static int test_sort_fn(const void* x, const void* y, void* nullable ctx) {
  const CoTesting* a = *(const void**)x;
  const CoTesting* b = *(const void**)y;
  int cmp = strcmp(a->file, b->file);
  return cmp == 0 ? (a->line - b->line) : cmp;
}


usize co_test_runall(const char* nullable filter_prefix) {
  usize failcount = 0;

  if (!testlist_head)
    goto done;

  if (filter_prefix == NULL) {
    #ifdef CO_NO_LIBC
      filter_prefix = "";
    #else
      stderr_isatty = isatty(2);
      if (stderr_isatty) {
        waitstyle  = "";
        okstyle    = "\e[1;32m"; // green
        failstyle  = "\e[1;31m"; // red
        dimstyle   = "\e[2m";
        nostyle    = "\e[0m";
      }
      filter_prefix = getenv("CO_TEST_FILTER");
    #endif
  }

  // Sort tests by source {file,line} to make execution order predictable.
  // Start by collecting enabled tests into an array:
  static CoTesting* tests[128];
  usize ntests = 0;
  for (CoTesting* t = testlist_head; t; t = t->_next) {
    if (!should_run_test(t, filter_prefix))
      continue;
    assertf(ntests < countof(tests), "wowza, that is a lot of tests (%zu)", ntests);
    tests[ntests++] = t;
  }
  if (ntests == 0) {
    if (filter_prefix && strlen(filter_prefix) > 0)
      fprintf(stderr, "no tests with prefix %s\n", filter_prefix);
    goto done;
  }
  xqsort(tests, ntests, sizeof(void*), &test_sort_fn, NULL);

  // run tests
  for (usize i = 0; i < ntests; i++) {
    CoTesting* t = tests[i];
    u64 startat = nanotime();
    testrun_start(t);
    t->fn(t);
    testrun_end(t, startat);
    if (t->failed)
      failcount++;
  }

  // report failures after all tests has finished running
  if (failcount) {
    fprintf(stderr, "%sFAILED:%s (%zu)\n", failstyle, nostyle, failcount);
    for (usize i = 0; i < ntests; i++) {
      CoTesting* t = tests[i];
      if (t->failed)
        fprintf(stderr, "  %s\tat %s:%d\n", t->name, t->file, t->line);
    }
  }

done:
  return failcount;
}

