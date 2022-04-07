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
  {"pkgname", 0, "<name>", CLI_T_STR, "Use <name> for package" },
  {"output-asm", 0, "<file>", CLI_T_STR, "Write machine assembly to <file>" },
  {"output-ir", 0, "<file>", CLI_T_STR, "Write IR source to <file>" },
  {"opt", 0, "<level>", CLI_T_STR, "Set specific optimization level (0-3, s)" },

  #if CO_TESTING_ENABLED
  {"test-only", 0, "", CLI_T_BOOL, "Exit after running unit tests" },
  #endif
  {0},
};
static const char* cli_help =
  "<srcfile>\n"
  "  If not provided, read source from stdin";
static CStrArray cli_args;


static void parse_cliopts(int argc, const char** argv) {
  progname = argv[0];
  static const char* cli_restbuf[16];
  cli_args = array_make(CStrArray, cli_restbuf, sizeof(cli_restbuf));
  switch (cliopt_parse(cliopts, argc, argv, &cli_args, cli_usage, cli_help)) {
    case CLI_PS_OK:     break;
    case CLI_PS_HELP:   exit(0);
    case CLI_PS_BADOPT: exit(1);
    case CLI_PS_NOMEM:  exit(127);
  }
}


#define CHECKERR(expr) \
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
  Str str = str_make(tmpbuf, sizeof(tmpbuf)); // for debug logging
  for (Source* src = build->srclist; src != NULL; src = src->next) {
    auto t = logtime_start("parse");
    FileNode* filenode;
    error err = parse_tu(&p, build, src, 0, &filenode);
    if (err)
      return err;
    logtime_end(t);
    str.len = 0;
    fmtast(filenode, &str, 0);
    printf("parse_tu =>\n————————————————————\n%s\n————————————————————\n", str.v);

    t = logtime_start("resolve");
    filenode = resolve_ast(build, filenode);
    logtime_end(t);
    str.len = 0;
    fmtast(filenode, &str, 0);
    printf("resolve_ast =>\n————————————————————\n%s\n————————————————————\n", str.v);

    array_push(&build->pkg.a, as_Node(filenode));
  }
  return 0;
}


#ifdef WITH_LLVM
  static int main2_llvm(BuildCtx* build) {
    // initialize llvm
    auto t = logtime_start("[llvm] init:");
    CHECKERR( llvm_init() );
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
    CHECKERR( llvm_build(build, &m, &buildopt) );
    logtime_end(t);
    //llvm_module_dump(m);

    const char* outfile = cliopt_str(cliopts, "output", NULL);
    const char* outfile_ir = cliopt_str(cliopts, "output-ir", NULL);
    const char* outfile_asm = cliopt_str(cliopts, "output-asm", NULL);

    if (outfile_ir && outfile_ir[0])
      CHECKERR( llvm_module_emit(&m, outfile_ir, CoLLVMEmit_ir, CoLLVMEmit_debug) );

    if (outfile_asm && outfile_asm[0])
      CHECKERR( llvm_module_emit(&m, outfile_asm, CoLLVMEmit_asm, 0) );

    if (!outfile)
      return 0;

    // generate object code
    Str outfile_o = str_dupcstr(outfile); str_appendcstr(&outfile_o, ".o");
    t = logtime_start("[llvm] emit object file:");
    CHECKERR( llvm_module_emit(&m, str_cstr(&outfile_o), CoLLVMEmit_obj, 0) );
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
    CHECKERR( llvm_link(&link) );
    logtime_end(t);

    //llvm_module_dispose(&m);
    return 0;
  }
#endif


static void set_build_opt(BuildCtx* build) {
  const char* opt = cliopt_str(cliopts, "opt", NULL);
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


int main(int argc, const char** argv) {
  time_init();
  fastrand_seed(nanotime());

  // parse command-line options
  parse_cliopts(argc, argv);

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

  // select a memory allocator
  #ifdef CO_NO_LIBC
    static u8 memv[4096*8];
    FixBufAllocator fba;
    Mem mem = FixBufAllocatorInit(&fba, memv, sizeof(memv));
  #else
    Mem mem = mem_mkalloc_libc();
  #endif
  mem_ctx_set(mem);

  error err;

  // setup a build context for the package (can be reused)
  BuildCtx* build = memalloczt(BuildCtx);
  CHECKERR( begin_pkg(build, cliopt_str(cliopts, "pkgname", "example")) );
  if (symlen(build->pkgid) == 0)
    panic("empty pkgname");

  // configure build mode
  build->safe  = !cliopt_bool(cliopts, "unsafe");
  build->debug = cliopt_bool(cliopts, "debug");
  set_build_opt(build);

  // add a source file to the package
  Source src1 = {0};
  const char* filename = cli_args.len ? cli_args.v[0] : "examples/hello.co";
  if (( err = source_open_file(&src1, filename) ))
    panic("%s: %s", filename, error_str(err));
  b_add_source(build, &src1);

  // // compute and print source checksum
  // source_checksum(&src1);
  // print_src_checksum(&src1);

  // parse
  CHECKERR( parse_pkg(build) );
  if (build->errcount)
    return 1;

  // codegen
  #ifdef WITH_LLVM
    return main2_llvm(build);
  #else
    return 0;
  #endif
}
