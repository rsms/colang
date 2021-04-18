#include "tstyle.h"
#include <unistd.h>  // for isatty()


const char* TStyleTable[_TStyle_MAX] = {
  "\x1b[0m",  // TStyle_none
  "\x1b[39m", // TStyle_noColor
  "\x1b[1m",  // TStyle_bold         // : sfn('1',  '1', '22'),
  "\x1b[3m",  // TStyle_italic       // : sfn('3',  '3', '23'),
  "\x1b[4m",  // TStyle_underline    // : sfn('4',  '4', '24'),
  "\x1b[7m",  // TStyle_inverse      // : sfn('7',  '7', '27'),
  "\x1b[37m", // TStyle_white        // : sfn('37', '38;2;255;255;255', '39'),
  "\x1b[90m", // TStyle_grey         // : sfn('90', '38;5;244', '39'),
  "\x1b[30m", // TStyle_black        // : sfn('30', '38;5;16', '39'),
  "\x1b[94m", // TStyle_blue         // : sfn('34', '38;5;75', '39'),
  "\x1b[96m", // TStyle_cyan         // : sfn('36', '38;5;87', '39'),
  "\x1b[92m", // TStyle_green        // : sfn('32', '38;5;84', '39'),
  "\x1b[95m", // TStyle_magenta      // : sfn('35', '38;5;213', '39'),
  "\x1b[35m", // TStyle_purple       // : sfn('35', '38;5;141', '39'),
  "\x1b[35m", // TStyle_pink         // : sfn('35', '38;5;211', '39'),
  "\x1b[91m", // TStyle_red          // : sfn('31', '38;2;255;110;80', '39'),
  "\x1b[33m", // TStyle_yellow       // : sfn('33', '38;5;227', '39'),
  "\x1b[93m", // TStyle_lightyellow  // : sfn('93', '38;5;229', '39'),
  "\x1b[33m", // TStyle_orange       // : sfn('33', '38;5;215', '39'),
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
    _TSTyleStderrIsTTY = isatty(1) ? 1 : 0;
  return !!_TSTyleStderrIsTTY;
}
