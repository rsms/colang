#include "coimpl.h"
#include "tstyle.h"

#ifdef CO_WITH_LIBC
  #include <unistd.h>  // isatty
#endif

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


#ifdef CO_WITH_LIBC
  bool TSTyleStdoutIsTTY() {
    static int g = -1;
    if (g == -1)
      g = isatty(STDOUT_FILENO) ? 1 : 0;
    return !!g;
  }
  bool TSTyleStderrIsTTY() {
    static int g = -1;
    if (g == -1)
      g = isatty(STDERR_FILENO) ? 1 : 0;
    return !!g;
  }
#else
  bool TSTyleStdoutIsTTY() { return false; }
  bool TSTyleStderrIsTTY() { return false; }
#endif // CO_WITH_LIBC


TStyleTable TSTyleForTerm() {
  static TStyleTable t = NULL;
  if (t == NULL) {
    t = TStyleNone;
    #ifdef CO_WITH_LIBC
      const char* TERM = getenv("TERM");
      if (TERM) {
        if (strstr(TERM, "256color")) {
          t = TStyleRGB;
        } else if (strstr(TERM, "xterm") || strstr(TERM, "screen") || strstr(TERM, "vt100")) {
          t = TStyle16;
        }
      }
    #endif
  }
  return t;
}

TStyleTable TSTyleForStdout() {
  return TSTyleStdoutIsTTY() ? TSTyleForTerm() : TStyleNone;
}

TStyleTable TSTyleForStderr() {
  return TSTyleStdoutIsTTY() ? TSTyleForTerm() : TStyleNone;
}

// ----------------------------------------------------------------------------------------------

void TStyleStackInit(TStyleStack* sstack, Mem mem, TStyleTable styles) {
  sstack->mem = mem;
  CStrArrayInitStorage(&sstack->stack, sstack->stack_storage, countof(sstack->stack_storage));
  assert(styles != NULL);
  sstack->styles = styles;
}

void TStyleStackDispose(TStyleStack* sstack) {
  CStrArrayFree(&sstack->stack, sstack->mem);
}

static Str style_append(TStyleStack* sstack, Str s, const char* suffix) {
  u32 len = (u32)strlen(suffix);
  sstack->nbyteswritten += len;
  return str_appendn(s, suffix, len);
}

static Str style_apply(TStyleStack* sstack, Str s) {
  if (sstack->stack.len == 0)
    return style_append(sstack, s, sstack->styles[TStyle_none]);
  bool nofg = true;
  bool nobg = true;
  s = str_makeroom(s, sstack->stack.len * 8);
  for (u32 i = 0; i < sstack->stack.len; i++) {
    const char* style = (const char*)sstack->stack.v[i];
    s = style_append(sstack, s, style);
    if (style[2] == '3' || style[2] == '9') {
      nofg = false;
    } else if (style[2] == '4') {
      nobg = false;
    }
  }
  if (nofg)
    s = style_append(sstack, s, sstack->styles[TStyle_defaultfg]);
  if (nobg)
    s = style_append(sstack, s, sstack->styles[TStyle_defaultbg]);
  return s;
}

Str TStyleStackPush(TStyleStack* sstack, Str s, TStyle style) {
  if (sstack->styles == TStyleNone)
    return s;
  const char* stylestr = sstack->styles[style];
  CStrArrayPush(&sstack->stack, (void*)stylestr, sstack->mem);
  return style_apply(sstack, s);
}

Str TStyleStackPop(TStyleStack* sstack, Str s) {
  if (sstack->styles == TStyleNone)
    return s;
  CStrArrayPop(&sstack->stack);
  return style_apply(sstack, s);
}
