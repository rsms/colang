#include "../common.h"
#if R_TESTING_ENABLED
#include "parse.h"
#include "../util/tmpstr.h"

typedef struct ScanTestCtx {
  u32         nerrors;
  const char* last_errmsg;
} ScanTestCtx;
typedef struct TokStringPair { Tok tok; const char* value; } TokStringPair;
// testscan(fl, sourcetext, tok1, value1, tok2, value2, ..., TNone)
static u32 testscan(ParseFlags, const char* sourcetext, ...);
// make_expectlist(len, tok1, value1, tok2, value2, ..., TNone)
static TokStringPair* make_expectlist(Mem, size_t* len_out, const char* sourcetext, ...);
// test_scanner_new creates a new scanner with all new dedicated resources like sympool
static Scanner* test_scanner_new(ParseFlags, const char* sourcetext);
static Scanner* test_scanner_newn(ParseFlags flags, const char* sourcetext, size_t len);
static void test_scanner_free(Scanner*);
inline static ScanTestCtx* test_scanner_ctx(Scanner* s) {
  return (ScanTestCtx*)s->build->userdata;
}

// TComment is a fake token used only for testing.
// Comments does not produce tokens but are added to a queue by the scanner.
// However in our testscan function calls it is convenient to be able to say
// "at this point there should be a comment on the queue".
static const Tok TComment = TMax;


R_TEST(scan_testutil) {
  // make sure our test utilities work so we can rely on them for further testing
  size_t len = 0;
  auto list = make_expectlist(MemHeap, &len, "hello = 123\n",
    TId,     "hello",
    TAssign, "=",
    TIntLit, "123",
    TSemi,   "",
    TNone
  );
  assert(list != NULL);
  asserteq(len, 5);
  asserteq(list[0].tok, TId);
  assert(strcmp(list[0].value, "hello") == 0);
  memfree(MemHeap, list);
}


R_TEST(scan_basics) {
  u32 nerrors = testscan(ParseFlagsDefault,
    "hello = 123\n"
    "fun",
    TId,"hello", TAssign,"=", TIntLit,"123", TSemi,"",
    TFun,"fun",
    // TId,"fun", TSemi,"",
    TNone);
  asserteq(nerrors, 0);
}


R_TEST(scan_comments) {
  auto source = "hello # trailing\n# leading1\n# leading2\n123";
  u32 nerrors = testscan(ParseComments, source,
    TId,      "hello",
    TSemi,    "",
    TComment, " trailing",
    TComment, " leading1",
    TComment, " leading2",
    TIntLit,  "123",
    TSemi,    "",
    TNone);
  asserteq(nerrors, 0);

  nerrors = testscan(ParseFlagsDefault, source,
    TId,     "hello",
    TSemi,   "",
    // no comment since ParseComments is not set
    TIntLit, "123",
    // no comment since ParseComments is not set
    // no comment since ParseComments is not set
    TSemi,   "",
    TNone);
  asserteq(nerrors, 0);
}


R_TEST(scan_comment_blocks) {
  auto source = "hello #* line1\n# line2\n*# line3\n#**#\n";
  u32 nerrors = testscan(ParseComments, source,
    TId,      "hello",
    TComment, " line1\n# line2\n",
    TId,      "line3",
    TSemi,    "",
    TComment, "",
    TNone);
  asserteq(nerrors, 0);

  nerrors = testscan(ParseFlagsDefault, source,
    TId,      "hello",
    // no comment since ParseComments is not set
    TId,      "line3",
    TSemi,    "",
    // no comment since ParseComments is not set
    TNone);
  asserteq(nerrors, 0);
}


R_TEST(scan_indent) {
  const u32 noerrors = 0;
  asserteq(noerrors, testscan(ParseIndent,
    "1 A\n"
    "  2 B\n"
    "  2\n"
    "    3\n"
    "  2\n"
    "       \n" // this empty line should not cause indent to be produced
    "  2\n",
                    TIntLit,"1", TId,"A", TSemi,"",
    TIndent,"  ",   TIntLit,"2", TId,"B", TSemi,"",
    TIndent,"  ",   TIntLit,"2", TSemi,"",
    TIndent,"    ", TIntLit,"3", TSemi,"",
    TIndent,"  ",   TIntLit,"2", TSemi,"",
    TIndent,"  ",   TIntLit,"2", TSemi,"",
    TNone));

  // first line indent
  asserteq(noerrors, testscan(ParseIndent,
    "  A\n",
    TIndent,"  ", TId,"A", TSemi,"",
    TNone));

  // without comments (comments should not cause TIndent)
  asserteq(noerrors, testscan(ParseIndent,
    "A\n"
    "  B\n"
    "  # comment\n"
    "  C\n",
    TId,"A", TSemi,"",
    TIndent,"  ", TId,"B", TSemi,"",
    TIndent,"  ", TId,"C", TSemi,"",
    TNone));

  // with comments
  asserteq(noerrors, testscan(ParseIndent|ParseComments,
    "A\n"
    "  B\n"
    "  # comment\n"
    "  C\n",
    TId,"A", TSemi,"",
    TIndent,"  ", TId,"B", TSemi,"",
    TIndent,"  ", TComment," comment",  TId,"C", TSemi,"",
    // Note: TComment is synthetic: the scanner doesn't actually produce TComment tokens.
    TNone));
}


R_TEST(scan_nulbyte) {
  // nul byte in source is invalid
  auto scanner = test_scanner_newn(ParseFlagsDefault, "a\0b", 3);
  asserteq(ScannerNext(scanner), TId);
  asserteq(ScannerNext(scanner), TNone);
  // expect error
  auto ctx = test_scanner_ctx(scanner);
  asserteq(ctx->nerrors, 1);
  assert(ctx->last_errmsg != NULL);
  assert(strstr(ctx->last_errmsg, "invalid input character") != NULL);
  test_scanner_free(scanner);
}


R_TEST(scan_id_sym) {
  // Sym interning and equivalence
  Tok t;
  auto scanner = test_scanner_new(ParseFlagsDefault, "hello foo hello foo");

  t = ScannerNext(scanner);
  asserteq(t, TId);
  assert(scanner->name != NULL); // should have assigned a Sym
  assert(strcmp(scanner->name, "hello") == 0);
  assert(symfind(scanner->build->syms, "hello", strlen("hello")) == scanner->name);
  auto hello_sym1 = scanner->name;

  t = ScannerNext(scanner);
  asserteq(t, TId);
  assert(scanner->name != NULL); // should have assigned a Sym
  assert(strcmp(scanner->name, "foo") == 0);
  assert(symfind(scanner->build->syms, "foo", strlen("foo")) == scanner->name);
  auto foo_sym1 = scanner->name;

  t = ScannerNext(scanner);
  asserteq(t, TId);
  asserteq(scanner->name, hello_sym1); // should have resulted in same Sym (interned)

  t = ScannerNext(scanner);
  asserteq(t, TId);
  asserteq(scanner->name, foo_sym1); // should have resulted in same Sym (interned)

  asserteq(ScannerNext(scanner), TSemi);
  asserteq(ScannerNext(scanner), TNone);

  auto ctx = test_scanner_ctx(scanner);
  asserteq(ctx->nerrors, 0);
  test_scanner_free(scanner);
}


R_TEST(scan_id_utf8) {
  { // valid unicode
    // 日本語 ("Japanese") U+65E5 U+672C U+8A9E
    auto scanner = test_scanner_new(ParseFlagsDefault, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    asserteq(ScannerNext(scanner), TId);
    asserteq(ScannerNext(scanner), TSemi);
    asserteq(ScannerNext(scanner), TNone);
    auto ctx = test_scanner_ctx(scanner);
    asserteq(ctx->nerrors, 0);
    test_scanner_free(scanner);
  }
  { // valid unicode
    //
    //   U+1F469 woman
    // + U+1F3FD skin tone modifier
    // + U+200D  zero width joiner
    // + U+1F680 rocket ship
    // = astronaut (woman) with skin tone modifier
    //
    // U+1F469 U+1F3FD U+200D U+1F680 ("a" + astronout woman with skin tone modifier)
    //
    auto scanner = test_scanner_new(ParseFlagsDefault,
      "\xf0\x9f\x91\xa9\xf0\x9f\x8f\xbd\xe2\x80\x8d\xf0\x9f\x9a\x80");
    asserteq(ScannerNext(scanner), TId);
    asserteq(ScannerNext(scanner), TSemi);
    asserteq(ScannerNext(scanner), TNone);
    auto ctx = test_scanner_ctx(scanner);
    asserteq(ctx->nerrors, 0);
    test_scanner_free(scanner);
  }
  { // invalid unicode
    auto scanner = test_scanner_new(ParseFlagsDefault, "ab\xff");
    asserteq(ScannerNext(scanner), TId);
    asserteq(ScannerNext(scanner), TSemi);
    asserteq(ScannerNext(scanner), TNone);
    auto ctx = test_scanner_ctx(scanner);
    // expect error
    asserteq(ctx->nerrors, 1);
    assert(ctx->last_errmsg != NULL);
    assert(strstr(ctx->last_errmsg, "invalid UTF-8 encoding") != NULL);
    test_scanner_free(scanner);
  }
}


R_TEST(scan_pos) {
  // source position
  Tok t;
  Pos p;
  auto scanner = test_scanner_new(ParseFlagsDefault, "a b\nc d, e");

  // a
  t = ScannerNext(scanner); asserteq(t, TId);
  p = ScannerPos(scanner);
  asserteq(pos_line(p), 1);
  asserteq(pos_col(p), 1);
  asserteq(pos_origin(p), 1); // 1 since the scanner from test_scanner_new has just one source

  // b
  t = ScannerNext(scanner); asserteq(t, TId);
  p = ScannerPos(scanner);
  asserteq(pos_line(p), 1);
  asserteq(pos_col(p), 3);
  asserteq(pos_origin(p), 1);

  // line 1 ends
  asserteq(ScannerNext(scanner), TSemi);

  // c
  t = ScannerNext(scanner); asserteq(t, TId);
  p = ScannerPos(scanner);
  asserteq(pos_line(p), 2);
  asserteq(pos_col(p), 1);
  asserteq(pos_origin(p), 1);

  // d
  t = ScannerNext(scanner); asserteq(t, TId);
  p = ScannerPos(scanner);
  asserteq(pos_line(p), 2);
  asserteq(pos_col(p), 3);
  asserteq(pos_origin(p), 1);

  // ,
  t = ScannerNext(scanner); asserteq(t, TComma);
  p = ScannerPos(scanner);
  asserteq(pos_line(p), 2);
  asserteq(pos_col(p), 4);
  asserteq(pos_origin(p), 1);

  // e
  t = ScannerNext(scanner); asserteq(t, TId);
  p = ScannerPos(scanner);
  asserteq(pos_line(p), 2);
  asserteq(pos_col(p), 6);
  asserteq(pos_origin(p), 1);

  // ;
  asserteq(ScannerNext(scanner), TSemi);

  asserteq(ScannerNext(scanner), TNone);

  auto ctx = test_scanner_ctx(scanner);
  asserteq(ctx->nerrors, 0);
  test_scanner_free(scanner);
}


// ScannerPos


// --------------------------------------------------------------------------------------------
// test helper functions

static void on_scan_diag(Diagnostic* d, void* userdata) {
  asserteq(d->level, DiagError); // scanner only produces error diagnostics
  auto testctx = (ScanTestCtx*)userdata;
  testctx->nerrors++;
  testctx->last_errmsg = str_cpycstr(d->message);
  // dlog("scan error: %s", msg);
}

static Scanner* test_scanner_newn(ParseFlags flags, const char* sourcetext, size_t len) {
  auto build = test_build_new();
  build->diagh = on_scan_diag;
  build->userdata = memalloct(build->mem, ScanTestCtx);

  Source* src = memalloct(build->mem, Source);
  SourceInitMem(src, build->pkg, "input", sourcetext, len);
  PkgAddSource(build->pkg, src);

  Scanner* scanner = memalloct(build->mem, Scanner);
  assert(ScannerInit(scanner, build, src, flags));
  return scanner;
}

static Scanner* test_scanner_new(ParseFlags flags, const char* sourcetext) {
  return test_scanner_newn(flags, sourcetext, strlen(sourcetext));
}

static void test_scanner_free(Scanner* s) {
  assertnotnull(s);
  Build* build = s->build;
  assertnotnull(build);
  assertnotnull(build->mem);

  // Note: No need to call SourceClose since its in memory and not file-backed.
  // Note: No need to call memfree as test_build_free drops the entire memory space.
  SourceDispose(s->src);
  ScannerDispose(s);
  test_build_free(build);
}


// expects an odd number of arguments in pairs with a TNone terminator:
// tok1, value1, tok2, value2, ... ,TNone
static TokStringPair* make_expectlistv(
  Mem mem, size_t* len_out, const char* sourcetext, va_list ap)
{
  va_list ap2;
  va_copy(ap2, ap);
  size_t len = 1; // 1 for TNone
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

  TokStringPair* expectlist = memalloc(mem, sizeof(TokStringPair) * len);
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

  expectlist[len - 1].tok = TNone;
  expectlist[len - 1].value = "";

  return expectlist;
}

// make_expectlist(len, tok1, value1, tok2, value2, ..., TNone)
static TokStringPair* make_expectlist(Mem mem, size_t* len_out, const char* sourcetext, ...) {
  va_list ap;
  va_start(ap, sourcetext);
  auto expectlist = make_expectlistv(mem, len_out, sourcetext, ap);
  va_end(ap);
  return expectlist;
}


static ConstStr reprstr(const char* pch, u32 len) {
  Str* sp = tmpstr_get();
  *sp = str_appendrepr(*sp, pch, len);
  return *sp;
}


static ConstStr scanposstr(Scanner* scanner) {
  Str* sp = tmpstr_get();
  Pos pos = ScannerPos(scanner);
  *sp = str_appendfmt(*sp, "<input>:%u:%u", pos_line(pos), pos_col(pos));
  return *sp;
}


static const char* tokname(Tok t) {
  return t == TComment ? "comment" : TokName(t);
}


_Noreturn static void err_unexpected(
  Scanner* scanner,
  TokStringPair* expect,
  Tok gottok, const char* gotval, u32 gotvallen,
  Str scantrace)
{
  assertf(!"err_unexpected",
    "\nunexpected scan result at %s"
    "\n  expected token: %s(\"%s\")"
    "\n  got token:      %s(\"%s\")"
    "\n"
    "\nscan trace:"
    "\n%s"
    "\n",
    scanposstr(scanner),
    tokname(expect->tok), reprstr(expect->value, strlen(expect->value)),
    tokname(gottok), reprstr(gotval, gotvallen),
    scantrace
  );
}


static u32 testscanp(
  ParseFlags     flags,
  const char*    sourcetext,
  TokStringPair* expectlist,
  size_t         nexpect)
{
  auto scanner = test_scanner_new(flags, sourcetext);
  Str scantrace = str_new(64);
  size_t ntokens = 0;
  Tok bufTok = TMax; // TMax == no buffered token

  while (1) {
    if (ntokens >= nexpect) {
      asserteq(ScannerNext(scanner), TNone);
      break;
    }

    auto expect = &expectlist[ntokens];
    bool err = false;

    // comments are collected in a list by the scanner, rather than emitted as tokens
    if (expect->tok == TComment) {
      if (bufTok == TMax)
        bufTok = ScannerNext(scanner);
      auto c = ScannerCommentPop(scanner);
      if (!c) {
        // err instead of asset to make error message "unexpected token ... (expected comment)"
        errlog("expected comment but no comment was returned from ScannerCommentPop");
        size_t vallen;
        auto valptr = (const char*)ScannerTokStr(scanner, &vallen);
        err_unexpected(scanner, expect, bufTok, valptr, vallen, scantrace);
      } else {
        if (strlen(expect->value) != c->len || memcmp(expect->value, c->ptr, c->len) != 0) {
          err_unexpected(scanner, expect, TComment, (const char*)c->ptr, c->len, scantrace);
        }
        // update "what we've got so far" trace
        scantrace = str_appendfmt(scantrace, "  comment(\"%s\")\n",
          reprstr((const char*)c->ptr, c->len));
        memfree(scanner->build->mem, c);
        ntokens++;
        continue;
      }
    } else if ((flags & ParseComments) != 0) {
      // we don't expect a comment; check if we got one (which would be unexpected)
      auto c = ScannerCommentPop(scanner);
      if (c)
        err_unexpected(scanner, expect, TComment, (const char*)c->ptr, c->len, scantrace);
    }

    // use token scanned from past comment scan, or scan the next token
    Tok t;
    if (bufTok == TMax) {
      t = ScannerNext(scanner);
    } else {
      t = bufTok;
      bufTok = TMax;
    }

    // for the last token, check to make sure there are no queued comments
    if (t == TNone && (flags & ParseComments) != 0) {
      auto c = ScannerCommentPop(scanner);
      assertf(!c, "unexpected comment at end: \"%s\"",
        reprstr((const char*)c->ptr, c->len));
    }

    size_t vallen;
    auto valptr = (const char*)ScannerTokStr(scanner, &vallen);

    // we only get here if we did not scan a comment

    // update "what we've got so far" trace
    scantrace = str_appendfmt(scantrace, "  %s(\"%s\")\n", tokname(t), reprstr(valptr, vallen));

    // size_t vallen;
    // auto valptr = ScannerTokStr(scanner, &vallen);
    // dlog(">> %-7s \"%.*s\"", TokName(t), (int)vallen, valptr);

    err = err || expect->tok != t;
    err = err || (strlen(expect->value) != (u32)vallen);
    err = err || (memcmp(expect->value, valptr, vallen) != 0);
    if (err)
      err_unexpected(scanner, expect, t, valptr, vallen, scantrace);
    ntokens++;
  }

  asserteq(ntokens, nexpect);
  auto nerrors = test_scanner_ctx(scanner)->nerrors;
  test_scanner_free(scanner);
  str_free(scantrace);
  return nerrors;
}

// testscan(sourcetext, tok1, value1, tok2, value2, ..., TNone)
// returns number of errors encountered
static u32 testscan(ParseFlags flags, const char* sourcetext, ...) {
  va_list ap;
  va_start(ap, sourcetext);
  size_t nexpect;
  auto mem = MemHeap;
  auto expectlist = make_expectlistv(mem, &nexpect, sourcetext, ap);
  va_end(ap);
  u32 nerrors = testscanp(flags, sourcetext, expectlist, nexpect);
  memfree(mem, expectlist);
  return nerrors;
}


#endif /* R_TESTING_ENABLED */
