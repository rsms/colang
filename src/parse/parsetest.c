#include "parse.h"
#if defined(CO_TESTING_ENABLED) && !defined(CO_NO_LIBC)

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>


static bool has_cli_flag(const char** argv, const char* flag) {
  const char** argp = &argv[1];
  while (*argp) {
    if (strcmp(*argp, flag) == 0)
      return true;
    argp++;
  }
  return false;
}


int parse_test_main(int argc, const char** argv) {
  if (!has_cli_flag(argv, "-parsetest"))
    return 0;

  dlog("parsetest");

  return 0;
}

#endif // CO_TESTING_ENABLED
