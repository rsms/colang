// co command-line program
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#ifndef CO_NO_LIBC

#include <stdio.h>
#include <getopt.h>

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

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
  {"pkgname", 0, "<name>", CLI_T_STR, "Use <name> for package" },
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


static void print_src_checksum(const Source* src) {
  ABuf s = abuf_make(tmpbuf, sizeof(tmpbuf));
  abuf_reprhex(&s, src->sha256, sizeof(src->sha256));
  abuf_terminate(&s);
  printf("%s %s\n", src->filename, tmpbuf);
}


static void on_diag(Diagnostic* d, void* userdata) {
  Str str = str_make(tmpbuf, sizeof(tmpbuf));
  assertf(diag_fmt(d, &str), "failed to allocate memory");
  fwrite(str.v, str.len, 1, stderr);
  str_free(&str);
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
    #define LOG_AST(label, n) { \
      str.len = 0; \
      fmtast((n), &str, 0); \
      printf("\n——————————————————————————————— %s ———————————————————————————————" \
             "\n%s\n", label, str_cstr(&str)); \
    }
  #else
    #define LOG_AST(label, n) ((void)0)
  #endif

  u8 secondary_analysis_st[countof(build->sources_st)];
  auto secondary_analysis =
    array_make(Array(u8), secondary_analysis_st, sizeof(secondary_analysis_st));
  if (!array_reserve(&build->pkg.a, build->sources.len) ||
      !array_reserve(&secondary_analysis, build->sources.len))
  {
    return err_nomem;
  }

  // parse
  auto t = logtime_start("parse & analyze");
  error err;
  for (u32 i = 0; i < build->sources.len && build->errcount == 0; i++) {
    FileNode* filenode;
    if (( err = parse_tu(&p, build, build->sources.v[i], pflags, &filenode) ))
      return err;
    LOG_AST("parse", filenode);
    // if filenode has unresolved references we need to analyze it in a secondary pass,
    // otherwise we can do analysis right away.
    if (NodeIsUnresolved(filenode)) {
      secondary_analysis.v[i] = true;
      secondary_analysis.len = i+1;
    } else {
      filenode = resolve_ast(build, filenode);
      LOG_AST("analyze1", filenode);
      // TODO: could do codegen now here if we were to split up codegen by file
    }
    build->pkg.a.v[i] = as_Node(filenode);
    build->pkg.a.len++;
  }
  logtime_end(t);

  // TODO: MT: wait for parse jobs here

  // secondary analysis
  if (secondary_analysis.len > 0) {
    t = logtime_start("analyze2");
    for (u32 i = 0; i < secondary_analysis.len && build->errcount == 0; i++) {
      if (secondary_analysis.v[i]) {
        build->pkg.a.v[i] = resolve_ast(build, build->pkg.a.v[i]);
        LOG_AST("analyze2", build->pkg.a.v[i]);
      }
    }
    logtime_end(t);
  }

  //LOG_AST("package", &build->pkg);

  array_free(&secondary_analysis);

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
    safenotnull( path_dir(&s, exepath, MAX(1, dirlen)) );
    safecheckexpr( str_appendc(&s, '\0'), true);
  }

  COPATH = getenv("COPATH");
  usize copath_offs = s.len;
  if (!COPATH || strlen(COPATH) == 0) {
    safenotnull( path_join(&s, sys_homedir(), ".co") );
    safecheckexpr( str_appendc(&s, '\0'), true);
  }

  COCACHE = getenv("COCACHE");
  usize cocache_offs = s.len;
  if (!COCACHE || strlen(COCACHE) == 0) {
    const char* copath = COPATH ? COPATH : memstrdup(s.v + copath_offs);
    safenotnull( path_join(&s, copath, "cache-0.0.1") );
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



static void set_pkgname_from_dir(BuildCtx* b, const char* filename) {
  // sets package name to the directory's name
  Str name = str_makex(b->tmpbuf[0]);
  bool ok = path_abs(&name, filename);

  dlog("path_abs: %s", str_cstr(&name));

  const char* base = path_base(&name, name.v, name.len);
  if UNLIKELY(!base || !ok)
    die("%s", error_str(err_nomem));

  dlog("pkgname: %s", base);

  assert(name.len > 0); // path_base never produces an empty result
  if ((base[0] == PATH_SEPARATOR && base[1] == 0) ||
      strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
  {
    // invalid name
    die("unable to infer package name from directory name. Please use -pkgname");
  }
  b_set_pkgname(b, base);
  str_free(&name);
}


static void add_sources_dir(BuildCtx* build, const char* filename, int fd) {
  if (cli_args.len > 1) {
    die("unexpected extra argument \"%s\" after source directory", cli_args.v[1]);
    close(fd);
  }
  FSDir dir;
  error err = sys_dir_open_fd(fd, &dir);
  if (err || (err = b_add_source_dir(build, filename, dir)))
    die("%s: %s", filename, error_str(err));
  sys_dir_close(dir);
  close(fd);
  set_pkgname_from_dir(build, filename);
  if (build->sources.len == 0)
    die("%s: no .co source files found in directory", cli_args.v[0]);
}


static void add_sources_files(BuildCtx* build, const char* filename, int fd, usize size) {
  Source* src = mem_alloct(build->mem, Source);
  if UNLIKELY(!src)
    die("%s", error_str(err_nomem));

  // source_open_filex takes ownership of fd (including closing it on error)
  error err = source_open_filex(src, filename, fd, size);
  if (err)
    die("%s: %s", filename, error_str(err));
  if (!b_add_source(build, src))
    die("%s", error_str(err_nomem));

  for (u32 i = 1; i < cli_args.len; i++) {
    filename = cli_args.v[i];
    if ((err = b_add_source_file(build, filename)))
      die("%s: %s", filename, error_str(err));
  }
}


static void add_sources(BuildCtx* build) {
  if (cli_args.len == 0 || strcmp(cli_args.v[0], "-") == 0)
    die("TODO read stdin");

  // first argument is either a directory or an individual source file
  const char* filename = cli_args.v[0];
  int fd = open(filename, O_RDONLY);
  if (fd < 0)
    die("%s: %s", filename, strerror(errno));

  // stat file
  struct stat info;
  if (fstat(fd, &info) != 0)
    die("%s: %s (stat)", filename, strerror(errno));

  // directory or individual files
  if (S_ISDIR(info.st_mode)) {
    add_sources_dir(build, filename, fd);
  } else {
    add_sources_files(build, filename, fd, (usize)info.st_size);
  }

  // compute and print source checksums
  for (u32 i = 0; i < build->sources.len; i++) {
    Source* src = build->sources.v[i];
    source_checksum(src);
    print_src_checksum(src);
  }
}


int main(int argc, const char** argv) {
  time_init();
  fastrand_seed(nanotime());

  // set heap memory allocator
  mem_ctx_set(mem_mkalloc_libc());

  // parse command-line options
  parse_cliopts(argc, argv);

  // run unit tests
  #if CO_TESTING_ENABLED
    if (co_test_runall(/*filter_prefix*/NULL))
      return 1;
    if (cliopt_bool(cliopts, "test-only")) return 0;
  #endif

  // initialize COPATH et al
  PANIC_ON_ERROR( sys_init_exepath(argv[0]) );
  init_copath_vars();

  // initialize built-ins
  universe_init();

  // run parser tests (only has effect with CO_TESTING_ENABLED)
  if (parse_test_main(argc, argv)) return 1;

  // setup a build context for the package (can be reused)
  BuildCtx* build = memalloczt(BuildCtx);
  PANIC_ON_ERROR( BuildCtxInit(build, mem_ctx(), on_diag, NULL) );

  // configure build mode
  build->safe  = !cliopt_bool(cliopts, "unsafe");
  build->debug = cliopt_bool(cliopts, "debug");
  set_build_opt(build);

  // add sources
  add_sources(build);

  // override or set package name
  const char* pkgname = cliopt_str(cliopts, "pkgname");
  if (pkgname && pkgname[0])
    b_set_pkgname(build, pkgname);

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
