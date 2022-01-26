// TStyle -- TTY terminal ANSI styling
#pragma once
#include "mem.h"
#include "array.h"
#include "str.h"
ASSUME_NONNULL_BEGIN

#define TSTYLE_STYLES(_) \
  /* Name               16       RGB */ \
  _(none,        "0",     "0") \
  _(nocolor,     "39",    "39") \
  _(defaultfg,   "39",    "39") \
  _(defaultbg,   "49",    "49") \
  _(bold,        "1",     "1") \
  _(dim,         "2",     "2") \
  _(nodim,       "22",    "22") \
  _(italic,      "3",     "3") \
  _(underline,   "4",     "4") \
  _(inverse,     "7",     "7") \
  _(white,       "37",    "38;2;255;255;255") \
  _(grey,        "90",    "38;5;244") \
  _(black,       "30",    "38;5;16") \
  _(blue,        "94",    "38;5;75") \
  _(lightblue,   "94",    "38;5;117") \
  _(cyan,        "96",    "38;5;87") \
  _(green,       "92",    "38;5;84") \
  _(lightgreen,  "92",    "38;5;157") \
  _(magenta,     "95",    "38;5;213") \
  _(purple,      "35",    "38;5;141") \
  _(lightpurple, "35",    "38;5;183") \
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

extern const char* nonull TStyle16[_TStyle_MAX];
extern const char* nonull TStyleRGB[_TStyle_MAX];
extern const char* nonull TStyleNone[_TStyle_MAX];

typedef const char** TStyleTable;

bool TSTyleStdoutIsTTY();
bool TSTyleStderrIsTTY();
TStyleTable TSTyleForTerm();   // best for the current terminal
TStyleTable TSTyleForStdout(); // best for the current terminal on stdout
TStyleTable TSTyleForStderr(); // best for the current terminal on stderr

typedef struct TStyleStack {
  Mem         mem;
  TStyleTable styles;
  CStrArray   stack;
  const char* stack_storage[4];
  u32         nbyteswritten;
} TStyleStack;

void TStyleStackInit(TStyleStack* sstack, Mem mem, TStyleTable styles);
void TStyleStackDispose(TStyleStack* sstack);
Str TStyleStackPush(TStyleStack* sstack, Str s, TStyle style);
Str TStyleStackPop(TStyleStack* sstack, Str s);

ASSUME_NONNULL_END
