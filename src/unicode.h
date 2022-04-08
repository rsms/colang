// Unicode text
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef u32 Rune; // A Unicode codepoint (UTF-32)
typedef u8 UnicodeLenFlags; enum {
  UC_LFL_SKIP_ANSI = 1 << 0, // skip past ANSI escape sequences
};

#define RuneSub  ((Rune)0xFFFD)   // Unicode replacement character
#define RuneMax  ((Rune)0x10FFFF) // Max Unicode codepoint
#define RuneSelf ((Rune)0x80)     // Runes below this are represented as a single byte
#define UTF8Max  ((Rune)4)        // Maximum number of bytes of a UTF8-encoded char

#define ascii_isalpha(c)    ( ((u32)(c) | 32) - 'a' < 26 )                  /* A-Za-z */
#define ascii_isdigit(c)    ( ((u32)(c) - '0') < 10 )                       /* 0-9 */
#define ascii_isupper(c)    ( ((u32)(c) - 'A') < 26 )                       /* A-Z */
#define ascii_islower(c)    ( ((u32)(c) - 'a') < 26 )                       /* a-z */
#define ascii_isprint(c)    ( ((u32)(c) - 0x20) < 0x5f )                    /* SP-~ */
#define ascii_isgraph(c)    ( ((u32)(c) - 0x21) < 0x5e )                    /* !-~ */
#define ascii_isspace(c)    ( (c) == ' ' || (u32)(c) - '\t' < 5 )           /* SP,\{tnvfr} */
#define ascii_ishexdigit(c) ( ascii_isdigit(c) || ((u32)c | 32) - 'a' < 6 ) /* 0-9A-Fa-f */

// utf8_encode writes to *dst the UTF-8 representation of r, advancing *dst by at least one.
// If r is an invalid Unicode codepoint (i.e. r>RuneMax) RuneSub is used instead.
// Returns false if there's not enough space at *dst.
bool utf8_encode(u8** dst, const u8* dstend, Rune r);

// utf8_decode validates and decodes the next codepoint at *src.
// Required precondition: *src < end (input is not empty.)
// Always advances *src by at least 1 byte.
// If src is a partial valid sequence (underflow), *src is set to end and false is returned.
// Returns false if *src contains invalid UTF-8 data; if so, caller should use RuneSub.
bool utf8_decode(const u8** src, const u8* end, Rune* result);

// utf8_decode4 is a faster validating decoder that requires src to have 4-byte alignment;
// at least 4 bytes to load at *src. Always advances *src by at least 1 byte.
// Returns false if *src is invalid UTF-8; if so, caller should use RuneSub.
bool utf8_decode4(const u8** src, Rune* result);

// utf8_len returns the number of unicode codepoints at s.
// Assumes input is valid UTF-8. For input that might contain invalid UTF-8 data,
// it's better to count the codepoinst using utf8_decode.
usize utf8_len(const u8* s, usize len, UnicodeLenFlags);

// utf8_printlen returns the number of printable unicode codepoints at s
usize utf8_printlen(const u8* s, usize len, UnicodeLenFlags);

// utf8_isvalid returns true if s contains a valid UTF-8 sequence
bool utf8_isvalid(const u8* s, usize len);

END_INTERFACE
