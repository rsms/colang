#include "colib.h"


extern FILE* g_cli_help_fp;


static const char* status_name(CliParseStatus status) {
  switch ((enum CliParseStatus)status) {
    case CLI_PS_HELP:   return "HELP";
    case CLI_PS_OK:     return "OK";
    case CLI_PS_BADOPT: return "BADOPT";
    case CLI_PS_NOMEM:  return "NOMEM";
  }
  return "?";
}


#define GETOPTION(opts, name) \
  assertnotnull(cliopt_find(opts, (name), strlen(name), false))


static void reset_test_options(CliOption* opts) {
  for (usize i = 0; opts[i].type; i++)
    opts[i].intval = 0;
  GETOPTION(opts, "output")->strval = "a.out";
  GETOPTION(opts, "count")->intval = 3;
}


DEF_TEST(cliopt_parse) {
  bool foo = false;
  bool bar = false;
  const char* output = NULL;
  i64 count = 0;

  CliOption options[] = {
    // longname, shortname, valuename, type, help [, default value]
    {"foo",       'f', "",       CLI_T_BOOL, "Help for foo", &foo },
    {"bars",      'B', "",       CLI_T_BOOL, "Help for bar", &bar },
    {"output",    'o', "<file>", CLI_T_STR,  "Help for output", &output },
    {"append",     0 , "",       CLI_T_BOOL, "Help for append" },
    {"delete",     0 , "",       CLI_T_BOOL, "Help for delete" },
    {"verbose",    0 , "",       CLI_T_BOOL, "Help for verbose" },
    {"count",     'c', "",       CLI_T_INT,  "Help for count", &count },
    {"file",       0 , "",       CLI_T_BOOL, "Help for file" },
    {0},
  };

  CliParseStatus status;
  const char* restbuf[16];
  auto rest = array_make(CStrArray, restbuf, sizeof(restbuf));

  { //———— boolean option + stop parsing options after "--" ————
    const char* argv[] = {
      "test", "-foo",
      "--", // stop parsing options
      "-a", "A", "--b",
    };
    rest.len = 0;
    reset_test_options(options);
    status = cliopt_parse(options, countof(argv), argv, &rest, NULL, NULL);
    asserteq(CLI_PS_OK, status);
    asserteq(foo, true);
    asserteq(bar, false);
    asserteq(cliopt_bool(options, "foo"), true);
    asserteq(cliopt_bool(options, "bar"), false);
    asserteq(cliopt_bool(options, "not a valid option"), false);
    asserteq(rest.len, 3);
    assertcstreq(rest.v[0], "-a");
    assertcstreq(rest.v[1], "A");
    assertcstreq(rest.v[2], "--b");
  }

  { //———— options with values ————
    const char* argv[] = {
      "test",
      "-o", "A", "-output", "B",
      "-c", "12345", "-count", "0xdeadbeef",
      "C",
    };
    rest.len = 0;
    reset_test_options(options);
    status = cliopt_parse(options, countof(argv), argv, &rest, NULL, NULL);
    asserteq(CLI_PS_OK, status);
    assertnotnull(output);
    assertcstreq(output, "B");
    asserteq(rest.len, 1);
    assertcstreq(rest.v[0], "C");
    asserteq(count, 0xdeadbeef);
  }

  { //———— help ————
    const char* argv[] = { "test", "-h" };
    rest.len = 0;
    reset_test_options(options);

    char helpbuf[512];
    g_cli_help_fp = test_fmemopen(helpbuf, sizeof(helpbuf), "w");

    const char* help_suffix = "Extra help\n";
    status = cliopt_parse(options, countof(argv), argv, &rest, NULL, help_suffix);

    isize n = test_fmemclose(g_cli_help_fp); g_cli_help_fp = NULL;
    usize helpbuflen = MIN((usize)MAX(n, 0), sizeof(helpbuf) - 1);
    helpbuf[helpbuflen] = 0;

    // For now, just make sure help is reported correctly (status, prefix and suffix.)
    // Don't validate exact output as it may change.
    //dlog("cliopt_parse => %s", status_name(status));
    //dlog("help: %s", helpbuf);
    asserteq(CLI_PS_HELP, status);

    const char* expect_help_prefix = "usage: test ";
    assertf(shasprefix(helpbuf, helpbuflen, expect_help_prefix),
      "expected prefix \"%s\", got \"%.*s\"",
      expect_help_prefix,
      (int)MIN(helpbuflen, strlen(expect_help_prefix)), helpbuf);

    assertf(shassuffix(helpbuf, helpbuflen, help_suffix),
      "expected suffix \"%s\", got \"%.*s\"",
      help_suffix,
      (int)MIN(helpbuflen, strlen(help_suffix)),
      helpbuf + MIN(helpbuflen, (helpbuflen - strlen(help_suffix))) );
  }


  { //———— parse a mixture of options ————
    const char* argv[] = {
      "test",
      "-foo", "--foo",
      "-o", "A", "-output", "B", "--output", "C", "-output=D",
    };
    rest.len = 0;
    reset_test_options(options);
    status = cliopt_parse(options, countof(argv), argv, &rest, NULL, NULL);
    asserteq(CLI_PS_OK, status);
  }
}
