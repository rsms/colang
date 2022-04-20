// co command-line program
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#ifndef CO_NO_LIBC

#include <stdio.h>
#include <getopt.h>

#include "colib.h"
#include "cli.h"
#include "parse/parse.h"

#ifdef WITH_LLVM
  #include "llvm/llvm.h"
#endif

static const char* progname = "co";
static char tmpbuf[4096];

const char* COROOT = "";
const char* COPATH = "";
const char* COCACHE = "";

static const char* cli_usage =
  "Co compiler\n"
  "Usage: co [options] [<srcfile> ...]\n"
  "       Build specific source files as one package\n"
  "Usage: co [options] <dir> ...\n"
  "       Build each directory as a package"
;
static CliOption cliopts[] = {
  // CliOption{longname, shortname, valuename, type, help [, default value]}
  // Keep most commonly used options at the top of the list.

  {"debug", 0, "", CLI_T_BOOL, "Build with debug features and minimum optimizations" },
  {"unsafe", 0, "", CLI_T_BOOL, "Build witout runtime safety checks" },
  {"small", 0, "", CLI_T_BOOL, "Bias optimizations toward minimizing code size" },
  {"output", 'o', "<file>", CLI_T_STR, "Write output to <file>" },
  {"pkgname", 0, "<name>", CLI_T_STR, "Use <name> for package", .strval="example" },
  {"output-asm", 0, "<file>", CLI_T_STR, "Write machine assembly to <file>" },
  {"output-ir", 0, "<file>", CLI_T_STR, "Write IR source to <file>" },
  {"opt", 'O', "<level>", CLI_T_STR, "Set specific optimization level (0-3, s)" },

  #if CO_TESTING_ENABLED
  {"test-only", 0, "", CLI_T_BOOL, "Exit after running unit tests" },
  #endif
  {0},
};
static CStrArray cli_args;


static void parse_cliopts(int argc, const char** argv) {
  progname = argv[0];
  static const char* cli_restbuf[16];
  cli_args = array_make(CStrArray, cli_restbuf, sizeof(cli_restbuf));
  switch (cliopt_parse(cliopts, argc, argv, &cli_args, cli_usage, NULL)) {
    case CLI_PS_OK:     break;
    case CLI_PS_HELP:   exit(0);
    case CLI_PS_BADOPT: exit(1);
    case CLI_PS_NOMEM:  exit(127);
  }
}


#define PANIC_ON_ERROR(expr) \
  ({ error err__ = (expr); \
     err__ != 0 ? panic(#expr ": %s", error_str(err__)) : ((void)0); })


#define die(fmt, args...) { log("%s: " fmt, progname, ##args); exit(1); }


// static void print_src_checksum(const Source* src) {
//   ABuf s = abuf_make(tmpbuf, sizeof(tmpbuf));
//   abuf_reprhex(&s, src->sha256, sizeof(src->sha256));
//   abuf_terminate(&s);
//   printf("%s %s\n", tmpbuf, src->filename);
// }

// static void scan_all(BuildCtx* build) {
//   Scanner scanner = {0};
//   for (Source* src = build->srclist; src != NULL; src = src->next) {
//     dlog("scan %s", src->filename);
//     error err = ScannerInit(&scanner, build, src, 0);
//     if (err)
//       panic("ScannerInit: %s", error_str(err));
//     while (ScannerNext(&scanner) != TNone) {
//       printf(">> %s\n", TokName(scanner.tok));
//     }
//   }
// }

static void on_diag(Diagnostic* d, void* userdata) {
  Str str = str_make(tmpbuf, sizeof(tmpbuf));
  assertf(diag_fmt(d, &str), "failed to allocate memory");
  fwrite(str.v, str.len, 1, stderr);
  str_free(&str);
}


static error begin_pkg(BuildCtx* build, const char* pkgid) {
  return BuildCtxInit(build, mem_ctx(), pkgid, on_diag, NULL);
}


static error parse_pkg(BuildCtx* build) {
  Parser p = {0};

  ParseFlags pflags = 0;
  if (build->opt > '0' && !build->debug) {
    // speed up & simplify AST at the cost of error message quality
    pflags |= ParseOpt;
  }

  #if DEBUG
  Str str = str_make(tmpbuf, sizeof(tmpbuf)); // for debug logging
  #endif

  for (Source* src = build->srclist; src != NULL; src = src->next) {
    auto t = logtime_start("parse");
    FileNode* filenode;
    error err = parse_tu(&p, build, src, pflags, &filenode);
    if (err)
      return err;
    logtime_end(t);

    #if DEBUG
    str.len = 0;
    fmtast(filenode, &str, 0);
    printf("parse_tu =>\n———————————————————\n%s\n———————————————————\n", str_cstr(&str));
    #endif

    if (build->errcount)
      break;

    t = logtime_start("resolve");
    filenode = resolve_ast(build, filenode);
    logtime_end(t);

    #if DEBUG
    str.len = 0;
    fmtast(filenode, &str, 0);
    printf("resolve_ast =>\n———————————————————\n%s\n———————————————————\n", str_cstr(&str));
    #endif

    array_push(&build->pkg.a, as_Node(filenode));
  }
  return 0;
}


static void set_build_opt(BuildCtx* build) {
  const char* opt = cliopt_str(cliopts, "opt");
  if (!opt) {
    // set default opt level based on build mode
    build->opt = build->debug ? OptMinimal : OptBalanced;
    if (cliopt_bool(cliopts, "small"))
      build->opt = OptSize;
    return;
  }
  if (opt[0] && opt[1] == 0) switch (opt[0]) {
    case '0': build->opt = OptNone; return;
    case '1': build->opt = OptMinimal; return;
    case '2': build->opt = OptBalanced; return;
    case '3': build->opt = OptPerf; return;
    case 's': build->opt = OptSize; return;
  }
  die("invalid -opt: %s", opt);
  exit(1);
}


static void init_copath_vars() {
  //          USE                        DEFAULT
  // COROOT   Directory of co itself     argv[0]/../..
  // COPATH   Directory for user files   ~/.co
  // COCACHE  Directory for build cache  COPATH/cache-VERSION

  Str s = str_make(NULL, 0);

  COROOT = getenv("COROOT");
  usize coroot_offs = s.len;
  if (!COROOT || strlen(COROOT) == 0) {
    // /path/to/build/debug/co => /path/to
    const char* exepath = sys_exepath();
    usize dirlen = path_dirlen(exepath, path_dirlen(exepath, strlen(exepath)));
    // MAX(1) here causes us to use "/" (instead of ".") if path is short
    safecheckexpr( path_dir(&s, exepath, MAX(1, dirlen)), true);
    safecheckexpr( str_appendc(&s, '\0'), true);
  }

  COPATH = getenv("COPATH");
  usize copath_offs = s.len;
  if (!COPATH || strlen(COPATH) == 0) {
    safecheckexpr( path_join(&s, sys_homedir(), ".co"), true);
    safecheckexpr( str_appendc(&s, '\0'), true);
  }

  COCACHE = getenv("COCACHE");
  usize cocache_offs = s.len;
  if (!COCACHE || strlen(COCACHE) == 0) {
    const char* copath = COPATH ? COPATH : &s.v[copath_offs];
    safecheckexpr( path_join(&s, copath, "cache-0.0.1"), true);
    safecheckexpr( str_appendc(&s, '\0'), true);
  }

  if (!COROOT)  COROOT = &s.v[coroot_offs];
  if (!COPATH)  COPATH = &s.v[copath_offs];
  if (!COCACHE) COCACHE = &s.v[cocache_offs];

  dlog("COROOT  = %s", COROOT);
  dlog("COPATH  = %s", COPATH);
  dlog("COCACHE = %s", COCACHE);
}


#ifdef WITH_LLVM
  static int main2_llvm(BuildCtx* build) {
    // initialize llvm
    auto t = logtime_start("[llvm] init:");
    PANIC_ON_ERROR( llvm_init() );
    logtime_end(t);

    // build module
    // build->opt = OptSpeed;
    CoLLVMBuild buildopt = {
      .target_triple = llvm_host_triple(),
      .enable_tsan = false,
      .enable_lto = false,
    };
    CoLLVMModule m;
    t = logtime_start("[llvm] build module:");
    PANIC_ON_ERROR( llvm_build(build, &m, &buildopt) );
    logtime_end(t);

    // #if DEBUG
    // dlog("═════════════════════════════════════════════════════════════════════");
    // llvm_module_dump(&m);
    // dlog("═════════════════════════════════════════════════════════════════════");
    // #endif

    const char* outfile = cliopt_str(cliopts, "output");
    const char* outfile_ir = cliopt_str(cliopts, "output-ir");
    const char* outfile_asm = cliopt_str(cliopts, "output-asm");

    if (outfile_ir && outfile_ir[0])
      PANIC_ON_ERROR( llvm_module_emit(&m, outfile_ir, CoLLVMEmit_ir, CoLLVMEmit_debug) );

    if (outfile_asm && outfile_asm[0])
      PANIC_ON_ERROR( llvm_module_emit(&m, outfile_asm, CoLLVMEmit_asm, 0) );

    if (!outfile)
      return 0;

    // generate object code
    Str outfile_o = str_dupcstr(outfile); str_appendcstr(&outfile_o, ".o");
    t = logtime_start("[llvm] emit object file:");
    PANIC_ON_ERROR( llvm_module_emit(&m, str_cstr(&outfile_o), CoLLVMEmit_obj, 0) );
    logtime_end(t);

    // link executable
    const char* objfiles[] = { outfile_o.v };
    CoLLVMLink link = {
      .target_triple = buildopt.target_triple,
      .outfile = outfile,
      .infilec = countof(objfiles),
      .infilev = objfiles,
    };
    t = logtime_start("[llvm] link executable:");
    PANIC_ON_ERROR( llvm_link(&link) );
    logtime_end(t);

    //llvm_module_dispose(&m);
    return 0;
  }
#endif


int main(int argc, const char** argv) {
  time_init();
  fastrand_seed(nanotime());

  // set heap memory allocator
  mem_ctx_set(mem_mkalloc_libc());

  // parse command-line options
  parse_cliopts(argc, argv);

  // initialize COPATH et al
  PANIC_ON_ERROR( sys_init_exepath(argv[0]) );
  init_copath_vars();

  // run unit tests
  #if CO_TESTING_ENABLED
    if (co_test_runall(/*filter_prefix*/NULL))
      return 1;
    if (cliopt_bool(cliopts, "test-only")) return 0;
  #endif

  // initialize built-ins
  universe_init();

  // run parser tests (only has effect with CO_TESTING_ENABLED)
  if (parse_test_main(argc, argv)) return 1;

  // setup a build context for the package (can be reused)
  BuildCtx* build = memalloczt(BuildCtx);
  PANIC_ON_ERROR( begin_pkg(build, cliopt_str(cliopts, "pkgname")) );
  if (symlen(build->pkgid) == 0)
    panic("empty pkgname");

  // configure build mode
  build->safe  = !cliopt_bool(cliopts, "unsafe");
  build->debug = cliopt_bool(cliopts, "debug");
  set_build_opt(build);

  // add a source file to the package
  error err;
  Source src1 = {0};
  const char* filename = cli_args.len ? cli_args.v[0] : "examples/hello.co";
  if (( err = source_open_file(&src1, filename) ))
    panic("%s: %s", filename, error_str(err));
  b_add_source(build, &src1);

  // // compute and print source checksum
  // source_checksum(&src1);
  // print_src_checksum(&src1);

  // parse
  PANIC_ON_ERROR( parse_pkg(build) );
  if (build->errcount)
    return 1;

  // codegen
  #ifdef WITH_LLVM
    return main2_llvm(build);
  #else
    return 0;
  #endif
}


#endif // CO_NO_LIBC
