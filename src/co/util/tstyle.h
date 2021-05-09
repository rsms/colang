#pragma once
#include <stdbool.h>

#define TSTYLE_STYLES(_) \
  /* Name               16       RGB */ \
  _(none,        "0",     "0") \
  _(nocolor,     "39",    "39") \
  _(defaultfg,   "39",    "39") \
  _(defaultbg,   "49",    "49") \
  _(bold,        "1",     "1") \
  _(dim,         "2",     "2") \
  _(italic,      "3",     "3") \
  _(underline,   "4",     "4") \
  _(inverse,     "7",     "7") \
  _(white,       "37",    "38;2;255;255;255") \
  _(grey,        "90",    "38;5;244") \
  _(black,       "30",    "38;5;16") \
  _(blue,        "94",    "38;5;75") \
  _(cyan,        "96",    "38;5;87") \
  _(green,       "92",    "38;5;84") \
  _(magenta,     "95",    "38;5;213") \
  _(purple,      "35",    "38;5;141") \
  _(pink,        "35",    "38;5;211") \
  _(red,         "91",    "38;2;255;110;80") \
  _(yellow,      "33",    "38;5;227") \
  _(lightyellow, "93",    "38;5;229") \
  _(orange,      "33",    "38;5;215") \
/*END DEF_NODE_KINDS*/

// TStyle_red, TStyle_bold, etc.
typedef enum {
  #define I_ENUM(name, c16, cRGB) TStyle_##name,
  TSTYLE_STYLES(I_ENUM)
  #undef I_ENUM
  _TStyle_MAX,
} TStyle;

// // Str functions. I.e. Str str_tstyle_red(Str s) => s with red wrapped around it
// #define I_ENUM(name, c16, cRGB) Str str_tstyle_##name(Str s);
// TSTYLE_STYLES(I_ENUM)
// #undef I_ENUM
// // Str str_tstyle_##name(Str s) { return sdscat(s, TStyleTable[TStyle_bold]); }



extern const char* TStyle16[_TStyle_MAX];
extern const char* TStyleRGB[_TStyle_MAX];
extern const char* TStyleNone[_TStyle_MAX];

typedef const char** TStyleTable;

bool TSTyleStdoutIsTTY();
bool TSTyleStderrIsTTY();
