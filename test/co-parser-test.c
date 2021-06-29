/*

AST-based parser test program.

Parses all *.co files in FIXTURES_DIR. If parsing fails the test fails.

If a file contains a "#*!AST ... *#" comment block, the contents of that comment is
compared with the results of the parser. If the results differ the test fails.

The expected AST in a "!AST" is a LISP representation of the parse result as produced
by NodeRepr. The first line of an "!AST" comment (immediately following "!AST") can
declare flags for NodeReprFlagsParse to enable additional detail for comparison.
AST text is pretty-printed so you can format it however you'd like, line breaks or not.
Common LISP ";" line comments are supported as well, i.e. "; line comment..."

Examples:

  foo.co
  | x = 4
  | #*!AST
  | (Let x (IntLit 4))
  | *#

  foo.co with types
  | x = 4
  | #*!AST types
  | (Let x (IntLit 4 ideal) [ideal])
  | *#

  foo.co with types, use count and attributes
  | x = 4
  | #*!AST types usecount attrs
  | (Let x @const (uses 0) (IntLit @const 4 ideal) [ideal])
  | *#

*/
#include "co/common.h"
#include "co/build.h"
#include "co/util/array.h"
#include "co/util/rtimer.h"
#include "co/util/tmpstr.h"
#include "co/util/sexpr.h"
#include "co/parse/parse.h"

ASSUME_NONNULL_BEGIN

#define BANNER "——————————————————————————————————————————————————————————————————————\n"

static auto FIXTURES_DIR = "test/parse";
static const char* progname = "test_parser"; // updated in main

typedef struct TestCtx {
  Build* build;
  Scope* pkgscope;
  Parser parser;
  u32    nerrors; // number of diagnostic messages with level DiagError
} TestCtx;

static TestCtx* testctx_new();
static void testctx_free(TestCtx*);
static Node* parse_source(TestCtx* tx, Source* src);
static Node* parse_file(TestCtx* tx, Str filename);
static const char* extract_src_ast_comment(Source* nullable src, u32* len_out);
static bool diff_ast(
  const char* filename,
  const char* expectstr, u32 expectlen,
  const char* actualstr, u32 actuallen);


static bool run_parse_test(Str cofile) {
  //dlog("parse %s", cofile);

  TestCtx* tx = testctx_new();
  bool ok = false;

  // parse
  Node* ast = parse_file(tx, cofile);
  if (!ast || tx->nerrors > 0) {
    errlog("%s: failed to parse", cofile);
    goto end;
  }

  // extract expected AST comment
  u32 expectlen;
  const char* expectstr = extract_src_ast_comment(tx->build->pkg->srclist, &expectlen);

  // if there's an expected AST defined, compare that to the actual AST
  if (expectlen == 0) {
    dlog("warning: skipping verififcation of %s as no #*!AST...*# comment found", cofile);
  } else {
    // parse any repr flags following "#*!AST ..."
    // find end of first line
    const char* ln = memchr(expectstr, '\n', (size_t)expectlen);
    u32 line1len = expectlen;
    NodeReprFlags ast_repr_fl = 0;
    if (ln) {
      // parse node repr flags
      line1len = (u32)(uintptr_t)(ln - expectstr);
      ast_repr_fl = NodeReprFlagsParse(expectstr, line1len);

      // exclude the first line
      expectstr = ln;
      expectlen -= line1len;
    }

    Str* actualstr = tmpstr_get();
    *actualstr = NodeRepr(ast, *actualstr, (ast_repr_fl & ~NodeReprColor) | NodeReprNoColor);
    // Note: +1 & -2 to exclude "(" ... ")" of actualstr
    if (!diff_ast(cofile, expectstr, expectlen, *actualstr + 1, str_len(*actualstr) - 2)) {
      // the actual AST differs from the expected AST -- this is a test failure
      goto end;
    }
  }

  ok = true;
  fprintf(stderr, "%s %s OK\n", progname, cofile);

end:
  testctx_free(tx);
  return ok;
}


static bool diff_ast(
  const char* filename,
  const char* expectstr, u32 expectlen,
  const char* actualstr, u32 actuallen)
{
  // run both expected and actual through the S-expression pretty-printer
  // to remove any formatting differences.
  Mem mem = MemLinearAlloc();

  SExpr* actualn = sexpr_parse((const u8*)actualstr, actuallen, mem);
  assertnotnull(actualn);
  // drop leading "File"
  if (actualn->type == SExprList &&
      actualn->list.head->type == SExprAtom &&
      actualn->list.head->atom.namelen == 4 &&
      memcmp(actualn->list.head->atom.name, "File", 4) == 0)
  {
    actualn->list.head = actualn->list.head->next;
  }
  Str* actualp = tmpstr_get();
  *actualp = sexpr_fmt(actualn, *actualp, SExprFmtPretty);

  SExpr* expectn = sexpr_parse((const u8*)expectstr, expectlen, mem);
  assertnotnull(expectn);
  Str* expectp = tmpstr_get();
  *expectp = sexpr_fmt(expectn, *expectp, SExprFmtPretty);

  MemLinearFree(mem);

  ConstStr expect = *expectp;
  ConstStr actual = *actualp;

  if (str_eq(expect, actual))
    return true;

  errlog("%s: unexpected AST (%s:%d)\n"
         BANNER
         "Expected AST:\n"
         "%s\n"
         BANNER
         "Actual AST:\n"
         "%s\n"
         BANNER
         "",
         filename, path_cwdrel(__FILE__), __LINE__, expect, actual);

  // invoke "diff -u" if available on the system
  //
  char expectfile[64];
  char actualfile[64];
  char randbuf[4];
  arc4random_buf(randbuf, sizeof(randbuf));
  snprintf(expectfile, sizeof(expectfile), ".expected_ast.tmp-%x", *((u32*)&randbuf));
  arc4random_buf(randbuf, sizeof(randbuf));
  snprintf(actualfile, sizeof(actualfile), ".actual_ast.tmp-%x", *((u32*)&randbuf));

  FILE* expectfp = fopen(expectfile, "w");
  FILE* actualfp = fopen(actualfile, "w");
  fwrite(expect, str_len(expect), 1, expectfp);
  fwrite(actual, str_len(actual), 1, actualfp);
  fputc('\n', expectfp);
  fputc('\n', actualfp);
  fclose(expectfp);
  fclose(actualfp);

  char cmdbuf[512];
  auto z = snprintf(cmdbuf, sizeof(cmdbuf),
    "diff --text --minimal -U 1 '%s' '%s'", expectfile, actualfile);
  if (z < (int)sizeof(cmdbuf)) {
    system(cmdbuf);
  }

  unlink(expectfile);
  unlink(actualfile);

  return false;
}


// ------------------------------------------------------------------------------------------


// extract_src_ast_comment finds and returns a "#*!AST ... *#" comment in src.
// This intentionally does not use scanner or parser to extract the comment,
// but instead uses a stand-alone implementation to reduce the test surface
// (i.e. if there's a bug in the parser with scanning comments, we could get
// false positive test results.)
static const char* extract_src_ast_comment(Source* nullable src, u32* len_out) {
  if (src) {
    const char* startstr = "#*!AST";
    auto startp = (const u8*)strnstr((const char*)src->body, startstr, (size_t)src->len);
    if (startp) {
      u32 start = (u32)(uintptr_t)(startp - src->body) + strlen(startstr);
      u32 end = src->len;
      for (u32 i = src->len; --i > start; ) {
        if (src->body[i] == '#' && src->body[i - 1] == '*') {
          end = i - 1;
          break;
        }
      }
      if (end > start) {
        *len_out = end - start;
        return (const char*)&src->body[start];
      }
    }
  }
  // not found
  *len_out = 0;
  return "";
}


static void diag_handler(Diagnostic* d, void* userdata) {
  asserteq(d->level, DiagError); // scanner only produces error diagnostics
  auto tx = (TestCtx*)userdata;
  if (d->level == DiagError)
    tx->nerrors++;
  // print the error with full details on stderr
  Str* sp = tmpstr_get();
  *sp = diag_fmt(*sp, d);
  fwrite(*sp, str_len(*sp), 1, stderr);
}


static Node* nullable parse_source(TestCtx* tx, Source* src) {
  PkgAddSource(tx->build->pkg, src);
  return Parse(&tx->parser, tx->build, src, ParseFlagsDefault, tx->pkgscope);
}


static Node* nullable parse_file(TestCtx* tx, Str filename) {
  Source* src = memalloct(tx->build->mem, Source);
  if (!SourceOpen(src, tx->build->pkg, filename))
    panic("failed to open %s", filename);
  return parse_source(tx, src);
}


// static Node* parse_text(TestCtx* tx, Str filename, const char* sourcetext, size_t len) {
//   Source* src = memalloct(tx->build->mem, Source);
//   SourceInitMem(src, tx->build->pkg, filename, sourcetext, len);
//   return parse_source(tx, src);
// }


static TestCtx* testctx_new() {
  Build* build = test_build_new();
  assertnotnull(build);
  TestCtx* tx = memalloct(build->mem, TestCtx);
  if (!tx)
    panic("failed to allocate memory");
  tx->build = build;
  tx->pkgscope = ScopeNew(GetGlobalScope(), build->mem);
  build->userdata = tx;
  build->diagh = diag_handler;
  return tx;
}


static void testctx_free(TestCtx* tx) {
  // frees all memory allocated in tx->build->mem which includes tx itself
  test_build_free(tx->build);
}


static bool has_suffix(const char* subj, size_t subjlen, const char* suffix, size_t suffixlen) {
  if (subjlen < suffixlen)
    return false;
  return memcmp(&subj[subjlen - suffixlen], suffix, suffixlen) == 0;
}


static void find_files(Array* files, const char* dir, const char* filter_suffix) {
  DIR* dirp = opendir(dir);
  if (!dirp)
    panic("opendir %s", dir);
  size_t filter_suffix_len = strlen(filter_suffix);
  DirEntry e;
  while (fs_readdir(dirp, &e) > 0) {
    switch (e.d_type) {
      case DT_REG:
      case DT_LNK:
      case DT_UNKNOWN:
        if ( e.d_name[0] != '.' &&       // is not a dotfile
             ( filter_suffix_len == 0 || // has no suffix filter OR suffix filter passes
               has_suffix(e.d_name, (size_t)e.d_namlen, filter_suffix, filter_suffix_len)
             )
           )
        {
          Str filename = path_join(dir, e.d_name);
          ArrayPush(files, filename, MemHeap);
          // ArrayPush(files, str_cpy(e.d_name, (u32)e.d_namlen), MemHeap);
        }
        break;
      default:
        break;
    }
  }
  closedir(dirp);
}


static int run_parse_test_thread(void* chp) {
  Chan* ch = (Chan*)chp;
  Str cofile = NULL;
  while (ChanRecv(ch, &cofile)) {
    if (!run_parse_test(cofile))
      return 1;
  }
  return 0;
}


static bool run_parse_tests_concurrently(Array* cofiles) {
  Mem mem = MemHeap;
  u32 nthreads = os_ncpu() - 1;
  Chan* ch = ChanOpen(mem, sizeof(Str), /*buffer_size*/nthreads);
  thrd_t* threads = memalloc(mem, sizeof(thrd_t) * nthreads);
  for (u32 i = 0; i < nthreads; i++) {
    auto tstatus = thrd_create(&threads[i], run_parse_test_thread, (void*)ch);
    assert(tstatus == thrd_success);
  }
  for (u32 i = 0; i < cofiles->len; i++) {
    ChanSend(ch, &cofiles->v[i]);
  }
  ChanClose(ch);
  bool ok = true;
  for (u32 i = 0; i < nthreads; i++) {
    int retval = 0;
    thrd_join(threads[i], &retval);
    ok = ok && retval == 0;
  }
  ChanFree(ch);
  memfree(mem, threads);
  return ok;
}


static bool run_parse_tests_serially(Array* cofiles) {
  bool ok = true;
  for (u32 i = 0; i < cofiles->len; i++) {
    ok = run_parse_test(cofiles->v[i]) && ok;
  }
  return ok;
}


int main(int argc, const char** argv) {
  // #if R_TESTING_ENABLED
  // if (testing_main(1, argv) != 0)
  //   return 1;
  // #endif

  RTimer rtimer = {0};
  rtimer_start(&rtimer);
  auto time_start = nanotime();

  progname = path_base(argv[0]);

  // if (chdir(FIXTURES_DIR) != 0)
  //   panic("chdir %s/%s", os_getcwd_str(), FIXTURES_DIR);

  // find .co files
  Array cofiles = Array_INIT; // Str[]
  find_files(&cofiles, FIXTURES_DIR, ".co");
  if (cofiles.len == 0) {
    errlog("no .co files found in %s", FIXTURES_DIR);
    return 1;
  }

  // run all tests (CLI flag -threads enables running tests on multiple threads)
  bool ok = false;
  if (cofiles.len > 1 && argc > 1 && strcmp(argv[1], "-threads") == 0) {
    ok = run_parse_tests_concurrently(&cofiles);
  } else {
    ok = run_parse_tests_serially(&cofiles);
  }

  if (!ok)
    return 1;

  rtimer_log(&rtimer, "%u tests", cofiles.len);

  // report OK with real time duration
  char buf[64];
  auto buflen = fmtduration(buf, sizeof(buf), nanotime() - time_start);
  dlog("OK: %u tests passed in %.*s", cofiles.len, buflen, buf);

  return 0;
}

ASSUME_NONNULL_END
