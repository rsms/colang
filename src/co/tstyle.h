#pragma once
#include <stdbool.h>

typedef enum {
  TStyle_none,
  TStyle_noColor,
  TStyle_bold,
  TStyle_italic,
  TStyle_underline,
  TStyle_inverse,
  TStyle_white,
  TStyle_grey,
  TStyle_black,
  TStyle_blue,
  TStyle_cyan,
  TStyle_green,
  TStyle_magenta,
  TStyle_purple,
  TStyle_pink,
  TStyle_red,
  TStyle_yellow,
  TStyle_lightyellow,
  TStyle_orange,
  _TStyle_MAX,
} TStyle;

extern const char* TStyleTable[_TStyle_MAX];

bool TSTyleStdoutIsTTY();
bool TSTyleStderrIsTTY();
