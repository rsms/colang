#include "coimpl.h"
#include "tstyle.h"
#include "str.h"
#include "test.h"

#ifdef CO_WITH_LIBC
  #include <unistd.h>  // isatty
  #include <stdlib.h>  // exit
#endif


static struct TStyles t0 = {{0},{0}};

#define PRE "\e["
#define SUF "m"

enum {
  //T4_PREFIX, T4__PREFIX = T4_PREFIX + strlen("\e["), // [WIP]
  #define _(NAME, c4) T4_##NAME, T4__##NAME = T4_##NAME + strlen(PRE c4 SUF),
  DEF_TSTYLE_MODS(_)
  #undef _
  #define _(NAME, c4, c8, ...) T4_##NAME, T4__##NAME = T4_##NAME + strlen(PRE "3" c4 SUF),
  DEF_TSTYLE_COLORS(_)
  #undef _
  #define _(NAME, c4, c8, ...) \
    T4_##NAME##_BG, T4__##NAME##_BG = T4_##NAME##_BG + strlen(PRE "4" c4 SUF),
  DEF_TSTYLE_COLORS(_)
  #undef _
  T4_STRS_SIZE,
};
static struct TStyles t4 = {
  { // offs
    #define _(NAME, ...) T4_##NAME,
    DEF_TSTYLE_MODS(_)
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, ...) T4_##NAME##_BG,
    DEF_TSTYLE_COLORS(_)
    #undef _
  }, { // strs
    //"\e[\0" // T4_PREFIX
    #define _(NAME, c4, ...) PRE c4 SUF "\0"
    DEF_TSTYLE_MODS(_)
    #undef _
    #define _(NAME, c4, ...) PRE "3" c4 SUF "\0"
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, c4, ...) PRE "4" c4 SUF "\0"
    DEF_TSTYLE_COLORS(_)
    #undef _
  }
};

enum {
  #define _(NAME, c4) T8_##NAME, T8__##NAME = T8_##NAME + strlen(PRE c4 SUF),
  DEF_TSTYLE_MODS(_)
  #undef _
  #define _(NAME, c4, c8, ...) T8_##NAME, T8__##NAME = T8_##NAME + strlen(PRE "3" c8 SUF),
  DEF_TSTYLE_COLORS(_)
  #undef _
  #define _(NAME, c4, c8, ...) \
    T8_##NAME##_BG, T8__##NAME##_BG = T8_##NAME##_BG + strlen(PRE "4" c8 SUF),
  DEF_TSTYLE_COLORS(_)
  #undef _
  T8_STRS_SIZE,
};
static struct TStyles t8 = {
  { // offs
    #define _(NAME, ...) T8_##NAME,
    DEF_TSTYLE_MODS(_)
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, ...) T8_##NAME##_BG,
    DEF_TSTYLE_COLORS(_)
    #undef _
  }, { // strs
    #define _(NAME, c4, ...) PRE c4 SUF "\0"
    DEF_TSTYLE_MODS(_)
    #undef _
    #define _(NAME, c4, c8, ...) PRE "3" c8 SUF "\0"
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, c4, c8, ...) PRE "4" c8 SUF "\0"
    DEF_TSTYLE_COLORS(_)
    #undef _
  }
};

// 24-bit RGB colors
// #define DEF_TSTYLE_COLORS(_) \
//   /* Name        8    256       24-bit */ \
//   _(BLACK,      "0",  "0",       "0") \
//   _(RED,        "1",  "8;5;203", "8;2;241;76;76") \
//   _(GREEN,      "2",  "8;5;84",  "8;2;35;209;139") \
//   _(LIGHTGREEN, "2",  "8;5;115", "8;2;35;209;139") \
//   _(YELLOW,     "3",  "8;5;227", "8;2;245;245;67") \
//   _(LIGHTYELLOW,"3",  "8;5;229", "8;2;245;245;67") \
//   _(ORANGE,     "3",  "8;5;208", "8;2;59;142;234") \
//   _(BLUE,       "4",  "8;5;39",  "8;2;59;142;234") \
//   _(LIGHTBLUE,  "4",  "8;5;117", "8;2;59;142;234") \
//   _(MAGENTA,    "5",  "8;5;170", "8;2;214;112;214") \
//   _(PINK,       "5",  "8;5;211", "8;2;214;112;214") \
//   _(PURPLE,     "5",  "8;5;141", "8;2;214;112;214") \
//   _(CYAN,       "6",  "8;5;51",  "8;2;20;240;240") \
//   _(WHITE,      "7",  "7",       "7") \
// // end DEF_TSTYLE_COLORS
// enum {
//   #define _(NAME, c4) T24_##NAME, T24__##NAME = T24_##NAME + strlen(PRE c4 SUF),
//   DEF_TSTYLE_MODS(_)
//   #undef _
//   #define _(NAME, c4, c8, rgb) T24_##NAME, T24__##NAME = T24_##NAME + strlen(PRE "3" rgb SUF),
//   DEF_TSTYLE_COLORS(_)
//   #undef _
//   #define _(NAME, c4, c8, rgb) \
//     T24_##NAME##_BG, T24__##NAME##_BG = T24_##NAME##_BG + strlen(PRE "4" rgb SUF),
//   DEF_TSTYLE_COLORS(_)
//   #undef _
//   T24_STRS_SIZE,
// };
// static struct TStyles t24 = {
//   { // offs
//     #define _(NAME, ...) T24_##NAME,
//     DEF_TSTYLE_MODS(_)
//     DEF_TSTYLE_COLORS(_)
//     #undef _
//     #define _(NAME, ...) T24_##NAME##_BG,
//     DEF_TSTYLE_COLORS(_)
//     #undef _
//   }, { // strs
//     #define _(NAME, c4, ...) PRE c4 SUF "\0"
//     DEF_TSTYLE_MODS(_)
//     #undef _
//     #define _(NAME, c4, c8, rgb) PRE "3" rgb SUF "\0"
//     DEF_TSTYLE_COLORS(_)
//     #undef _
//     #define _(NAME, c4, c8, rgb) PRE "4" rgb SUF "\0"
//     DEF_TSTYLE_COLORS(_)
//     #undef _
//   }
// };


TStyles TStylesNone() { return &t0; }
TStyles TStyles8() { return &t4; }
TStyles TStyles256() { return &t8; }


TStyles TStylesForTerm() {
  static TStyles t = NULL;
  if (t != NULL)
    return t;
  t = &t0;
  #ifdef CO_WITH_LIBC
    const char* TERM = getenv("TERM");
    if (!TERM)
      return t;
    if (strstr(TERM, "xterm") || strstr(TERM, "256color") != NULL) {
      t = &t8;
    } else if (strstr(TERM, "screen") || strstr(TERM, "vt100")) {
      t = &t4;
    }
  #endif
  return t;
}

TStyles TStylesForStdout() {
  static TStyles t = NULL;
  if (t != NULL)
    return t;
  #ifdef CO_WITH_LIBC
    t = isatty(STDOUT_FILENO) ? TStylesForTerm() : &t0;
  #else
    t = &t0;
  #endif
  return t;
}

TStyles TStylesForStderr() {
  static TStyles t = NULL;
  if (t != NULL)
    return t;
  #ifdef CO_WITH_LIBC
    t = isatty(STDERR_FILENO) ? TStylesForTerm() : &t0;
  #else
    t = &t0;
  #endif
  return t;
}


// --------------------------------------------------------------------------
// tests

#ifdef CO_TESTING_ENABLED

UNUSED static void tstyle_demo() {
  TStyles all_styles[] = { &t0, &t4, &t8 };
  for (usize ti = 0; ti < countof(all_styles); ti++) {
    printf("\n");
    TStyles styles = all_styles[ti];
    const char* reset = TStyleStr(styles, TS_RESET);
    int i;

    i = 0;
    #define _(NAME, ...)                                              \
      if (i++) {                                                      \
        if (i > 2) putc(' ', stdout);                                 \
        printf("%s%s%s", TStyleStr(styles, TS_##NAME), #NAME, reset); \
      }
    DEF_TSTYLE_MODS(_)
    #undef _
    printf(" %sWHITE%s", TStyleStr(styles, TS_WHITE), reset);
    printf(" %sBLACK%s", TStyleStr(styles, TS_BLACK), reset);
    printf(" %sWHITE%s", TStyleStr(styles, TS_WHITE_BG), reset);
    printf(" %sBLACK%s", TStyleStr(styles, TS_BLACK_BG), reset);
    printf("\n");

    i = 0;
    int w = 6; // max name length (clips)
    #define _(NAME, ...)                                    \
      if (TS_##NAME != TS_WHITE && TS_##NAME != TS_BLACK) { \
        if (i++) putc(' ', stdout);                         \
        printf("%s%-*.*s%s", TStyleStr(styles, TS_##NAME),  \
          w, MIN(w,(int)strlen(#NAME)), #NAME, reset);      \
      }
    DEF_TSTYLE_COLORS(_)
    printf("\n");
    #undef _

    i = 0;
    #define _(NAME, ...)                                        \
      if (TS_##NAME != TS_WHITE && TS_##NAME != TS_BLACK) {     \
        if (i++) putc(' ', stdout);                             \
        printf("%s%-*.*s%s", TStyleStr(styles, TS_##NAME##_BG), \
          w, MIN(w,(int)strlen(#NAME)), #NAME, reset);          \
      }
    DEF_TSTYLE_COLORS(_)
    printf("\n");
    #undef _
  }
}


DEF_TEST(tstyle) {
  // char tmp[512];

  usize t4_size = sizeof(t4) + T4_STRS_SIZE;
  usize t8_size = sizeof(t8) + T8_STRS_SIZE;
  // dlog("t4  %p .. %p (%zu)", &t4, &t4 + t4_size, t4_size);
  // dlog("t8  %p .. %p (%zu)", &t8, &t8 + t8_size, t8_size);
  // strrepr(tmp, sizeof(tmp), t4.strs, T4_STRS_SIZE);
  // dlog("t4.strs: \"%s\"", tmp);
  // strrepr(tmp, sizeof(tmp), t8.strs, T8_STRS_SIZE);
  // dlog("t8.strs: \"%s\"", tmp);

  const char* green4 = &t4.strs[t4.offs[TS_GREEN]];
  const char* green8 = &t8.strs[t8.offs[TS_GREEN]];

  // make sure the compiler placed strs inside table
  assert((uintptr)&t4 <= (uintptr)green4);
  assert((uintptr)&t4 + t4_size > (uintptr)green4);

  assert((uintptr)&t8 <= (uintptr)green8);
  assert((uintptr)&t8 + t8_size > (uintptr)green8);

  // const char* reset = TStyleStr(&t4, TS_RESET);
  // strrepr(tmp, sizeof(tmp), green4, strlen(green4));
  // dlog("t4.strs[t4.offs[TS_GREEN]]: \"%s\" %stest%s", tmp, green4, reset);
  // strrepr(tmp, sizeof(tmp), green8, strlen(green8));
  // dlog("t8.strs[t8.offs[TS_GREEN]]: \"%s\" %stest%s", tmp, green8, reset);

  tstyle_demo();
}

#endif // CO_TESTING_ENABLED



// -------------------------------------------------------------------
#if 0

void TStyleStackInit(TStyleStack* sstack, Mem mem, TStyles styles) {
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
    s = style_append(sstack, s, sstack->styles[TStyle_fg_default]);
  if (nobg)
    s = style_append(sstack, s, sstack->styles[TStyle_bg_default]);
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

#endif
