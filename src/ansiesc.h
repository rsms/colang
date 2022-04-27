// ANSI escape codes
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// 3-bit colors
typedef u8 ANSIColor;
enum ANSIColor {
  ANSI_COLOR_BLACK   = 0,
  ANSI_COLOR_RED     = 1,
  ANSI_COLOR_GREEN   = 2,
  ANSI_COLOR_YELLOW  = 3,
  ANSI_COLOR_BLUE    = 4,
  ANSI_COLOR_MAGENTA = 5,
  ANSI_COLOR_CYAN    = 6,
  ANSI_COLOR_WHITE   = 7,
} END_ENUM(ANSIColor)

typedef struct AEscAttr {
  union {
    struct { ANSIColor fg8; bool fg8bright; };
    u8 fg256;
    u8 fgrgb[3];
  };
  union {
    struct { ANSIColor bg8; bool bg8bright; };
    u8 bg256;
    u8 bgrgb[3];
  };
  u8 fgtype    : 2; // 0 = color8, 1 = color256, 2 = rgb, 3 = user type
  u8 bgtype    : 2; // 0 = color8, 1 = color256, 2 = rgb, 3 = user type
  u8 _reserved : 4;
  // flags
  u8 bold      : 1;
  u8 dim       : 1;
  u8 italic    : 1;
  u8 underline : 1;
  u8 inverse   : 1;
  u8 blink     : 1;
  u8 hidden    : 1;
  u8 strike    : 1;
} AEscAttr; static_assert(sizeof(AEscAttr) == 8, "");

typedef u8 AEscParseState;
enum AEscParseState {
  AESC_P_MORE, // Just waiting for input
  AESC_P_NONE, // Input was not an ANSI escape sequence
  AESC_P_ATTR, // Parsed an attribute, now available at p.attr
} END_ENUM(AEscParseState)

typedef struct AEscParser {
  AEscAttr attr;        // current attributes
  AEscAttr defaultattr; // default attributes (used when style is reset e.g. \e[0m)
  union {
    AEscAttr nextattr; // partial attributes, currently being parsed
    u8       nab;      // most significant byte of nextattr containing colors
  };
  u8  _reserved;
  u8  pstate[4]; // parse state stack
  u8* int8p;     // pointer to an integer being parsed
} AEscParser;


// aesc_mkparser returns a parser initialized with default attributes
static AEscParser aesc_mkparser(AEscAttr defaultattr);

// aesc_parser_init initializes a parser with initial & default attributes.
// After this call, the parser state is expecting an ESC byte to begin a sequence.
static void aesc_parser_init(AEscParser* p, AEscAttr defaultattr);

// aesc_parsec parses the next byte of input
AEscParseState aesc_parsec(AEscParser* p, char c);

// AESC_DEFAULT_ATTR is the default attribute
#define AESC_DEFAULT_ATTR ((AEscAttr){.fgrgb={ANSI_COLOR_WHITE},.bgrgb={0}})

inline static bool aesc_attr_eq(const AEscAttr* a, const AEscAttr* b) {
  return *(u64*)a == *(u64*)b;
}
inline static bool aesc_attr_colors_eq(const AEscAttr* a, const AEscAttr* b) {
  return ((u32*)a)[0] == ((u32*)b)[0];
}
inline static bool aesc_attr_flags_eq(const AEscAttr* a, const AEscAttr* b) {
  return ((u32*)a)[1] == ((u32*)b)[1];
}

// u8 AESC_ATTR_FG8(const AEscAttr*)
// u8 AESC_ATTR_BG8(const AEscAttr*)
//   Returns a 3-bit color including its bright bit in one value:
//       ╭───────────────┬───┬───────────╮
//   Bit │ 7   6   5   4 │ 3 │ 2   1   0 │
//       ╰───────┬───────┴─┬─┴─────┬─────╯
//               │         │       ╰── color
//             unused      ╰────────── bright bit
//
//   This extends the meaning of the value and allows lookups like this:
//     0x0 => black     0x8 => bright black
//     0x1 => red       0x9 => bright red
//     0x2 => green     0xA => bright green
//     0x3 => yellow    0xB => bright yellow
//     0x4 => blue      0xC => bright blue
//     0x5 => magenta   0xD => bright magenta
//     0x6 => cyan      0xE => bright cyan
//     0x7 => white     0xF => bright white
//
#define AESC_ATTR_FG8(attrptr) ((u8)( ((attrptr)->fg8 & 7) | ((attrptr)->fg8bright << 3) ))
#define AESC_ATTR_BG8(attrptr) ((u8)( ((attrptr)->bg8 & 7) | ((attrptr)->bg8bright << 3) ))

// u8 AESC_ATTR_COLORS8(const AEscAttr*)
//   Returns a text-mode style byte representing 3-bit fg & bg colors:
//       ╭───┬───────────┬───┬───────────╮
//   Bit │ 7 │ 6   5   4 │ 3 │ 2   1   0 │
//       ╰─┬─┴─────┬─────┴─┬─┴─────┬─────╯
//         │       │       │       ╰── foreground color
//         │       │       ╰────────── foreground bright bit
//         │       ╰────────────────── background color
//         ╰────────────────────────── background bright bit (OR blink)
//
#define AESC_ATTR_COLORS8(attrptr) \
  (AESC_FG_COLOR8(attrptr) | (AESC_BG_COLOR8(attrptr) << 4))
// TODO: #if !CO_LITTLE_ENDIAN


//———————————————————————————————————————————————————————————————————————————————————————
// internal

inline static AEscParser aesc_mkparser(AEscAttr defaultattr) {
  return (AEscParser){
    .attr        = defaultattr,
    .defaultattr = defaultattr,
  };
}

inline static void aesc_parser_init(AEscParser* p, AEscAttr defaultattr) {
  memset(p, 0, sizeof(*p));
  p->attr = defaultattr;
  p->defaultattr = defaultattr;
}

END_INTERFACE
