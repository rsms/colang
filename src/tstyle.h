// terminal ANSI styling
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef u8                    TStyle;      // describes a style like "bold" or "red"
typedef const struct TStyles* TStyles;     // table of styles
typedef struct TStyleStack    TStyleStack; // support for undo and easy range styling

#define DEF_TSTYLE_MODS(_) \
  _(TS_RESET,       "0") \
  _(TS_BOLD,        "1") \
  _(TS_DIM,         "2") \
  _(TS_ITALIC,      "3") \
  _(TS_UNDERLINE,   "4") \
  _(TS_NOBOLD,      "22") /* 21 is double underline, ugh */ \
  _(TS_NODIM,       "22") \
  _(TS_NOITALIC,    "23") \
  _(TS_NOUNDERLINE, "24") \
  _(TS_DEFAULT_FG,  "39") \
  _(TS_DEFAULT_BG,  "49") \
// end DEF_TSTYLE_MODS
#define DEF_TSTYLE_COLORS(_) \
  /* Name         8    256       24-bit */ \
  _(TS_BLACK,       "0",  "0") /* "BLACK" MUST BE FIRST MEMBER */ \
  _(TS_DARKGREY,    "0",  "8;5;237") \
  _(TS_LIGHTGREY,   "7",  "8;5;248") \
  _(TS_RED,         "1",  "8;5;203") \
  _(TS_GREEN,       "2",  "8;5;84") \
  _(TS_LIGHTGREEN,  "2",  "8;5;115") \
  _(TS_YELLOW,      "3",  "8;5;227") \
  _(TS_LIGHTYELLOW, "3",  "8;5;229") \
  _(TS_ORANGE,      "3",  "8;5;208") \
  _(TS_LIGHTORANGE, "3",  "8;5;215") \
  _(TS_BLUE,        "4",  "8;5;39") \
  _(TS_LIGHTBLUE,   "4",  "8;5;117") \
  _(TS_DARKBLUE,    "4",  "8;5;18") \
  _(TS_MAGENTA,     "5",  "8;5;170") \
  _(TS_PINK,        "5",  "8;5;211") \
  _(TS_PURPLE,      "5",  "8;5;141") \
  _(TS_LIGHTPURPLE, "5",  "8;5;147") \
  _(TS_CYAN,        "6",  "8;5;51") \
  _(TS_WHITE,       "7",  "7") /* "WHITE" MUST BE LAST MEMBER */ \
// end DEF_TSTYLE_COLORS
#define DEF_TSTYLE_INTERNALS(_) \
  _(_TS_NONE,       "") \
// end DEF_TSTYLE_INTERNALS

enum TStyle {
  #define _(NAME, ...) NAME,
  DEF_TSTYLE_MODS(_)
  DEF_TSTYLE_COLORS(_)
  #undef _
  #define _(NAME, ...) NAME##_BG,
  DEF_TSTYLE_COLORS(_)
  #undef _
  #define _(NAME, ...) NAME,
  DEF_TSTYLE_INTERNALS(_)
  #undef _
  _TS_MAX,
  _TS_FGCOLOR_START = TS_BLACK,
  _TS_FGCOLOR_END = TS_WHITE+1,
  _TS_BGCOLOR_START = TS_BLACK_BG,
  _TS_BGCOLOR_END = TS_WHITE_BG+1,
} END_ENUM(TStyle)

struct TStyles {
  u16  offs[_TS_MAX]; // index style constant => strs offset
  char strs[];
};

struct TStyleStack {
  TStyles  styles;
  char     buf[64];
  AEscAttr stack[32];
  u32      stack_len;
};

TStyles TStylesForTerm();   // best for the current terminal
TStyles TStylesForStdout(); // best for the current terminal on stdout
TStyles TStylesForStderr(); // best for the current terminal on stderr
TStyles TStyles16();        // 4-bit color codes (16 colors)
TStyles TStyles256();       // 8-bit color codes (256 colors)
TStyles TStylesNone();      // no styles/colors

// tstyle_pushv records styles as a change to the current logical style.
// Returns a null-terminated string which when printed enables the new styles
// on top of any current styling.
const char* tstyle_pushv(TStyleStack*, const TStyle* stylev, u32 stylec);
inline static const char* tstyle_push(TStyleStack* st, TStyle s) {
  return tstyle_pushv(st, (TStyle[]){s}, 1);
}

// tstyle_pop undoes the most recent tstyle_push.
// Returns a null-terminated string which when printed restores the style to what
// it was prior to the corresponding tstyle_push call.
const char* tstyle_pop(TStyleStack*);

// tstyle_str returns the ANSI escape sequence for s as a null-terminated string
inline static const char* tstyle_str(TStyles t, TStyle s) {
  return &t->strs[t->offs[MIN(s, _TS_MAX-1)]];
}

// TStylesIsNone returns true if t == TStylesNone()
inline static bool TStylesIsNone(TStyles t) { return t->offs[sizeof(void*)]==0; }


END_INTERFACE
