// TStyle -- terminal ANSI styling
#pragma once
ASSUME_NONNULL_BEGIN

#define DEF_TSTYLE_MODS(_) \
  _(RESET,     "0") \
  _(BOLD,      "1") \
  _(DIM,       "2") \
  _(ITALIC,    "3") \
  _(UNDERLINE, "4") \
// end DEF_TSTYLE_MODS
#define DEF_TSTYLE_COLORS(_) \
  /* Name         8    256       24-bit */ \
  _(BLACK,       "0",  "0") \
  _(RED,         "1",  "8;5;203") \
  _(GREEN,       "2",  "8;5;84") \
  _(LIGHTGREEN,  "2",  "8;5;115") \
  _(YELLOW,      "3",  "8;5;227") \
  _(LIGHTYELLOW, "3",  "8;5;229") \
  _(ORANGE,      "3",  "8;5;208") \
  _(LIGHTORANGE, "3",  "8;5;215") \
  _(BLUE,        "4",  "8;5;39") \
  _(LIGHTBLUE,   "4",  "8;5;117") \
  _(MAGENTA,     "5",  "8;5;170") \
  _(PINK,        "5",  "8;5;211") \
  _(PURPLE,      "5",  "8;5;141") \
  _(CYAN,        "6",  "8;5;51") \
  _(WHITE,       "7",  "7") \
// end DEF_TSTYLE_COLORS

typedef u8 TStyle;
typedef const struct TStyles* TStyles;

enum TStyle {
  #define _(NAME, ...) TS_##NAME,
  DEF_TSTYLE_MODS(_)
  DEF_TSTYLE_COLORS(_)
  #undef _
  #define _(NAME, ...) TS_##NAME##_BG,
  DEF_TSTYLE_COLORS(_)
  #undef _
  _TS_MAX
} END_TYPED_ENUM(TStyle)

struct TStyles {
  u16  offs[_TS_MAX]; // index style constant => strs offset
  char strs[];
};

TStyles TStylesForTerm();   // best for the current terminal
TStyles TStylesForStdout(); // best for the current terminal on stdout
TStyles TStylesForStderr(); // best for the current terminal on stderr
TStyles TStyles8();         // 4-bit 16-color table
TStyles TStyles256();       // 8-bit 256-color table
TStyles TStylesNone();      // no styles or colors

// TStyleStr returns the ANSI escape sequence for s as a null-terminated string
inline static const char* TStyleStr(TStyles t, TStyle s) {
  return &t->strs[t->offs[MIN(s, _TS_MAX-1)]];
}


ASSUME_NONNULL_END
