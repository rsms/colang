#include <rbase/rbase.h>
#include "parse.h"
#if R_UNIT_TEST_ENABLED

typedef struct TokStringPair { Tok tok; const char* value; } TokStringPair;
// testscan(fl, sourcetext, tok1, value1, tok2, value2, ..., TNone)
static void testscan(ParseFlags, const char* sourcetext, ...);
// make_expectlist(len, tok1, value1, tok2, value2, ..., TNone)
static TokStringPair* make_expectlist(size_t* len_out, const char* sourcetext, ...);


R_UNIT_TEST(scan_basics) {
  testscan(ParseFlagsDefault,
    "hello = 123\n",
    TIdent,  "hello",
    TAssign, "=",
    TIntLit, "123",
    TSemi,   "",
    TNone);
}

R_UNIT_TEST(scan_comments) {
  auto source = "hello # trailing\n# leading1\n# leading2\n123";
  testscan(ParseComments, source,
    TIdent,  "hello",
    TSemi,   "",
    TComment, " trailing",
    TIntLit, "123",
    TComment, " leading1",
    TComment, " leading2",
    TSemi,   "",
    TNone);

  testscan(ParseFlagsDefault, source,
    TIdent,  "hello",
    TSemi,   "",
    // no comment since ParseComments is not set
    TIntLit, "123",
    // no comment since ParseComments is not set
    // no comment since ParseComments is not set
    TSemi,   "",
    TNone);
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


typedef struct ScanTestCtx {
  int nerrors;
} ScanTestCtx;


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

static void testscanp(
  ParseFlags     flags,
  const char*    sourcetext,
  TokStringPair* expectlist,
  size_t         nexpect)
{
  Pkg pkg = { .dir = "." };
  Source src;
  Scanner scanner;
  SymPool syms;

  SourceInitMem(&pkg, &src, "input", sourcetext, strlen(sourcetext));
  PkgAddSource(&pkg, &src);

  sympool_init(&syms, universe_syms(), pkg.mem, NULL);

  ScanTestCtx testctx = {};
  assert(ScannerInit(&scanner, pkg.mem, &syms, on_scan_err, &src, flags, &testctx));

  size_t ntokens = 0;
  while (1) {
    if (ntokens >= nexpect) {
      asserteq(ScannerNext(&scanner), TNone);
      break;
    }

    auto expect = &expectlist[ntokens];
    bool err = false;
    Tok tok;

    // comments are collected in a list by the scanner, rather than emitted as tokens
    if (expect->tok == TComment) {
      auto c = ScannerCommentPopFront(&scanner);
      if (!c) {
        // err instead of asset to make error message "unexpected token ... (expected comment)"
        err = true;
      } else {
        asserteq(strlen(expect->value), c->len);
        assert(memcmp(expect->value, c->ptr, c->len) == 0);
        memfree(scanner.mem, c);
        ntokens++;
        continue;
      }
    } else if ((flags & ParseComments) != 0) {
      auto c = ScannerCommentPopFront(&scanner);
      if (c) {
        errlog("unexpected token: %s \"%.*s\" (expected %s \"%s\")",
          TokName(TComment), (int)c->len, c->ptr, TokName(expect->tok), expect->value);
        assert(c == NULL);
      }
    }

    Tok t = ScannerNext(&scanner);
    assert(t != TNone);
    assert(expect->tok != TNone);

    size_t vallen;
    auto valptr = ScannerTokStr(&scanner, &vallen);
    // dlog(">> %-7s \"%.*s\"", TokName(t), (int)vallen, valptr);

    err = err || expect->tok != t;
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
static void testscan(ParseFlags flags, const char* sourcetext, ...) {
  va_list ap;
  va_start(ap, sourcetext);
  size_t nexpect;
  auto expectlist = make_expectlistv(&nexpect, sourcetext, ap);
  va_end(ap);
  testscanp(flags, sourcetext, expectlist, nexpect);
  memfree(NULL, expectlist);
}


#endif /* R_UNIT_TEST_ENABLED */
