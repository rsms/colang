// Unicode text
// SPDX-License-Identifier: Apache-2.0
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define UNICODE_IMPLEMENTATION
#endif
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

typedef i32 Rune;

static const Rune RuneErr  = 0xFFFD; // Unicode replacement character
static const Rune RuneSelf = 0x80;
  // characters below RuneSelf are represented as themselves in a single byte.
static const u32 UTF8Max = 4; // Maximum number of bytes of a UTF8-encoded char.

#define ascii_isalpha(c)    ( ((u32)(c) | 32) - 'a' < 26 )                  /* A-Za-z */
#define ascii_isdigit(c)    ( ((u32)(c) - '0') < 10 )                       /* 0-9 */
#define ascii_isupper(c)    ( ((u32)(c) - 'A') < 26 )                       /* A-Z */
#define ascii_islower(c)    ( ((u32)(c) - 'a') < 26 )                       /* a-z */
#define ascii_isprint(c)    ( ((u32)(c) - 0x20) < 0x5f )                    /* SP-~ */
#define ascii_isgraph(c)    ( ((u32)(c) - 0x21) < 0x5e )                    /* !-~ */
#define ascii_isspace(c)    ( (c) == ' ' || (u32)(c) - '\t' < 5 )           /* SP, \{tnvfr} */
#define ascii_ishexdigit(c) ( ascii_isdigit(c) || ((u32)c | 32) - 'a' < 6 ) /* 0-9A-Fa-f */

Rune utf8_decode(const u8* src, usize srclen, u32* width_out);

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef UNICODE_IMPLEMENTATION

#include "coimpl.h"
#include "test.c"

Rune utf8_decode(const u8* buf, usize len, u32* width_out) {
  u8 b = *buf;
  if (b < RuneSelf) {
    *width_out = 1;
    return b;
  }
  if ((b >> 5) == 0x6) {
    *width_out = 2;
    return len < 2 ? RuneErr
                   : ((b << 6) & 0x7ff) +
                     ((buf[1]) & 0x3f);
  } else if ((b >> 4) == 0xE) {
    *width_out = 3;
    return len < 3 ? RuneErr
                  : ((b << 12) & 0xffff) +
                    ((buf[1] << 6) & 0xfff) +
                    ((buf[2]) & 0x3f);
  } else if ((b >> 3) == 0x1E) {
    *width_out = 4;
    return len < 4 ? RuneErr
                   : ((b << 18) & 0x1fffff) +
                     ((buf[1] << 12) & 0x3ffff) +
                     ((buf[2] << 6) & 0xfff) +
                     ((buf[3]) & 0x3f);
  }
  *width_out = 1;
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

#endif // UNICODE_IMPLEMENTATION
