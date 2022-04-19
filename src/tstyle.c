// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"

#ifndef CO_NO_LIBC
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
  #ifndef CO_NO_LIBC
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
  #ifndef CO_NO_LIBC
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
  #ifndef CO_NO_LIBC
    t = isatty(STDERR_FILENO) ? TStylesForTerm() : &t0;
  #else
    t = &t0;
  #endif
  return t;
}


// tstyle_patch relies on TS_DEFAULT_* to have a lesser value than any color
static_assert(TS_DEFAULT_FG < TS_BLACK, "");
static_assert(TS_DEFAULT_BG < TS_BLACK_BG, "");


static void patch_attr(AEscAttr* a, TStyle style) {
  switch ((enum TStyle)style) {
    case TS_RESET:       *a = AESC_DEFAULT_ATTR; break;
    case TS_BOLD:        a->bold = true; break;
    case TS_DIM:         a->dim = true; break;
    case TS_ITALIC:      a->italic = true; break;
    case TS_UNDERLINE:   a->underline = true; break;
    case TS_NOBOLD:      a->bold = false; break;
    case TS_NODIM:       a->dim = false; break;
    case TS_NOITALIC:    a->italic = false; break;
    case TS_NOUNDERLINE: a->underline = false; break;
    case TS_DEFAULT_FG:  a->fgtype = 0; memcpy(a->fgrgb, AESC_DEFAULT_ATTR.fgrgb, 3); break;
    case TS_DEFAULT_BG:  a->bgtype = 0; memcpy(a->bgrgb, AESC_DEFAULT_ATTR.bgrgb, 3); break;

    case _TS_FGCOLOR_START ... _TS_FGCOLOR_END-1:
      a->fg256 = style;
      a->fgtype = 3;
      break;

    case _TS_BGCOLOR_START ... _TS_BGCOLOR_END-1:
      a->bg256 = style;
      a->bgtype = 3;
      break;

    case _TS_MAX:
    case _TS_NONE:
      break;
  }
}


static const char* diff_attr(TStyleStack* st, AEscAttr prev, AEscAttr next) {
  if (aesc_attr_eq(&prev, &next))
    return "";

  st->buf[0] = '\x1B';
  st->buf[1] = '[';
  ABuf s_ = abuf_make(st->buf + 2, sizeof(st->buf) - 2); ABuf* s = &s_;

  #define ADD_STYLE(tstyle) { \
    if (s->len) abuf_c(s, ';'); \
    const char* str = tstyle_str(st->styles, (tstyle)) + strlen(PRE); \
    abuf_append(s, str, strlen(str) - strlen(SUF)); \
  }

  #define FLAG(FIELD, on_style, off_style) \
    if (prev.FIELD != next.FIELD) ADD_STYLE(next.FIELD ? (on_style) : (off_style));

  FLAG(bold,      TS_BOLD,      TS_NOBOLD)
  FLAG(dim,       TS_DIM,       TS_NODIM)
  FLAG(italic,    TS_ITALIC,    TS_NOITALIC)
  FLAG(underline, TS_UNDERLINE, TS_NOUNDERLINE)
  // FLAG(inverse, not supported by tstyle)
  // FLAG(blink, not supported by tstyle)
  // FLAG(hidden, not supported by tstyle)
  // FLAG(strike, not supported by tstyle)

  if (prev.fg256 != next.fg256) {
    ADD_STYLE((next.fgtype == 3) ? (TStyle)next.fg256 : TS_DEFAULT_FG);
  }

  if (prev.bg256 != next.bg256)
    ADD_STYLE((next.bgtype == 3) ? (TStyle)next.bg256 : TS_DEFAULT_BG);

  if (s->len == 0)
    return "";
  abuf_c(s, 'm');
  abuf_terminate(s);
  return st->buf;
}


const char* tstyle_pushv(TStyleStack* st, const TStyle* stylev, u32 stylec) {
  if (st->styles == &t0)
    return "";

  AEscAttr a = st->stack_len ? st->stack[st->stack_len] : AESC_DEFAULT_ATTR;
  for (u32 i = 0; i < stylec; i++)
    patch_attr(&a, stylev[i]);

  st->stack[st->stack_len] = a;
  st->stack_len = MIN(st->stack_len + 1, countof(st->stack)-2);

  if (stylec == 1)
    return tstyle_str(st->styles, stylev[0]);

  if (stylec == 0)
    return "";

  st->buf[0] = '\x1B';
  st->buf[1] = '[';
  ABuf s_ = abuf_make(st->buf + 2, sizeof(st->buf) - 2); ABuf* s = &s_;

  for (u32 i = 0; i < stylec; i++) {
    if (s->len) abuf_c(s, ';');
    const char* str = tstyle_str(st->styles, stylev[i]) + strlen(PRE);
    abuf_append(s, str, strlen(str) - strlen(SUF));
  }

  abuf_c(s, 'm');
  abuf_terminate(s);
  return st->buf;
}


const char* tstyle_pop(TStyleStack* st) {
  if (st->styles == &t0)
    return "";
  assertf(st->stack_len > 0, "extra tstyle_pop without matching tstyle_push");
  st->stack_len--;
  AEscAttr prev = st->stack[st->stack_len];
  AEscAttr next = st->stack_len == 0 ? AESC_DEFAULT_ATTR : st->stack[st->stack_len - 1];
  return diff_attr(st, prev, next);
}



//———————————————————————————————————————————————————————————————————————————————————————
// tests must be here in this source file as they need to access the internal tN tables

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


/* TODO: convert to new AEscAttr impl
DEF_TEST(tstyle_stack) {
  auto styles = TStyles256();
  TStyleStack stk = {0};
  char buf[512];
  ABuf abuf = abuf_make(buf, sizeof(buf)); ABuf* s = &abuf;

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

  abuf_cstr(s, "default\n");
  abuf_cstr(s, push(TS_ITALIC));
    abuf_cstr(s, "+italic\n");
    abuf_cstr(s, push(TS_DARKGREY_BG));
      abuf_cstr(s, "+grey_bg\n");
      abuf_cstr(s, push(TS_RED));
        abuf_cstr(s, "+red\n");
        abuf_cstr(s, push(TS_GREEN));
          abuf_cstr(s, "+green\n");
          abuf_cstr(s, push(TS_NOITALIC));
            abuf_cstr(s, "+noitalic\n");
          abuf_cstr(s, pop());
          abuf_cstr(s, "-noitalic\n");
          abuf_cstr(s, push(TS_DEFAULT_FG));
            abuf_cstr(s, "+default_fg\n");
          abuf_cstr(s, pop());
          abuf_cstr(s, "-default_fg\n");
          abuf_cstr(s, push(TS_DEFAULT_BG));
            abuf_cstr(s, "+default_bg\n");
          abuf_cstr(s, pop());
          abuf_cstr(s, "-default_bg\n");
        abuf_cstr(s, pop()); // TS_GREEN
        abuf_cstr(s, "-green\n");
      abuf_cstr(s, pop()); // TS_RED
      abuf_cstr(s, "-red\n");
    abuf_cstr(s, pop()); // TS_DARKGREY_BG
    abuf_cstr(s, "-grey_bg\n");
  abuf_cstr(s, pop()); // TS_ITALIC
  abuf_cstr(s, "-italic\n");
  asserteq(stk.depth, 0);

  // abuf = abuf_make(buf, sizeof(buf));
  // abuf_cstr(s, "default\n");
  // abuf_cstr(s, push(TS_GREEN));
  // abuf_cstr(s, "green\n");
  // abuf_cstr(s, pop());
  // abuf_cstr(s, "default\n");
  // abuf_cstr(s, push(TS_GREEN));
  // abuf_cstr(s, "green\n");
  // abuf_cstr(s, pop());
  // abuf_cstr(s, "default\n");
  // asserteq(stk.depth, 0);

  // abuf_terminate(s);
  // char tmp[sizeof(buf)*3];
  // strrepr(tmp, sizeof(tmp), buf, strlen(buf));
  // printf("\n%s\n\n%s\n", tmp, buf);
  // exit(0);
}*/

#endif // CO_TESTING_ENABLED
//———————————————————————————————————————————————————————————————————————————————————————
