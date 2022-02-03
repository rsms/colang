#include "coimpl.h"
#include "tstyle.h"

#ifdef CO_TESTING_ENABLED
  #include "sbuf.h"
  #include "str.h"
  #include "test.h"
#endif

#ifdef CO_WITH_LIBC
  #include <unistd.h> // isatty
  #include <stdlib.h> // getenv
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
  #define _(NAME, s) _T4_##NAME, _T4__##NAME = _T4_##NAME + strlen(s),
  DEF_TSTYLE_INTERNALS(_)
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
    #define _(NAME, ...) _T4_##NAME,
    DEF_TSTYLE_INTERNALS(_)
    #undef _
  }, { // strs
    //"\e[\0" // T4_PREFIX
    #define _(NAME, c4) PRE c4 SUF "\0"
    DEF_TSTYLE_MODS(_)
    #undef _
    #define _(NAME, c4, ...) PRE "3" c4 SUF "\0"
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, c4, ...) PRE "4" c4 SUF "\0"
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, s) s "\0"
    DEF_TSTYLE_INTERNALS(_)
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
  #define _(NAME, s) _T8_##NAME, _T8__##NAME = _T8_##NAME + strlen(s),
  DEF_TSTYLE_INTERNALS(_)
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
    #define _(NAME, ...) _T8_##NAME,
    DEF_TSTYLE_INTERNALS(_)
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
    #define _(NAME, s) s "\0"
    DEF_TSTYLE_INTERNALS(_)
    #undef _
  }
};

// 24-bit RGB colors
// #define DEF_TSTYLE_COLORS(_) \
//   _(BLACK,        "0") \
//   _(RED,          "8;2;241;76;76") \
//   _(GREEN,        "8;2;35;209;139") \
//   _(LIGHTGREEN,   "8;2;35;209;139") \
//   _(YELLOW,       "8;2;245;245;67") \
//   _(LIGHTYELLOW,  "8;2;245;245;67") \
//   _(ORANGE,       "8;2;59;142;234") \
//   _(BLUE,         "8;2;59;142;234") \
//   _(LIGHTBLUE,    "8;2;59;142;234") \
//   _(MAGENTA,      "8;2;214;112;214") \
//   _(PINK,         "8;2;214;112;214") \
//   _(PURPLE,       "8;2;214;112;214") \
//   _(CYAN,         "8;2;20;240;240") \
//   _(WHITE,        "7") \
// // end DEF_TSTYLE_COLORS
// enum {
//   #define _(NAME, c4) T24_##NAME, T24__##NAME = T24_##NAME + strlen(PRE c4 SUF),
//   DEF_TSTYLE_MODS(_)
//   #undef _
//   #define _(NAME, c4, c8, rgb) \
//     T24_##NAME, T24__##NAME = T24_##NAME + strlen(PRE "3" rgb SUF),
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
TStyles TStyles16() { return &t4; }
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
    if (strstr(TERM, "xterm") || strstr(TERM, "256color")) {
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


// tstyle_patch relies on TS_DEFAULT_* to have a lesser value than any color
static_assert(TS_DEFAULT_FG < TS_BLACK, "");
static_assert(TS_DEFAULT_BG < TS_BLACK_BG, "");


// tstyle_patch updates s's style state.
// Returns the "inverse" style, which if applied undoes the initial change.
static TStyle tstyle_patch(TStyleStack* s, TStyle style) {
  auto prevcol = s->color;
  switch ((enum TStyle)style) {
    case TS_BOLD:        return (s->bold=1)      ? TS_NOBOLD      : _TS_NONE;
    case TS_DIM:         return (s->dim=1)       ? TS_NODIM       : _TS_NONE;
    case TS_ITALIC:      return (s->italic=1)    ? TS_NOITALIC    : _TS_NONE;
    case TS_UNDERLINE:   return (s->underline=1) ? TS_NOUNDERLINE : _TS_NONE;
    case TS_NOBOLD:      return (s->bold=0)      ? _TS_NONE       : TS_BOLD;
    case TS_NODIM:       return (s->dim=0)       ? _TS_NONE       : TS_DIM;
    case TS_NOITALIC:    return (s->italic=0)    ? _TS_NONE       : TS_ITALIC;
    case TS_NOUNDERLINE: return (s->underline=0) ? _TS_NONE       : TS_UNDERLINE;
    case TS_DEFAULT_FG:
      s->color.fg = 0; // 0 rather than TS_DEFAULT_FG; allows for zero-init'd TStyleStack
      return MAX(prevcol.fg, TS_DEFAULT_FG);
    case TS_DEFAULT_BG:
      s->color.bg = 0;
      return MAX(prevcol.bg, TS_DEFAULT_BG);
    case _TS_FGCOLOR_START ... _TS_FGCOLOR_END-1:
      s->color.fg = style;
      return MAX(prevcol.fg, TS_DEFAULT_FG);
    case _TS_BGCOLOR_START ... _TS_BGCOLOR_END-1:
      s->color.bg = style;
      return MAX(prevcol.bg, TS_DEFAULT_BG);
    case TS_RESET:
    case _TS_MAX:
    case _TS_NONE:
      break;
  }
  assertf(0,"invalid TStyle %d", style);
  return _TS_NONE;
}


TStyle tstyle_push(TStyleStack* s, TStyle style) {
  TStyle inverse_style = tstyle_patch(s, style);
  s->undo[MIN(s->depth, countof(s->undo)-1)] = inverse_style;
  s->depth++; // overflow doesn't matter
  return style;
}

TStyle tstyle_pop(TStyleStack* s) {
  assertf(s->depth > 0, "extra tstyle_pop without matching tstyle_push");
  s->depth--;
  TStyle undostyle = s->undo[MIN(s->depth, countof(s->undo)-1)];
  tstyle_patch(s, undostyle);
  return undostyle;
}



// --------------------------------------------------------------------------
// tests

#ifdef CO_TESTING_ENABLED

UNUSED static const char* TStyleName(TStyle s) {
  static const char* names[_TS_MAX] = {
    #define _(NAME, ...) #NAME,
    DEF_TSTYLE_MODS(_)
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, ...) #NAME "_BG",
    DEF_TSTYLE_COLORS(_)
    #undef _
    #define _(NAME, ...) #NAME,
    DEF_TSTYLE_INTERNALS(_)
    #undef _
  };
  return names[MIN(s,_TS_MAX-1)];
}

UNUSED static void tstyle_demo() {
  TStyles all_styles[] = { &t0, &t4, &t8 };
  for (usize ti = 0; ti < countof(all_styles); ti++) {
    printf("\n");
    TStyles styles = all_styles[ti];
    const char* reset = tstyle_str(styles, TS_RESET);
    int i;

    i = 0;
    int w = 5; // max name length (clips)
    #define _(NAME, ...)                               \
      if (i++) {                                       \
        if (i > 2) putc(' ', stdout);                  \
        printf("%s%-*.*s%s", tstyle_str(styles, NAME), \
          w, MIN(w,(int)strlen(#NAME)), #NAME, reset); \
      }
    DEF_TSTYLE_MODS(_)
    #undef _
    printf(" %sWHITE%s", tstyle_str(styles, TS_WHITE), reset);
    printf(" %sBLACK%s", tstyle_str(styles, TS_BLACK), reset);
    printf(" %sWHITE%s", tstyle_str(styles, TS_WHITE_BG), reset);
    printf(" %sBLACK%s", tstyle_str(styles, TS_BLACK_BG), reset);
    printf("\n");

    i = 0;
    // w = 5;
    #define _(NAME, ...)                               \
      if (NAME != TS_WHITE && NAME != TS_BLACK) {      \
        if (i++) putc(' ', stdout);                    \
        printf("%s%-*.*s%s", tstyle_str(styles, NAME), \
          w, MIN(w,(int)strlen(#NAME)), #NAME, reset); \
      }
    DEF_TSTYLE_COLORS(_)
    printf("\n");
    #undef _

    i = 0;
    #define _(NAME, ...)                                    \
      if (NAME != TS_WHITE && NAME != TS_BLACK) {           \
        if (i++) putc(' ', stdout);                         \
        printf("%s%-*.*s%s", tstyle_str(styles, NAME##_BG), \
          w, MIN(w,(int)strlen(#NAME)), #NAME, reset);      \
      }
    DEF_TSTYLE_COLORS(_)
    printf("\n");
    #undef _
  }
}


DEF_TEST(tstyle) {

  usize t4_size = sizeof(t4) + T4_STRS_SIZE;
  usize t8_size = sizeof(t8) + T8_STRS_SIZE;

  // char tmp[1024];
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

  // const char* reset = tstyle_str(&t4, TS_RESET);
  // strrepr(tmp, sizeof(tmp), green4, strlen(green4));
  // dlog("t4.strs[t4.offs[TS_GREEN]]: \"%s\" %stest%s", tmp, green4, reset);
  // strrepr(tmp, sizeof(tmp), green8, strlen(green8));
  // dlog("t8.strs[t8.offs[TS_GREEN]]: \"%s\" %stest%s", tmp, green8, reset);

  assert(TStylesIsNone(TStylesNone()));
  assert(!TStylesIsNone(TStyles16()));
  assert(!TStylesIsNone(TStyles256()));

  // tstyle_demo();
}


DEF_TEST(tstyle_stack) {
  auto styles = TStyles256();
  TStyleStack stk = {0};
  char buf[512];
  auto sbuf = sbuf_make(buf, sizeof(buf)); auto s = &sbuf;

  // #define pushpoplog dlog
  #define pushpoplog(...) ((void)0)

  #define push(style) ({ \
    tstyle_push(&stk, style); \
    UNUSED TStyle inverse_style = stk.undo[MIN(stk.depth-1, countof(stk.undo)-1)]; \
    pushpoplog(" → %s%s\e[0m  (%s%s\e[0m)", \
      tstyle_str(styles, style), TStyleName(style), \
      tstyle_str(styles, inverse_style), TStyleName(inverse_style)); \
    tstyle_str(styles, style); \
  })

  #define pop() ({ \
    TStyle s__ = tstyle_pop(&stk); \
    pushpoplog("←  %s%s\e[0m", tstyle_str(styles, s__), TStyleName(s__)); \
    tstyle_str(styles, s__); \
  })

  sbuf_appendstr(s, "default\n");
  sbuf_appendstr(s, push(TS_ITALIC));
    sbuf_appendstr(s, "+italic\n");
    sbuf_appendstr(s, push(TS_DARKGREY_BG));
      sbuf_appendstr(s, "+grey_bg\n");
      sbuf_appendstr(s, push(TS_RED));
        sbuf_appendstr(s, "+red\n");
        sbuf_appendstr(s, push(TS_GREEN));
          sbuf_appendstr(s, "+green\n");
          sbuf_appendstr(s, push(TS_NOITALIC));
            sbuf_appendstr(s, "+noitalic\n");
          sbuf_appendstr(s, pop());
          sbuf_appendstr(s, "-noitalic\n");
          sbuf_appendstr(s, push(TS_DEFAULT_FG));
            sbuf_appendstr(s, "+default_fg\n");
          sbuf_appendstr(s, pop());
          sbuf_appendstr(s, "-default_fg\n");
          sbuf_appendstr(s, push(TS_DEFAULT_BG));
            sbuf_appendstr(s, "+default_bg\n");
          sbuf_appendstr(s, pop());
          sbuf_appendstr(s, "-default_bg\n");
        sbuf_appendstr(s, pop()); // TS_GREEN
        sbuf_appendstr(s, "-green\n");
      sbuf_appendstr(s, pop()); // TS_RED
      sbuf_appendstr(s, "-red\n");
    sbuf_appendstr(s, pop()); // TS_DARKGREY_BG
    sbuf_appendstr(s, "-grey_bg\n");
  sbuf_appendstr(s, pop()); // TS_ITALIC
  sbuf_appendstr(s, "-italic\n");
  asserteq(stk.depth, 0);

  // sbuf = sbuf_make(buf, sizeof(buf));
  // sbuf_appendstr(s, "default\n");
  // sbuf_appendstr(s, push(TS_GREEN));
  // sbuf_appendstr(s, "green\n");
  // sbuf_appendstr(s, pop());
  // sbuf_appendstr(s, "default\n");
  // sbuf_appendstr(s, push(TS_GREEN));
  // sbuf_appendstr(s, "green\n");
  // sbuf_appendstr(s, pop());
  // sbuf_appendstr(s, "default\n");
  // asserteq(stk.depth, 0);

  // sbuf_terminate(s);
  // char tmp[sizeof(buf)*3];
  // strrepr(tmp, sizeof(tmp), buf, strlen(buf));
  // printf("\n%s\n\n%s\n", tmp, buf);
  // exit(0);
}


#endif // CO_TESTING_ENABLED
