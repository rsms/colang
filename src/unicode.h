// Unicode text
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef i32 Rune;

static const Rune RuneErr  = 0xFFFD; // Unicode replacement character
static const Rune RuneSelf = 0x80;
  // characters below RuneSelf are represented as themselves in a single byte.
#define UTF8Max ((Rune)4) // Maximum number of bytes of a UTF8-encoded char.

#define ascii_isalpha(c)    ( ((u32)(c) | 32) - 'a' < 26 )                  /* A-Za-z */
#define ascii_isdigit(c)    ( ((u32)(c) - '0') < 10 )                       /* 0-9 */
#define ascii_isupper(c)    ( ((u32)(c) - 'A') < 26 )                       /* A-Z */
#define ascii_islower(c)    ( ((u32)(c) - 'a') < 26 )                       /* a-z */
#define ascii_isprint(c)    ( ((u32)(c) - 0x20) < 0x5f )                    /* SP-~ */
#define ascii_isgraph(c)    ( ((u32)(c) - 0x21) < 0x5e )                    /* !-~ */
#define ascii_isspace(c)    ( (c) == ' ' || (u32)(c) - '\t' < 5 )           /* SP, \{tnvfr} */
#define ascii_ishexdigit(c) ( ascii_isdigit(c) || ((u32)c | 32) - 'a' < 6 ) /* 0-9A-Fa-f */

Rune utf8_decode(const u8* src, usize srclen, u32* width_out);


END_INTERFACE
