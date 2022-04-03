#include <stdio.h>
#include <err.h>

#include "colib.h"
#include "parse/parse.h"

#ifdef WITH_LLVM
  #include "llvm/llvm.h"
#endif

#define CHECKERR(expr) \
  ({ error err__ = (expr); \
     err__ != 0 ? panic(#expr ": %s", error_str(err__)) : ((void)0); })

static char tmpbuf[4096];


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


int main(int argc, const char** argv) {
  time_init();
  fastrand_seed(nanotime());
  if (co_test_main(argc, argv))
    return 1;
  universe_init();

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
  BuildCtx build = {0};
  CHECKERR( begin_pkg(&build, "example") );

  // configure build mode
  build.opt   = OptNone;
  build.safe  = true;
  build.debug = true;

  // add a source file to the package
  Source src1 = {0};
  const char* filename = argv[1] ? argv[1] : "examples/hello.co";
  if (( err = source_open_file(&src1, filename) ))
    panic("%s: %s", filename, error_str(err));
  b_add_source(&build, &src1);

  // // compute and print source checksum
  // source_checksum(&src1);
  // print_src_checksum(&src1);

  // parse
  CHECKERR( parse_pkg(&build) );
  if (build.errcount)
    return 1;

  // codegen
  #ifdef WITH_LLVM
  // initialize llvm
  auto t = logtime_start("llvm_init");
  if (!llvm_init())
    panic("llvm_init");
  logtime_end(t);

  // build module, generate object code and link executable
  t = logtime_start("llvm_build_and_emit");
  CHECKERR( llvm_build_and_emit(&build, llvm_host_triple()) );
  logtime_end(t);
  #endif

  return 0;
}
