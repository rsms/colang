// Unicode
#pragma once
ASSUME_NONNULL_BEGIN

typedef i32 Rune;

static const Rune RuneErr  = 0xFFFD; // Unicode replacement character
static const Rune RuneSelf = 0x80;
  // characters below RuneSelf are represented as themselves in a single byte.
static const u32 UTF8Max = 4; // Maximum number of bytes of a UTF8-encoded char.

Rune utf8_decode(const u8* src, usize srclen, u32* width_out);

ASSUME_NONNULL_END
