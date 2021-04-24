#include "tstyle.h"
#include <unistd.h>  // for isatty()

const char* TStyle16[_TStyle_MAX] = {
  #define I_ENUM(name, c16, cRGB) "\x1b[" c16 "m",
  TSTYLE_STYLES(I_ENUM)
  #undef I_ENUM
};

const char* TStyleRGB[_TStyle_MAX] = {
  #define I_ENUM(name, c16, cRGB) "\x1b[" cRGB "m",
  TSTYLE_STYLES(I_ENUM)
  #undef I_ENUM
};

const char* TStyleNone[_TStyle_MAX] = {
  #define I_ENUM(name, c16, cRGB) "",
  TSTYLE_STYLES(I_ENUM)
  #undef I_ENUM
};


static int _TSTyleStdoutIsTTY = -1;
static int _TSTyleStderrIsTTY = -1;

// STDIN  = 0
// STDOUT = 1
// STDERR = 2

bool TSTyleStdoutIsTTY() {
  if (_TSTyleStdoutIsTTY == -1)
    _TSTyleStdoutIsTTY = isatty(1) ? 1 : 0;
  return !!_TSTyleStdoutIsTTY;
}

bool TSTyleStderrIsTTY() {
  if (_TSTyleStderrIsTTY == -1)
    _TSTyleStderrIsTTY = isatty(2) ? 1 : 0;
  return !!_TSTyleStderrIsTTY;
}

// #define I_ENUM(name, c16, cRGB) Str str_tstyle_##name(Str s);
// TSTYLE_STYLES(I_ENUM)
// #undef I_ENUM
// // Str str_tstyle_##name(Str s) { return sdscat(s, TStyle[TStyle_bold]); }
