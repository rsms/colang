// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


Rune utf8_decode(const u8* buf, usize len, u32* width_out) {
  u8 b = *buf;
  if (b < RuneSelf) {
    *width_out = 1;
    return b;
  }
  if ((b >> 5) == 0x6) {
    *width_out = 2;
    return len < 2 ? RuneErr
                   : ((b << 6) & 0x7ff) + ((buf[1]) & 0x3f);
  }
  if ((b >> 4) == 0xE) {
    *width_out = 3;
    return len < 3 ? RuneErr
                  : ((b << 12) & 0xffff) + ((buf[1] << 6) & 0xfff) + ((buf[2]) & 0x3f);
  }
  if ((b >> 3) == 0x1E) {
    *width_out = 4;
    return len < 4 ? RuneErr
                   : ((b << 18) & 0x1fffff) + ((buf[1] << 12) & 0x3ffff) +
                     ((buf[2] << 6) & 0xfff) + ((buf[3]) & 0x3f);
  }
  *width_out = 1; // make naive caller advance in case it doesn't check error
  return RuneErr;
}


DEF_TEST(unicode_ascii_is) {
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