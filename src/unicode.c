// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


Rune utf8_decode(const u8** cursor, const u8* end) {
  assertf(*cursor != end, "empty input");
  const u8* s = *cursor;
  int k = __builtin_clz(~((u32)*s << 24));
  assert(k <= 8);
  Rune mask = (1 << (8 - k)) - 1;
  Rune r = *s & mask;
  for (++s, --k; k > 0 && s != end; --k, ++s) {
    r <<= 6;
    r += (*s & 0x3F);
  }
  *cursor = s;
  // TODO: RuneErr on invalid input
  return r;
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
  while (s < end) {
    Rune r = utf8_decode(&s, end);
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

  if LIKELY((flags & UC_LFL_SKIP_ANSI) == 0) {
    while (s < end) {
      Rune r = utf8_decode(&s, end);
      count += r >= RuneSelf || ascii_isprint(r);
    }
    return count;
  }

  AEscParser ap = aesc_mkparser(AESC_DEFAULT_ATTR);
  while (s < end) {
    Rune r = utf8_decode(&s, end);
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
