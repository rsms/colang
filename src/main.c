#include <stdio.h>
#include <err.h>

#include "coimpl.h"
#include "test.c"
#include "hash.c"
#include "time.h"
#include "parse/parse.h"
#include "sys.c"

static char tmpbuf[4096];

void cli_usage(const char* prog) {
  fprintf(stdout, "usage: %s <lua-file>\n", prog);
}

void on_diag(Diagnostic* d, void* userdata) {
  Str str = str_make(tmpbuf, sizeof(tmpbuf));
  assert(diag_fmt(d, &str));
  fwrite(str.v, str.len, 1, stderr);
  str_free(&str);
}

void print_src_checksum(Mem mem, const Source* src) {
  ABuf s = abuf_make(tmpbuf, sizeof(tmpbuf));
  abuf_reprhex(&s, src->sha256, sizeof(src->sha256));
  abuf_terminate(&s);
  printf("%s %s\n", tmpbuf, src->filename);
}

void scan_all(BuildCtx* build) {
  Scanner scanner = {0};
  for (Source* src = build->srclist; src != NULL; src = src->next) {
    dlog("scan %s", src->filename);
    error err = ScannerInit(&scanner, build, src, 0);
    if (err)
      panic("ScannerInit: %s", error_str(err));
    while (ScannerNext(&scanner) != TNone) {
      printf(">> %s\n", TokName(scanner.tok));
    }
  }
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

  // TODO: simplify this by maybe making syms & pkg fields of BuildCtx, instead of
  // separately allocated data.

  // create a symbol pool to hold all known symbols (keywords and identifiers)
  SymPool syms = {0};
  sympool_init(&syms, universe_syms(), mem, NULL);

  // create a build context
  BuildCtx build = {0};
  const char* pkgid = "foo";
  BuildCtxInit(&build, mem, &syms, pkgid, on_diag, NULL);

  // add a source file to the logical package
  Source src1 = {0};
  const char* filename = argv[1] ? argv[1] : "examples/hello.co";
  error err = source_open_file(&src1, filename);
  if (err)
    panic("source_open_data: %s %s", filename, error_str(err));
  b_add_source(&build, &src1);

  // // compute and print source checksum
  // source_checksum(&src1);
  // print_src_checksum(mem, &src1);

  // scan all sources of the package
  // scan_all(&build);

  // parse
  Parser p = {0};
  Str str = str_make(tmpbuf, sizeof(tmpbuf));
  auto pkgscope = ScopeNew(mem, universe_scope());
  for (Source* src = build.srclist; src != NULL; src = src->next) {
    auto t = logtime_start("parse");
    FileNode* result;
    error err = parse_tu(&p, &build, src, 0, pkgscope, &result);
    if (err)
      panic("parse_tu: %s", error_str(err));
    logtime_end(t);
    str.len = 0;
    fmtast(result, &str, 0);
    printf("parse_tu =>\n————————————————————\n%s\n————————————————————\n", str.v);

    t = logtime_start("resolve");
    result = resolve_ast(&build, pkgscope, result);
    logtime_end(t);
    str.len = 0;
    fmtast(result, &str, 0);
    printf("resolve_ast =>\n————————————————————\n%s\n————————————————————\n", str.v);
  }

  return 0;
}
