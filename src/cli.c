#include "colib.h"

#ifndef CO_NO_LIBC
  #include <sys/ioctl.h>
  #include <unistd.h> // isatty
  #define cli_logf(fmt, args...) fprintf(g_cli_stderr, fmt "\n", ##args)
#else
  #define cli_logf log
#endif

#if CO_TESTING_ENABLED && !defined(CO_NO_LIBC)
  FILE* _g_cli_stderr = NULL;
  #define g_cli_stderr (_g_cli_stderr ? _g_cli_stderr : stderr)
#else
  #define g_cli_stderr stderr
#endif


static CliParseStatus fail_nomem(const char* prog) {
  cli_logf("%s: out of memory", prog);
  return CLI_PS_NOMEM;
}


CliOption* nullable cliopt_find(
  CliOption* opts, const char* name, usize namelen, bool consider_short)
{
  // search long names
  for (usize i = 0; opts[i].type; i++) {
    const char* optname = opts[i].longname;
    if (strlen(optname) == namelen && memcmp(name, optname, namelen) == 0)
      return &opts[i];
  }
  if (!consider_short || namelen != 1)
    return NULL;
  // search short names
  for (usize i = 0; opts[i].type; i++) {
    if (opts[i].shortname == name[0])
      return &opts[i];
  }
  return NULL;
}


bool cliopt_booln(CliOption* opts, const char* name, usize namelen) {
  CliOption* opt = cliopt_find(opts, name, namelen, true);
  return opt && opt->type == CLI_T_BOOL && opt->boolval;
}

i64 cliopt_intn(CliOption* opts, const char* name, usize namelen) {
  CliOption* opt = cliopt_find(opts, name, namelen, true);
  return (opt && opt->type == CLI_T_INT) ? opt->intval : 0;
}

const char* nullable cliopt_strn(CliOption* opts, const char* name, usize namelen) {
  CliOption* opt = cliopt_find(opts, name, namelen, true);
  return (opt && opt->type == CLI_T_STR && opt->strval) ? opt->strval : NULL;
}



#ifndef CO_NO_LIBC

  typedef struct CliHelpCtx {
    CliOption*  opts;
    FILE*       fp;
    usize       column_limit;
    usize       usagelen_limit;
    usize       linelen_max;
    usize       usagelen_max;
    TStyles     styles;
    TStyleStack stylestack;
    char        tmpbuf[512];
  } CliHelpCtx;


  inline static usize printlen(const char* cstr) {
    return utf8_printlen((const u8*)cstr, strlen(cstr), UC_LFL_SKIP_ANSI);
  }


  static void fmt_arg(CliHelpCtx* hc, ABuf* s, CliOption* opt) {
    if (opt->type == CLI_T_BOOL)
      return;
    abuf_c(s, ' ');
    abuf_cstr(s, tstyle_str(hc->styles, TS_LIGHTGREEN));
    if (opt->valuename && opt->valuename[0]) {
      abuf_cstr(s, opt->valuename);
    } else switch (opt->type) {
      case CLI_T_STR: abuf_cstr(s, "<str>"); break;
      case CLI_T_INT: abuf_cstr(s, "<int>"); break;
    }
    abuf_cstr(s, tstyle_str(hc->styles, TS_DEFAULT_FG));
  }


  static usize fmt_opt_usage(CliHelpCtx* hc, char* buf, usize bufcap, CliOption* opt) {
    // e.g. -v, -verbose
    //      -o, -output <file>
    ABuf s_ = abuf_make(buf, bufcap); ABuf* s = &s_;
    bool has_longname = opt->longname[0] != 0;
    if (opt->shortname) {
      abuf_c(s, '-');
      abuf_c(s, opt->shortname);
      if (has_longname) {
        if (hc->styles != TStylesNone()) {
          abuf_cstr(s, "\x1B[2m,\x1B[22m ");
        } else {
          abuf_cstr(s, ", ");
        }
      }
    }
    if (has_longname) {
      abuf_c(s, '-');
      abuf_cstr(s, opt->longname);
    }
    fmt_arg(hc, s, opt);
    return abuf_terminate(s);
  }


  static usize fmt_opt_help(CliHelpCtx* hc, char* buf, usize bufcap, CliOption* opt) {
    // e.g. "Enable verbose logging"
    ABuf s_ = abuf_make(buf, bufcap); ABuf* s = &s_;
    if (opt->help && opt->help[0]) {
      abuf_cstr(s, opt->help);
    } else {
      abuf_cstr(s, "(No help information)");
    }
    return abuf_terminate(s);
  }


  static const usize kIndent = 2;


  static void print_option(CliHelpCtx* hc, CliOption* opt) {
    //char* buf = hc->tmpbuf + kIndent;
    //usize bufcap = sizeof(hc->tmpbuf) - kIndent;
    const char spaces[] = "                                                                ";

    // format & write usage
    fmt_opt_usage(hc, hc->tmpbuf, sizeof(hc->tmpbuf), opt);
    usize usagelen = printlen(hc->tmpbuf);
    fwrite(spaces, kIndent, 1, hc->fp);
    fwrite(hc->tmpbuf, strlen(hc->tmpbuf), 1, hc->fp);

    // format help
    fmt_opt_help(hc, hc->tmpbuf, sizeof(hc->tmpbuf), opt);
    usize help_indent = kIndent*2;

    // separate line for help if usage is long
    if (usagelen > hc->usagelen_limit || hc->column_limit < 40) {
      fputc('\n', hc->fp);
      fwrite(spaces, kIndent, 1, hc->fp);
    } else {
      if (usagelen < hc->usagelen_max) {
        // adjust to column for single-line
        usize nspaces = MIN(sizeof(spaces), hc->usagelen_max - usagelen);
        fwrite(spaces, nspaces, 1, hc->fp);
      }
      help_indent += hc->usagelen_max;
    }

    usize help_column_limit = (usize)MAX(((isize)hc->column_limit - (isize)help_indent),8);

    usize buflen = strlen(hc->tmpbuf);
    usize buf_prinlen = printlen(hc->tmpbuf);
    if (buf_prinlen > help_column_limit) {
      swrap_simple(hc->tmpbuf, buflen, help_column_limit);

      usize nspaces = MIN(sizeof(spaces), help_indent);
      fwrite(spaces, kIndent, 1, hc->fp);
      usize i = 0, lnstart = 0;

      for (; i < buflen; i++) {
        if (hc->tmpbuf[i] == '\n') {
          fwrite(hc->tmpbuf + lnstart, i - lnstart, 1, hc->fp);
          lnstart = ++i;
          fputc('\n', hc->fp);
          if (i == buflen)
            break;
          fwrite(spaces, nspaces, 1, hc->fp);
        }
      }
      fwrite(hc->tmpbuf + lnstart, i - lnstart, 1, hc->fp);
    } else {
      fwrite(spaces, kIndent, 1, hc->fp);
      fwrite(hc->tmpbuf, buflen, 1, hc->fp);
    }

    fputc('\n', hc->fp);
  }


  static void print_options(CliHelpCtx* hc) {
    fprintf(hc->fp, "options:\n");

    hc->usagelen_max = 0; // does not include indent
    hc->linelen_max = 0;
    u32 auto_help = 3; // 3: long & short, 2: long only, 1: short only, 0: none

    memset(hc->tmpbuf, ' ', kIndent);
    char* buf = hc->tmpbuf + kIndent;
    usize bufcap = sizeof(hc->tmpbuf) - kIndent;

    for (usize i = 0; hc->opts[i].type; i++) {
      if ((auto_help & 2) && strcmp(hc->opts[i].longname, "help") == 0)
        auto_help &= ~2;
      if ((auto_help & 1) && hc->opts[i].shortname == 'h')
        auto_help &= ~1;

      fmt_opt_usage(hc, buf, bufcap, &hc->opts[i]);
      usize len = printlen(buf);
      if (len <= hc->usagelen_limit + kIndent) {
        char tmpbuf[128];
        sfmt_repr(tmpbuf, sizeof(tmpbuf), buf, strlen(buf));
        hc->usagelen_max = MAX(hc->usagelen_max, len);
      }
      fmt_opt_help(hc, buf, bufcap, &hc->opts[i]);
      len += printlen(buf);
      hc->linelen_max = MAX(hc->linelen_max, len);
    }

    CliOption helpopt = {0};
    if (auto_help) {
      helpopt.longname = (auto_help & 2) ? "help" : "";
      helpopt.shortname = (auto_help & 1) ? 'h' : 0;
      helpopt.type = CLI_T_BOOL;
      helpopt.help = "Show help on stdout and exit";

      fmt_opt_usage(hc, buf, bufcap, &helpopt);
      usize len = printlen(buf);
      if (len <= hc->usagelen_limit + kIndent)
        hc->usagelen_max = MAX(hc->usagelen_max, len);
      fmt_opt_help(hc, buf, bufcap, &helpopt);
      len += printlen(buf);
      hc->linelen_max = MAX(hc->linelen_max, len);
    }

    for (usize i = 0; hc->opts[i].type; i++)
      print_option(hc, &hc->opts[i]);

    if (auto_help)
      print_option(hc, &helpopt);
  }


  void cliopt_help(
    CliOption* opts, const char* progname, bool accepts_args,
    const char* nullable usage, const char* nullable extra_help)
  {
    CliHelpCtx hc = {0};
    hc.opts = opts;
    hc.fp = g_cli_stderr;
    hc.styles = isatty(fileno(hc.fp)) ? TStylesForTerm() : TStylesNone();

    hc.column_limit = 80;
    #ifdef TIOCGWINSZ
      struct winsize wsize;
      if (hc.styles != TStylesNone() && ioctl(0, TIOCGWINSZ, &wsize) == 0 && wsize.ws_col)
        hc.column_limit = (usize)wsize.ws_col;
    #endif
    // use two lines for option which usage ("-foo <thing>") is longer than usagelen_limit
    hc.usagelen_limit = MAX(4, hc.column_limit / 3);

    usize usagelen = usage ? strlen(usage) : 0;
    if (usagelen) {
      fwrite(usage, usagelen, 1, hc.fp);
      fputc('\n', hc.fp);
    } else {
      const char* args_str = accepts_args ? " [args...]" : "";
      if (opts[0].type) {
        fprintf(hc.fp, "usage: %s [options]%s\n", progname, args_str);
      } else {
        fprintf(hc.fp, "usage: %s%s\n", progname, args_str);
      }
    }

    if (opts[0].type)
      print_options(&hc);

    if (extra_help) {
      usize len = strlen(extra_help);
      if (len) {
        fwrite(extra_help, len, 1, hc.fp);
        if (extra_help[len - 1] != '\n')
          fputc('\n', hc.fp);
      }
    }
  }

#endif // CO_NO_LIBC



static int parse_opt(
  CliOption* opts,
  int argc, const char** argv,
  CStrArray* nullable rest,
  const char* nullable usage,
  const char* nullable extra_help,
  int argi, const char* arg, CliParseStatus* stp)
{
  char tmpbuf[64];
  bool maybe_short = true;
  const char* name = &arg[1];
  if (arg[1] == '-') {
    maybe_short = false;
    name = &arg[2];
  }

  // find value after "="
  const char* value = NULL;
  usize namelen = strlen(name);
  isize eqi = sindexofn(name, namelen, '=');
  if (eqi != -1) {
    namelen = (usize)eqi;
    value = &name[eqi + 1];
  }
  int arglen = namelen + 1 + (arg[1] == '-'); // length of "-foo" in "-foo=bar"

  CliOption helpopt = {0};
  CliOption* opt = cliopt_find(opts, name, namelen, maybe_short);

  if (!opt) {
    // short with immediate value?
    if (maybe_short && name[1]) {
      opt = cliopt_find(opts, name, 1, maybe_short);
      if (opt && opt->type != CLI_T_BOOL)
        value = &name[1];
    }
    if (!opt) {
      // Did not find a short option that accepts immediate value
      if ((name[0] == 'h' && name[1] == 0) ||
          (namelen == 4 && memcmp(name, "help", 4) == 0))
      {
        // -h, -help, --help
        if (!value) {
          *stp = CLI_PS_HELP;
          cliopt_help(opts, argv[0], rest != NULL, usage, extra_help);
          return -1;
        }
        // trigger "-h option does not accept a value"
        helpopt.type = CLI_T_BOOL;
        opt = &helpopt;
      } else {
        sfmt_repr(tmpbuf, sizeof(tmpbuf), arg, (usize)arglen);
        cli_logf("%s: unrecognized option \"%s\"", argv[0], tmpbuf);
        goto badopt;
      }
    }
  }

  if (opt->type == CLI_T_BOOL) {
    if (value) { // e.g. -foo=on
      cli_logf("%s: %.*s option does not accept a value", argv[0], arglen, arg);
      goto badopt;
    }
    opt->boolval = true;
    if (opt->valuep)
      *(bool*)opt->valuep = true;
    return argi;
  }

  // want value

  if (value == NULL) {
    argi++;
    if (argc == argi) {
      cli_logf("%s: missing value for option %.*s", argv[0], arglen, arg);
      goto badopt;
    }
    value = argv[argi];
  }

  switch (opt->type) {

  case CLI_T_STR:
    opt->strval = value;
    if (opt->valuep)
      *(const char**)opt->valuep = value;
    return argi;

  case CLI_T_INT: {
    usize vallen = strlen(value);
    u32 base = 10;
    if (vallen > 2 && value[0] == '0') {
      switch (value[1]) {
        case 'X': case 'x': value += 2; base = 16; break;
        case 'O': case 'o': value += 2; base = 8; break;
        case 'B': case 'b': value += 2; base = 2; break;
      }
    }
    error err = sparse_i64(value, strlen(value), base, &opt->intval);
    if (err) {
      cli_logf("%s: invalid integer value for option %.*s", argv[0], arglen, arg);
      goto badopt;
    }
    if (opt->valuep)
      *(i64*)opt->valuep = opt->intval;
    return argi;
  }

  default:
    assertf(0,"invalid cli option type %u", opt->type);
    cli_logf("%s: failed to parse option %.*s", argv[0], arglen, arg);
  }
badopt:
  *stp = CLI_PS_BADOPT;
  if UNLIKELY(rest && !array_push(rest, arg)) {
    *stp = fail_nomem(argv[0]);
    return -1;
  }
  return argi;
}


CliParseStatus cliopt_parse(
  CliOption* opts,
  int argc, const char** argv,
  CStrArray* nullable rest,
  const char* nullable usage,
  const char* nullable extra_help)
{
  if (argc < 1) {
    cli_logf("?: empty command line");
    return CLI_PS_BADOPT;
  }
  CliParseStatus status = CLI_PS_OK;
  const char* prog = argv[0];

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    usize arglen = strlen(arg);

    if (arglen == 0 || *arg != '-' || arglen == 1 /* "-" argument */) {
      if UNLIKELY(!array_push(rest, arg))
        return fail_nomem(prog);
      continue;
    }

    if (arglen == 2 && arg[1] == '-') {
      // "--" end of options
      i++;
      if UNLIKELY(i < argc && !array_append(rest, argv + i, (u32)(argc - i)))
        return fail_nomem(prog);
      return status;
    }

    i = parse_opt(opts, argc, argv, rest, usage, extra_help, i, arg, &status);
    if (i < 0)
      return status;
  }

  return status;
}
