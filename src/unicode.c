// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


// lookup tables used by utf8_decode
//
// first_byte_mark
//   Once the bits are split out into bytes of UTF-8, this is a mask OR-ed into the
//   first byte, depending on how many bytes follow. There are as many entries in this
//   table as there are UTF-8 sequence types. (I.e., one byte sequence, two byte... etc.).
//   Remember that sequencs for *legal* UTF-8 will be 4 or fewer bytes total.
static const u8 first_byte_mark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
//
static const u8 utf8_seqlentab[] = {
      2,2,2,2,2,2,2,2,2,2,2,2,2,2, /* 0xC2-0xCF */
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, /* 0xD0-0xDF */
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, /* 0xE0-0xEF */
  4,4,4,4,4                        /* 0xF0-0xF4 */
};
//
static const u32 dec_mintab[] = {4194304, 0, 128, 2048, 65536};
static const u8 dec_shiftetab[] = {0, 6, 4, 2, 0};


bool utf8_isvalid(const u8* src, usize len) {
  Rune ign;
  return utf8_decode(&src, src + len, &ign);
}


bool utf8_decode(const u8** src, const u8* end, Rune* result) {
  assertf(*src != end, "empty input");

  const u8* p = *src;
  const u8* s = p;
  u8 b0 = *p;

  if (b0 < 0xC2 || b0 > 0xF4) {
    (*src)++;
    *result = b0;
    return (b0 < RuneSelf);
  }

  u8 len = utf8_seqlentab[b0 - 0xC2];
  *src += len;

  if UNLIKELY(*src > end) {
    *src = end;
    return false;
  }

  Rune r = 0;
  switch (len) {
    case 4: r += *p++; r <<= 6; FALLTHROUGH;
    case 3: r += *p++; r <<= 6;
  }
  r += *p++; r <<= 6;
  r += *p++;

  // precomputed values to subtract from codepoint, depending on how many shifts we did
  static const Rune subtab[] = {0,0x3080,0xE2080,0x3C82080};
  r -= subtab[len - 1];
  *result = r;

  // accumulate error conditions
  int e = (r < dec_mintab[len]) << 6;  // non-canonical encoding
  e |= ((r >> 11) == 0x1b) << 7; // surrogate half?
  e |= (r > RuneMax) << 8;       // out of range?
  e |= (s[1] & 0xc0) >> 2;
  if (len > 2) e |= (s[2] & 0xc0) >> 4;
  if (len > 3) e |= (s[3]       ) >> 6;
  e ^= 0x2a; // top two bits of each tail byte correct?
  e >>= dec_shiftetab[len];

  return !e;
}


bool utf8_decode4(const u8** src, Rune* result) {
  // branchless decoder by Christopher Wellons, released into the public domain.
  // https://nullprogram.com/blog/2017/10/06/

  static const u8 lentab[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0
  };
  static const u8 maskstab[]  = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
  static const u8 shiftctab[] = {0, 18, 12, 6, 0};

  const u8* s = *src;
  u8 len = lentab[s[0] >> 3];

  // Update the src pointer early so that the next iteration can start
  // working on the next character.
  // Neither Clang nor GCC figure out this reordering on their own.
  *src = s + len + !len;

  // Assume a four-byte character and load four bytes. Unused bits are shifted out
  Rune r = (u32)(s[0] & maskstab[len]) << 18;
  r |= (u32)(s[1] & 0x3f) << 12;
  r |= (u32)(s[2] & 0x3f) <<  6;
  r |= (u32)(s[3] & 0x3f) <<  0;
  r >>= shiftctab[len];
  *result = r;

  // accumulate error conditions
  int e = (r < dec_mintab[len]) << 6;  // non-canonical encoding
  e |= ((r >> 11) == 0x1b) << 7; // surrogate half?
  e |= (r > RuneMax) << 8;       // out of range?
  e |= (s[1] & 0xc0) >> 2;
  e |= (s[2] & 0xc0) >> 4;
  e |= (s[3]       ) >> 6;
  e ^= 0x2a; // top two bits of each tail byte correct?
  e >>= dec_shiftetab[len];

  return !e;
}


bool utf8_encode(u8** dst, const u8* dstend, Rune r) {
  bool ok = true;
  usize n = 4;
  if      (r < 0x80)      n = 1;
  else if (r < 0x800)     n = 2;
  else if (r < 0x10000) { n = 3; ok = (r < 0xD800 || r > 0xDFFF); }
  else if UNLIKELY(r > RuneMax) { r = RuneSub; n = 3; ok = false; }

  u8* p = *dst + n;
  if UNLIKELY(p > dstend)
    return false;
  *dst = p;

  switch (n) {
    case 4: *--p = (u8)((r | (Rune)0x80) & (Rune)0xBF); r >>= 6; FALLTHROUGH;
    case 3: *--p = (u8)((r | (Rune)0x80) & (Rune)0xBF); r >>= 6; FALLTHROUGH;
    case 2: *--p = (u8)((r | (Rune)0x80) & (Rune)0xBF); r >>= 6; FALLTHROUGH;
    case 1: *--p = (u8) (r | first_byte_mark[n]);
  }

  return ok;
}


usize utf8_len(const u8* s, usize len, UnicodeLenFlags flags) {
  usize count = 0;
  const u8* end = s + len;

  if LIKELY((flags & UC_LFL_SKIP_ANSI) == 0) {
    // Note: this can be done MUCH faster for large inputs with SIMD.
    // See https://github.com/simdutf/simdutf/blob/4f6d4c6a89e3fba1fb6/src/generic/utf8.h#L10
    // -65 is 0b10111111, anything larger in two-complement's should start a new code point.
    while (s < end)
      count += ((i8)*s++ > (i8)-65);
    return count;
  }

  AEscParser ap = aesc_mkparser(AESC_DEFAULT_ATTR);
  Rune r;
  while (s < end && utf8_decode(&s, end, &r)) {
    if (r == '\x1B') {
      for (s--;;) { // scan past the ANSI escape sequence
        AEscParseState ps = aesc_parsec(&ap, *s++);
        if ((ps && *s != '\x1B') || s == end)
          break;
      }
    } else {
      count++;
    }
  }
  return count;
}


usize utf8_printlen(const u8* s, usize len, UnicodeLenFlags flags) {
  usize count = 0;
  const u8* end = s + len;
  Rune r;

  if LIKELY((flags & UC_LFL_SKIP_ANSI) == 0) {
    while (s < end && utf8_decode(&s, end, &r))
      count += r >= RuneSelf || ascii_isprint(r);
    return count;
  }

  AEscParser ap = aesc_mkparser(AESC_DEFAULT_ATTR);
  while (s < end && utf8_decode(&s, end, &r)) {
    if (r >= RuneSelf) {
      count++;
    } else if (r == '\x1B') {
      for (s--;;) { // scan past the ANSI escape sequence
        AEscParseState ps = aesc_parsec(&ap, *s++);
        if ((ps && *s != '\x1B') || s == end)
          break;
      }
    } else {
      count += ascii_isprint(r);
    }
  }
  return count;
}


const u8* nullable utf8_validate(const u8* src, usize len) {
  const u8* s = src;
  const u8* end = s + len;
  Rune r;
  bool ok = true;
  usize q = len >> 2; // number of quads at s

  while (q) {
    ok &= utf8_decode4(&s, &r);
    q--;
  }
  while (s < end)
    ok &= utf8_decode(&s, end, &r);

  if LIKELY(ok)
    return NULL;

  // slow path: find offending byte offset
  s = src;
  while (s < end) {
    const u8* p = s;
    if (!utf8_decode(&s, end, &r))
      return p;
  }
  UNREACHABLE;
  return src;
}
