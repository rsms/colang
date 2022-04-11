#include "colib.h"


typedef struct UTF8Test { const char utf8[32]; Rune utf32[16]; } UTF8Test;
// uft8 field is [N] instead of * so that we can safely test utf8_decode4
static_assert(sizeof(((UTF8Test*)0)->utf8) == ALIGN2(sizeof(((UTF8Test*)0)->utf8), 4),
  "utf8 field not aligned to 4-byte boundary");


static const UTF8Test utf8_tests[] = {
  { "hello", {'h','e','l','l','o'} },
  { "你好",   {0x4F60, 0x597D} },
  { "नमस्ते",  {0x0928, 0x092e, 0x0938, 0x094d, 0x0924, 0x0947} },
  { "مرحبا", {0x0645, 0x0631, 0x062d, 0x0628, 0x0627} },
  // at the boundary of invalid ranges
  { "\xED\x9F\xBF", {0xD7FF} }, // just before UTF-16 surrogates
  { "\xEE\x80\x80", {0xE000} }, // just after UTF-16 surrogates
};

static const UTF8Test utf8_bad_tests[] = {
  { "\xFF" },
  { "\xFE" },
  { "\x80" },
  { "\xC0\x0A" },
  { "\xEBxx" },

  // Single UTF-16 surrogates
  { "\xED\xA0\x80", { 0xD800 } },
  { "\xED\x9F\xC0", { 0xD800 } },
  { "\xED\xAD\xBF", { 0xDB7F } },
  { "\xED\xAE\x80", { 0xDB80 } },
  { "\xED\xAF\xBF", { 0xDBFF } },
  { "\xED\xB0\x80", { 0xDC00 } },
  { "\xED\xBE\x80", { 0xDF80 } },
  { "\xED\xBF\xBF", { 0xDFFF } },

  // Paired UTF-16 surrogates
  { "\xED\xA0\x80\xED\xB0\x80", { 0xD800, 0xDC00 } },
  { "\xED\xA0\x80\xED\xBF\xBF", { 0xD800, 0xDFFF } },
  { "\xED\xAD\xBF\xED\xB0\x80", { 0xDB7F, 0xDC00 } },
  { "\xED\xAD\xBF\xED\xBF\xBF", { 0xDB7F, 0xDFFF } },
  { "\xED\xAE\x80\xED\xB0\x80", { 0xDB80, 0xDC00 } },
  { "\xED\xAE\x80\xED\xBF\xBF", { 0xDB80, 0xDFFF } },
  { "\xED\xAF\xBF\xED\xB0\x80", { 0xDBFF, 0xDC00 } },
  { "\xED\xAF\xBF\xED\xBF\xBF", { 0xDBFF, 0xDFFF } },
};

static const Rune utf32_bad_tests[][16] = {
  { RuneMax+1 },
};


static usize utf32len(const Rune* runes, usize cap) {
  usize n = 0;
  for (; n < cap && runes[n]; n++) {
  }
  return n;
}


static usize fmtrunes(char* buf, usize bufcap, const Rune* runes, usize len) {
  ABuf s = abuf_make(buf, bufcap);
  for (usize i = 0; i < len; i++) {
    if (i) abuf_c(&s, ' ');
    abuf_fmt(&s, "U+%0*X", (int)((runes[i] > 0xFFFF) ? 8 : 4), runes[i]);
  }
  return abuf_terminate(&s);
}


DEF_TEST(utf8_decode) {
  char tmpbuf[512];
  for (usize ti = 0; ti < countof(utf8_tests); ti++) {
    auto t = &utf8_tests[ti];

    const u8* input = (const u8*)t->utf8;
    const u8* input_end = input + strlen(t->utf8);
    Rune      result[countof(t->utf32)];
    usize     limit = 100;
    usize     ri = 0;
    Rune      r, r2;

    // dlog("——— %s ———", t->utf8);
    while (input < input_end) {
      assertf(--limit, "test #%zu", ti);
      assertf(ri < countof(result), "test #%zu", ti);

      const u8* start = input;
      const u8* input2 = input;
      assertf(utf8_decode(&input, input_end, &r),
        "utf8_tests[%zu] utf8_decode failed", ti);
      assertf(utf8_decode4(&input2, &r2),
        "utf8_tests[%zu] utf8_decode4 failed", ti);
      assertf(input == input2,
        "utf8_decode/utf8_decode4 consumed different amounts: %zu/%zu",
        (usize)(uintptr)(input - start),
        (usize)(uintptr)(input2 - start));
      assertf(r == r2,
        "utf8_decode/utf8_decode4 produced different results: %04X/%04X",
        r, r2);

      assertf(ri < countof(result), "test #%zu", ti);
      result[ri++] = r;
    }

    if (memcmp(result, t->utf32, sizeof(result[0]) * ri) != 0) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->utf8, strlen(t->utf8));
      log("test #%zu failed: \"%s\" (UTF-8 \"%s\")", ti, t->utf8, tmpbuf);
      usize nrunes = utf32len(t->utf32, countof(t->utf32));
      fmtrunes(tmpbuf, sizeof(tmpbuf), t->utf32, nrunes);
      log("expected: %s", tmpbuf);
      fmtrunes(tmpbuf, sizeof(tmpbuf), result, ri);
      log("got:      %s", tmpbuf);
      assert(0);
    }
  }

  // utf8_decode should fail on all bad-input tests
  for (usize ti = 0; ti < countof(utf8_bad_tests); ti++) {
    auto t = &utf8_bad_tests[ti];
    Rune r;

    // utf8_decode
    bool succeeded = true;
    const u8* input = (const u8*)t->utf8;
    const u8* input_end = input + strlen(t->utf8);
    while (input < input_end) {
      if (!utf8_decode(&input, input_end, &r)) {
        succeeded = false;
        break;
      }
    }
    if (succeeded) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->utf8, strlen(t->utf8));
      assertf(0,"bad_tests[%zu] \"%s\" (UTF-8 \"%s\") utf8_decode succeeded => 0x%04X",
        ti, t->utf8, tmpbuf, r);
    }

    // utf8_decode4
    succeeded = true;
    input = (const u8*)t->utf8;
    while (input < input_end) {
      if (!utf8_decode4(&input, &r)) {
        succeeded = false;
        break;
      }
    }
    if (succeeded) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->utf8, strlen(t->utf8));
      assertf(0,"bad_tests[%zu] \"%s\" (UTF-8 \"%s\") utf8_decode4 succeeded => 0x%04X",
        ti, t->utf8, tmpbuf, r);
    }
  }
}


DEF_TEST(utf8_encode) {
  u8 outbuf[64];
  char tmpbuf[128];

  for (usize ti = 0; ti < countof(utf8_tests); ti++) {
    auto t = &utf8_tests[ti];
    usize nrunes = utf32len(t->utf32, countof(t->utf32));
    u8* dst = outbuf;
    const u8* dst_end = dst + sizeof(outbuf);

    for (usize i = 0; i < nrunes; i++) {
      u8* start = dst;
      bool ok = utf8_encode(&dst, dst_end, t->utf32[i]);
      if (!ok) {
        fmtrunes(tmpbuf, sizeof(tmpbuf), t->utf32, nrunes);
        assertf(0,"utf8_tests[%zu]: utf8_encode({ %s }) failed", ti, tmpbuf);
      }
      assertf(dst > start, "made progress");
      assertf(memcmp(dst, "\xEF\xBF\xBD", 3) != 0,
        "invalid codepoint t->utf32[%zu]=%04X", i, t->utf32[i]);
    }
  }

  for (usize ti = 0; ti < countof(utf8_bad_tests); ti++) {
    auto t = &utf8_bad_tests[ti];
    usize nrunes = utf32len(t->utf32, countof(t->utf32));
    for (usize i = 0; i < nrunes; i++) {
      u8* dst = outbuf;
      bool ok = utf8_encode(&dst, outbuf + sizeof(outbuf), t->utf32[i]);
      if (ok) {
        fmtrunes(tmpbuf, sizeof(tmpbuf), t->utf32, nrunes);
        assertf(0,"utf8_encode({ %s }) should fail, but succeeded", tmpbuf);
      }
    }
  }

  for (usize ti = 0; ti < countof(utf32_bad_tests); ti++) {
    const Rune* utf32 = utf32_bad_tests[ti];
    usize nrunes = utf32len(utf32, countof(utf32_bad_tests[0]));
    for (usize i = 0; i < nrunes; i++) {
      u8* dst = outbuf;
      bool ok = utf8_encode(&dst, outbuf + sizeof(outbuf), utf32[i]);
      if (ok) {
        fmtrunes(tmpbuf, sizeof(tmpbuf), utf32, nrunes);
        assertf(0,"utf8_encode({ %s }) should fail, but succeeded", tmpbuf);
      }
    }
  }
}


DEF_TEST(utf8_len) {
  char tmpbuf[512];

  for (usize ti = 0; ti < countof(utf8_tests); ti++) {
    auto t = &utf8_tests[ti];
    usize expect = utf32len(t->utf32, countof(t->utf32));
    usize actual = utf8_len((const u8*)t->utf8, strlen(t->utf8), 0);
    if (actual != expect) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->utf8, strlen(t->utf8));
      assertf(0,"utf8_len(\"%s\") => %zu; expected %zu", tmpbuf, actual, expect);
    }
  }

  // printlen
  struct {
    const char* input;
    usize expected_len;
    usize expected_printlen;
    UnicodeLenFlags flags;
  } tests[] = {
    { "hej   \x1B[31mredfg \x1B[44mbluebg\x1B[49m redfg\x1B[39m", 44, 40 },
    { "hello \x1B[31mredfg \x1B[44mbluebg\x1B[49m redfg\x1B[39m", 24, 24, UC_LFL_SKIP_ANSI },
    { "fancy \x1B[38;5;203mred\x1B[39m", 9, 9, UC_LFL_SKIP_ANSI },
  };
  for (usize i = 0; i < countof(tests); i++) {
    auto t = &tests[i];

    usize len = utf8_len((const u8*)t->input, strlen(t->input), t->flags);
    if (len != t->expected_len) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->input, strlen(t->input));
      assertf(0,"tests[%zu]: utf8_len(%s) => %zu (expected %zu)",
        i, tmpbuf, len, t->expected_len);
    }

    len = utf8_printlen((const u8*)t->input, strlen(t->input), t->flags);
    if (len != t->expected_printlen) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->input, strlen(t->input));
      assertf(0,"tests[%zu]: utf8_printlen(%s) => %zu (expected %zu)",
        i, tmpbuf, len, t->expected_printlen);
    }
  }
}


DEF_TEST(utf8_validate) {
  char tmpbuf[512];

  for (usize ti = 0; ti < countof(utf8_tests); ti++) {
    auto t = &utf8_tests[ti];
    const u8* badbyte = utf8_validate((const u8*)t->utf8, strlen(t->utf8));
    if (badbyte) {
      usize tmp1len = (sizeof(tmpbuf)/4)*3;
      sfmt_repr(tmpbuf, tmp1len, t->utf8, strlen(t->utf8));
      char* tmp2 = tmpbuf + tmp1len + 1;
      sfmt_repr(tmp2, sizeof(tmpbuf)-tmp1len-1, badbyte, 1);
      assertf(0,"utf8_tests[%zu]: utf8_validate(%s) => 0x%02x '%s' (expected NULL)",
        ti, tmpbuf, *badbyte, tmp2);
    }
  }

  for (usize ti = 0; ti < countof(utf8_bad_tests); ti++) {
    auto t = &utf8_bad_tests[ti];
    const u8* badbyte = utf8_validate((const u8*)t->utf8, strlen(t->utf8));
    if (badbyte == NULL) {
      sfmt_repr(tmpbuf, sizeof(tmpbuf), t->utf8, strlen(t->utf8));
      assertf(0,"utf8_bad_tests[%zu]: utf8_validate(%s) did not fail", ti, tmpbuf);
    }
    // usize tmp1len = (sizeof(tmpbuf)/4)*3;
    // sfmt_repr(tmpbuf, tmp1len, t->utf8, strlen(t->utf8));
    // char* tmp2 = tmpbuf + tmp1len + 1;
    // sfmt_repr(tmp2, sizeof(tmpbuf)-tmp1len-1, badbyte, 1);
    // dlog("utf8_validate(%s) => 0x%02x '%s'", tmpbuf, *badbyte, tmp2);
  }
}


DEF_TEST(ascii_is) {
  for (char c = 0; c < '0'; c++) {
    assert(!ascii_isdigit(c));
    assert(!ascii_ishexdigit(c));
  }
  for (char c = '0'; c < '9'+1; c++) {
    assert(ascii_isdigit(c));
    assert(ascii_ishexdigit(c));
  }
  for (char c = 'A'; c < 'F'+1; c++) {
    assert(!ascii_isdigit(c));
    assert(ascii_ishexdigit(c));
  }
  for (char c = 'a'; c < 'f'+1; c++) {
    assert(!ascii_isdigit(c));
    assert(ascii_ishexdigit(c));
  }
  for (char c = 'f'+1; c < 'z'+1; c++) {
    assert(!ascii_isdigit(c));
    assert(!ascii_ishexdigit(c));
  }
}
