#include <stdio.h>
#include <err.h>

#include "coimpl.h"
#include "test.h"
#include "time.h"
#include "parse/parse.h"
#include "parse/resolve.h"
#include "sys.h"


void cli_usage(const char* prog) {
  fprintf(stdout, "usage: %s <lua-file>\n", prog);
}

void on_diag(Diagnostic* d, void* userdata) {
  Str* sp = str_tmp();
  *sp = diag_fmt(d, *sp);
  fwrite((*sp)->p, (*sp)->len, 1, stderr);
}

void print_src_checksum(Mem mem, const Source* src) {
  Str s = str_make_hex_lc(mem, src->sha256, sizeof(src->sha256));
  printf("%s %s\n", s->p, src->filename->p);
  str_free(s);
}

void scan_all(BuildCtx* build) {
  Scanner scanner = {0};
  for (Source* src = build->srclist; src != NULL; src = src->next) {
    dlog("scan %s", src->filename->p);
    error err = ScannerInit(&scanner, build, src, 0);
    if (err)
      panic("ScannerInit: %s", error_str(err));
    while (ScannerNext(&scanner) != TNone) {
      printf(">> %s\n", TokName(scanner.tok));
    }
  }
}

int main(int argc, const char** argv) {
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
  error err = source_open_file(&src1, mem, filename);
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
  auto pkgscope = ScopeNew(mem, universe_scope());
  for (Source* src = build.srclist; src != NULL; src = src->next) {
    auto t = logtime_start("parse");
    FileNode* result;
    error err = parse_tu(&p, &build, src, 0, pkgscope, &result);
    if (err)
      panic("parse_tu: %s", error_str(err));
    logtime_end(t);
    printf("parse_tu =>\n————————————————————\n%s\n————————————————————\n",
      fmtast(result));

    t = logtime_start("resolve");
    result = resolve_ast(&build, pkgscope, result);
    logtime_end(t);
    printf("resolve_ast =>\n————————————————————\n%s\n————————————————————\n",
      fmtast(result));
  }

  return 0;
}
