#include "common.h"
#include "parse/parse.h"
#include "ir/ir.h"
#include "ir/irbuilder.h"
#include "util/rtimer.h"
#include "util/tmpstr.h"

#ifdef CO_WITH_LLVM
  #include "llvm/llvm.h"
#endif
#ifdef CO_WITH_BINARYEN
  #ifdef CO_WITH_LLVM
    #error both binaryen and llvm
  #else
    #include "bn/bn.h"
  #endif
#endif

//#define ENABLE_CO_IR // enable generating Co's own IR

ASSUME_NONNULL_BEGIN

// co filesystem directories. init() from either env (same names) or default values
static const char* COROOT = NULL;  // directory of co itself. default: argv[0]/../..
static const char* COPATH = NULL;  // directory for user files. default: ~/.co
static const char* COCACHE = NULL; // directory for build cache. default: COPATH/cache

// rtimer helpers
#define ENABLE_RTIMER_LOGGING
#ifdef ENABLE_RTIMER_LOGGING
  #define RTIMER_INIT          RTimer rtimer_ = {0}
  #define RTIMER_START()       rtimer_start(&rtimer_)
  #define RTIMER_LOG(fmt, ...) rtimer_log(&rtimer_, fmt, ##__VA_ARGS__)
#else
  #define RTIMER_INIT          do{}while(0)
  #define RTIMER_START()       do{}while(0)
  #define RTIMER_LOG(fmt, ...) do{}while(0)
#endif

#ifdef DEBUG
  #define PRINT_BANNER() \
    printf("————————————————————————————————————————————————————————————————\n")
  static void dump_ast(const char* message, Node* ast) {
    Str* sp = tmpstr_get();
    *sp = NodeRepr(
      ast, *sp, NodeReprTypes
              | NodeReprUseCount
              | NodeReprRefs
              | NodeReprAttrs
    );
    fprintf(stderr, "%s%s\n", message, *sp);
    PRINT_BANNER();
  }
#else
  #define PRINT_BANNER() do{}while(0)
  #define dump_ast(...) do{}while(0)
#endif


#ifdef ENABLE_CO_IR
static void dump_ir(const PosMap* posmap, const IRPkg* pkg) {
  auto s = IRReprPkgStr(pkg, posmap, str_new(512));
  s = str_appendc(s, '\n');
  fwrite(s, str_len(s), 1, stderr);
  str_free(s);
}
#endif


static void diag_handler(Diagnostic* d, void* userdata) {
  auto s = str_new(strlen(d->message) + 32);
  s = diag_fmt(s, d);
  // s[str_len(s)] = '\n'; // replace nul byte
  // fwrite(s, str_len(s) + 1, 1, stderr);
  fwrite(s, str_len(s), 1, stderr);
  str_free(s);
}


int cmd_build(int argc, const char** argv) {
  if (argc < 3) {
    errlog("missing input");
    return 1;
  }

  RTIMER_INIT;
  auto timestart = nanotime();

  Pkg pkg = {
    .mem  = MemHeap,
    .dir  = ".",
    .id   = "foo/bar",
    .name = "bar",
  };

  // make sure COCACHE exists
  if (!fs_mkdirs(MemHeap, COCACHE, 0700)) {
    errlog("failed to create directory %s", COCACHE);
    return 1;
  }

  // guess argument 1 is a directory
  pkg.dir = argv[2];
  RTIMER_START();
  if (!PkgScanSources(&pkg)) {
    if (errno != ENOTDIR)
      panic("%s (errno %d %s)", pkg.dir, errno, strerror(errno));
    // guessed wrong; it's probably a file
    errno = 0; // clear errno to make errlog messages sane
    pkg.dir = path_dir(argv[2]);
    if (!PkgAddFileSource(&pkg, argv[2]))
      panic("%s (errno %d %s)", argv[2], errno, strerror(errno));
  }
  RTIMER_LOG("find source files");

  // setup build context
  RTIMER_START();
  SymPool syms = {0};
  sympool_init(&syms, universe_syms(), MemHeap, NULL);
  Mem astmem = MemHeap; // allocate AST in global memory pool
  //Mem astmem = MemLinearAlloc(1024/*pages*/); // allocate AST in a linear slab of memory
  Build build = {0};
  build_init(&build, astmem, &syms, &pkg, diag_handler, NULL);
  build.debug = true; // include debug info
  // build.opt = CoOptFast;
  RTIMER_LOG("init build state");

  // setup package namespace and create package AST node
  Scope* pkgscope = ScopeNew(GetGlobalScope(), build.mem);
  Node* pkgnode = CreatePkgAST(&build, pkgscope);

  // parse source files
  RTIMER_START();
  Source* src = pkg.srclist;
  Parser parser = {0};
  while (src) {
    Node* filenode = Parse(&parser, &build, src, ParseFlagsDefault, pkgscope);
    if (!filenode)
      return 1;
    NodeArrayAppend(build.mem, &pkgnode->cunit.a, filenode);
    NodeTransferUnresolved(pkgnode, filenode);
    src = src->next;
  }
  RTIMER_LOG("parse");
  dump_ast("", pkgnode);
  if (build.errcount) {
    errlog("%u %s", build.errcount, build.errcount == 1 ? "error" : "errors");
    return 1;
  }

  // validate AST produced by parser
  #ifdef DEBUG
    if (!NodeValidate(&build, pkgnode, NodeValidateDefault))
      return 1;
    dlog("AST validated OK");
  #endif

  //goto end; // XXX

  // resolve identifiers if needed (note: it often is needed)
  if (NodeIsUnresolved(pkgnode)) {
    RTIMER_START();
    pkgnode = ResolveSym(&build, ParseFlagsDefault, pkgnode, pkgscope);
    RTIMER_LOG("resolve symbolic references");
    dump_ast("", pkgnode);
    if (build.errcount) {
      errlog("%u %s", build.errcount, build.errcount == 1 ? "error" : "errors");
      return 1;
    }
    assert( ! NodeIsUnresolved(pkgnode)); // no errors should mean all resolved

    // validate AST after symbol resolution
    #ifdef DEBUG
      if (!NodeValidate(&build, pkgnode, NodeValidateDefault))
        return 1;
      dlog("AST validated OK");
    #endif
  }

  // check for and report unused globals
  if (build.debug) {
    for (u32 i = 0; i < pkgnode->cunit.a.len; i++) {
      Node* file = pkgnode->cunit.a.v[i];
      for (u32 j = 0; j < file->cunit.a.len; j++) {
        Node* n = file->cunit.a.v[j];
        if (n->kind == NVar && NodeIsUnused(n) && !NodeIsPublic(n)) {
          build_diagf(&build, DiagWarn, NodePosSpan(n), "unused internal %s",
            n->var.init == NULL ? "variable" :
            NodeIsType(n->var.init) ? "type" : "value");
        }
      }
    }
  }

  //goto end; // XXX

  // resolve types
  RTIMER_START();
  pkgnode = ResolveType(&build, pkgnode);
  RTIMER_LOG("semantic analysis & type resolution");
  dump_ast("", pkgnode);
  if (build.errcount) {
    errlog("%u %s", build.errcount, build.errcount == 1 ? "error" : "errors");
    return 1;
  }
  // validate AST after type resolution
  #ifdef DEBUG
    if (!NodeValidate(&build, pkgnode, NodeValidateMissingTypes))
      return 1;
    dlog("AST validated OK");
  #endif

  //goto end; // XXX

  // build Co IR
  #ifdef ENABLE_CO_IR
    RTIMER_START();
    IRBuilder irbuilder = {};
    IRBuilderInit(&irbuilder, &build, IRBuilderComments);
    IRBuilderAddAST(&irbuilder, pkgnode);
    RTIMER_LOG("build Co IR");
    PRINT_BANNER();
    dump_ir(&build.posmap, irbuilder.pkg);
    IRBuilderDispose(&irbuilder);
  #endif


  // emit target code with LLVM
  #ifdef CO_WITH_LLVM
    PRINT_BANNER();
    RTIMER_START();

    // build.safe = false;
    #if 0
    // JIT
    //build.opt = CoOptFast;
    llvm_jit(&build, pkgnode);
    #else
    // Build native executable
    // build.opt = CoOptFast;
    if (!llvm_build_and_emit(&build, pkgnode, NULL/*target=host*/)) {
      return 1;
    }
    #endif
    RTIMER_LOG("llvm total");
  #endif


  // generate WASM with binaryen
  #ifdef CO_WITH_BINARYEN
    RTIMER_START();
    if (!bn_codegen(&build, pkgnode))
      return 1;
    RTIMER_LOG("binaryen total");
    PRINT_BANNER();
  #endif


  // ————————————————————————————————
  UNUSED /* label */ end:
  {
    // print how much (real) time we spent
    auto timeend = nanotime();
    char abuf[40];
    auto buflen = fmtduration(abuf, countof(abuf), timeend - timestart);
    printf("done in %.*s (real time)\n", buflen, abuf);
  }
  return 0;
}

int main_usage(const char* arg0, int exit_code) {
  fprintf(exit_code == 0 ? stdout : stderr,
    "usage: %s build <srcdir>\n"
    "       %s build <srcfile> <outfile>\n"
    "       %s help\n"
    "",
    arg0,
    arg0,
    arg0
  );
  return exit_code;
}

static bool init(const char* argv0) {
  COROOT = getenv("COROOT");
  COPATH = getenv("COPATH");
  COCACHE = getenv("COCACHE");

  if (!COROOT || strlen(COROOT) == 0) {
    // COROOT is not set; infer from argv[0]
    COROOT = NULL;
    auto dirbase = argv0;
    auto dir = strrchr(dirbase, PATH_SEPARATOR);
    if (!dir && (dirbase = getenv("_")))
      dir = strrchr(dirbase, PATH_SEPARATOR);
    if (dir) {
      char buf[512];
      auto dirlen = (size_t)dir - (size_t)dirbase;
      auto len = MIN(dirlen, sizeof(buf));
      memcpy(buf, dirbase, len + 1);
      buf[len] = '\0';
      auto pch = realpath(buf, NULL);
      if (pch)
        COROOT = path_dir(pch);
    }
    if (!COROOT) {
      errlog("unable to infer COROOT; set it in env");
      return false;
    }
  }

  if (!COPATH || strlen(COPATH) == 0)
    COPATH = path_join(os_user_home_dir(), ".co");

  if (!COCACHE || strlen(COCACHE) == 0)
    COCACHE = path_join(COPATH, "cache");

  dlog("COROOT=%s", COROOT);
  dlog("COPATH=%s", COPATH);
  dlog("COCACHE=%s", COCACHE);

  return true;
}

int main(int argc, const char** argv) {
  #if R_TESTING_ENABLED
    if (testing_main(1, argv) != 0)
      return 1;
    if (argc > 1 && strcmp(argv[1], "-testonly") == 0)
      return 0;
  #endif

  if (!init(argv[0]))
    return 1;

  if (argc < 2)
    return main_usage(argv[0], 1);

  if (strcmp(argv[1], "build") == 0)
    return cmd_build(argc, argv);

  // help | -h* | --help
  if (strstr(argv[1], "help") || strcmp(argv[1], "-h") == 0)
    return main_usage(argv[0], 0);

  if (strlen(argv[1]) > 0 && argv[1][0] == '-')
    errlog("unknown option: %s", argv[1]);
  else
    errlog("unknown command: %s", argv[1]);
  return 1;
}

ASSUME_NONNULL_END
