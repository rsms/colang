#include <rbase/rbase.h>
#include "parse.h"
#if R_UNIT_TEST_ENABLED

typedef struct ScanTestCtx {
  int nerrors;
} ScanTestCtx;

typedef struct TokStringPair {
  Tok         tok;
  const char* value;
} TokStringPair;


// expects an odd number of arguments in pairs with a TNone terminator:
// tok1, value1, tok2, value2, ... ,TNone
static TokStringPair* make_expectlistv(size_t* len_out, const char* sourcetext, va_list ap) {
  va_list ap2;
  va_copy(ap2, ap);
  size_t len = 0;
  for (size_t i = 0; i < 10000; i++) {
    if ((i % 2) == 0) {
      // even arguments are tokens
      if (va_arg(ap2, Tok) == TNone)
        break;
      len++;
    } else {
      // odd arguments are values
      va_arg(ap2, const char*);
    }
  }
  va_end(ap2);

  TokStringPair* expectlist = memalloc(NULL, sizeof(TokStringPair) * len);
  *len_out = len;

  va_copy(ap2, ap);
  size_t x = 0;
  for (size_t i = 0; i < len * 2; i++) {
    if ((i % 2) == 0) {
      // even arguments are tokens
      expectlist[x].tok = va_arg(ap2, Tok);
    } else {
      // odd arguments are values
      expectlist[x].value = va_arg(ap2, const char*);
      x++;
    }
  }
  va_end(ap2);

  return expectlist;
}

// make_expectlist(len, tok1, value1, tok2, value2, ..., TNone)
static TokStringPair* make_expectlist(size_t* len_out, const char* sourcetext, ...) {
  va_list ap;
  va_start(ap, sourcetext);
  auto expectlist = make_expectlistv(len_out, sourcetext, ap);
  va_end(ap);
  return expectlist;
}


static void on_scan_err(const Source* src, SrcPos pos, const Str msg, void* userdata) {
  auto testctx = (ScanTestCtx*)userdata;
  testctx->nerrors++;
  errlog("scan error: %s", msg);
}

static void testscanp(const char* sourcetext, TokStringPair* expectlist, size_t nexpect) {
  Pkg pkg = { .dir = "." };
  Source src;
  Scanner scanner;
  SymPool syms;

  SourceInitMem(&pkg, &src, "input", sourcetext, strlen(sourcetext));
  PkgAddSource(&pkg, &src);

  sympool_init(&syms, universe_syms(), pkg.mem, NULL);

  ScanTestCtx testctx = {};
  ParseFlags parseflags = ParseComments;
  assert(ScannerInit(&scanner, pkg.mem, &syms, on_scan_err, &src, parseflags, &testctx));

  size_t ntokens = 0;
  while (1) {
    Tok t = ScannerNext(&scanner);
    if (t == TNone)
      break;
    size_t vallen;
    auto valptr = ScannerTokStr(&scanner, &vallen);
    // dlog(">> %-7s \"%.*s\"", TokName(t), (int)vallen, valptr);
    assertop(ntokens, <, nexpect);
    auto expect = &expectlist[ntokens];
    bool err = expect->tok != t;
    err = err || (strlen(expect->value) != (u32)vallen);
    err = err || (memcmp(expect->value, valptr, vallen) != 0);
    if (err) {
      errlog("unexpected token: %s \"%.*s\" (expected %s \"%s\")",
        TokName(t), (int)vallen, valptr, TokName(expect->tok), expect->value);
    }
    asserteq(expect->tok, t);
    asserteq(strlen(expect->value), (u32)vallen);
    assert(memcmp(expect->value, valptr, vallen) == 0);
    ntokens++;
  }

  asserteq(testctx.nerrors, 0);
  asserteq(ntokens, nexpect);
  SourceDispose(&src);
}

// testscan(sourcetext, tok1, value1, tok2, value2, ..., TNone)
static void testscan(const char* sourcetext, ...) {
  va_list ap;
  va_start(ap, sourcetext);
  size_t nexpect;
  auto expectlist = make_expectlistv(&nexpect, sourcetext, ap);
  va_end(ap);
  testscanp(sourcetext, expectlist, nexpect);
  memfree(NULL, expectlist);
}

R_UNIT_TEST(scan_testutil) {
  // make sure our test utilities work so we can rely on them for further testing
  size_t expectlen = 0;
  auto expectlist = make_expectlist(&expectlen, "hello = 123\n",
    TIdent,  "hello",
    TAssign, "=",
    TIntLit, "123",
    TSemi,   "",
    TNone
  );
  assert(expectlist != NULL);
  asserteq(expectlen, 4);
  asserteq(expectlist[0].tok, TIdent);
  assert(strcmp(expectlist[0].value, "hello") == 0);
  memfree(NULL, expectlist);
}

R_UNIT_TEST(scan_basics) {
  testscan("hello = 123\n",
    TIdent,  "hello",
    TAssign, "=",
    TIntLit, "123",
    TSemi,   "",
    TNone);
}

#endif /* R_UNIT_TEST_ENABLED */
