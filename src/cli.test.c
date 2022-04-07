#include "colib.h"


extern FILE* _g_cli_stderr;


UNUSED static const char* status_name(CliParseStatus status) {
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
  if (_g_cli_stderr && _g_cli_stderr != stderr) {
    // rewind stderrbuf write offset
    fseek(_g_cli_stderr, 0, SEEK_SET);
  }
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

  // redirect stderr to memory buffer
  char stderrbuf[512];
  _g_cli_stderr = test_fmemopen(stderrbuf, sizeof(stderrbuf), "w");

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

  { //———— short immediate is NOT supported ————
    const char* argv[] = { "test", "-c3" };
    rest.len = 0;
    reset_test_options(options);
    status = cliopt_parse(options, countof(argv), argv, &rest, NULL, NULL);
    asserteq(CLI_PS_BADOPT, status);
    asserteq(rest.len, 1);
  }

  { //———— help ————
    const char* argv[] = { "test", "-h" };
    rest.len = 0;
    reset_test_options(options);

    const char* help_suffix = "Extra help\n";
    status = cliopt_parse(options, countof(argv), argv, &rest, NULL, help_suffix);

    isize n = ftell(_g_cli_stderr); fseek(_g_cli_stderr, 0, SEEK_SET);
    usize stderrbuflen = MIN((usize)MAX(n, 0), sizeof(stderrbuf) - 1);
    stderrbuf[stderrbuflen] = 0;

    // For now, just make sure help is reported correctly (status, prefix and suffix.)
    // Don't validate exact output as it may change.
    //dlog("help: %s", stderrbuf);
    asserteq(CLI_PS_HELP, status);

    const char* expect_help_prefix = "usage: test ";
    assertf(shasprefix(stderrbuf, stderrbuflen, expect_help_prefix),
      "expected prefix \"%s\", got \"%.*s\"",
      expect_help_prefix,
      (int)MIN(stderrbuflen, strlen(expect_help_prefix)), stderrbuf);

    assertf(shassuffix(stderrbuf, stderrbuflen, help_suffix),
      "expected suffix \"%s\", got \"%.*s\"",
      help_suffix,
      (int)MIN(stderrbuflen, strlen(help_suffix)),
      stderrbuf + MIN(stderrbuflen, (stderrbuflen - strlen(help_suffix))) );
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

  // close memory buffer and restore stderr
  fclose(_g_cli_stderr);
  _g_cli_stderr = NULL;
}
