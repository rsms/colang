#include "colib.h"


UNUSED static char* ANSIColor8_str(ANSIColor s) {
  switch (s) {
    case ANSI_COLOR_BLACK:       return "black";
    case ANSI_COLOR_RED:         return "red";
    case ANSI_COLOR_GREEN:       return "green";
    case ANSI_COLOR_YELLOW:      return "yellow";
    case ANSI_COLOR_BLUE:        return "blue";
    case ANSI_COLOR_MAGENTA:     return "magenta";
    case ANSI_COLOR_CYAN:        return "cyan";
    case ANSI_COLOR_WHITE:       return "white";
    // with bright bit set:
    case ANSI_COLOR_BLACK + 8:   return "BLACK";
    case ANSI_COLOR_RED + 8:     return "RED";
    case ANSI_COLOR_GREEN + 8:   return "GREEN";
    case ANSI_COLOR_YELLOW + 8:  return "YELLOW";
    case ANSI_COLOR_BLUE + 8:    return "BLUE";
    case ANSI_COLOR_MAGENTA + 8: return "MAGENTA";
    case ANSI_COLOR_CYAN + 8:    return "CYAN";
    case ANSI_COLOR_WHITE + 8:   return "WHITE";

  }
  return "?";
}


UNUSED static char* AEscParseState_str(AEscParseState s) {
  switch ((enum AEscParseState)s) {
    case AESC_P_MORE: return "MORE";
    case AESC_P_ATTR: return "ATTR";
    case AESC_P_NONE: return "NONE";
  }
  return "?";
}


char* aesc_parse_state_str(u8 s);


static void fmtattr(AEscAttr a, char* buf, usize bufcap) {
  ABuf s = abuf_make(buf, bufcap);
  abuf_c(&s, '{');
  // colors
  switch (a.fgtype) {
    case 0: abuf_cstr(&s, ANSIColor8_str(AESC_ATTR_FG8(&a))); break;
    case 1: abuf_u32(&s, (u32)a.fg256, 10); break;
    case 2: abuf_fmt(&s, "#%02X%02X%02X", a.fgrgb[0], a.fgrgb[1], a.fgrgb[2]); break;
    default: assertf(0,"invalid fgtype %u", a.fgtype);
  }
  abuf_cstr(&s, ", ");
  switch (a.bgtype) {
    case 0: abuf_cstr(&s, ANSIColor8_str(AESC_ATTR_BG8(&a))); break;
    case 1: abuf_u32(&s, (u32)a.bg256, 10); break;
    case 2: abuf_fmt(&s, "#%02X%02X%02X", a.bgrgb[0], a.bgrgb[1], a.bgrgb[2]); break;
    default: assertf(0,"invalid bgtype %u", a.bgtype);
  }
  // flags
  if (a.bold)      abuf_cstr(&s, ", bold");
  if (a.dim)       abuf_cstr(&s, ", dim");
  if (a.italic)    abuf_cstr(&s, ", italic");
  if (a.underline) abuf_cstr(&s, ", underline");
  if (a.inverse)   abuf_cstr(&s, ", inverse");
  if (a.blink)     abuf_cstr(&s, ", blink");
  if (a.hidden)    abuf_cstr(&s, ", hidden");
  if (a.strike)    abuf_cstr(&s, ", strike");
  abuf_c(&s, '}');
  abuf_terminate(&s);
}


DEF_TEST(aesc_parsec) {
  #define DEFAULT_FG AESC_DEFAULT_ATTR.fg8
  #define DEFAULT_BG AESC_DEFAULT_ATTR.bg8

  static char tmpbuf[3][256];
  static const AEscAttr
    a_def_def = AESC_DEFAULT_ATTR,
    a_red_def = {
      .fg8 = ANSI_COLOR_RED, .bg8 = DEFAULT_BG,
    },
    a_red_blue = {
      .fg8 = ANSI_COLOR_RED, .bg8 = ANSI_COLOR_BLUE,
    },
    a_203_def  = {
      .fg256 = 203, .fgtype=1,
    },
    a_203_39  = {
      .fg256 = 203, .fgtype=1,
      .bg256 = 39, .bgtype=1,
    },
    a_rgbFF0102_def  = {
      .fgrgb = {0xFF,0x01,0x02}, .fgtype=2,
    },
    a_rgbFF0102_rgb0201FF  = {
      .fgrgb = {0xFF,0x01,0x02}, .fgtype=2,
      .bgrgb = {0x02,0x01,0xFF}, .bgtype=2,
    },
    a_white_red_dim = {
      .fg8 = ANSI_COLOR_WHITE, .bg8 = ANSI_COLOR_RED,
      .dim = 1,
    },
    a_DEF_def = {
      .fg8 = DEFAULT_FG, .bg8 = DEFAULT_BG, .fg8bright = true,
      .bold = 1,
    },
    a_DEF_def_dim = {
      .fg8 = DEFAULT_FG, .bg8 = DEFAULT_BG,
      .bold = 1, .dim = 1,
    };

  // // fmtattr mini tests (we rely on these for debugging "real" tests)
  // fmtattr(a_203_def, tmpbuf[0], sizeof(tmpbuf[0]));
  // dlog("a_203_def: %s", tmpbuf[0]);

  // fmtattr(a_rgbFF0102_def, tmpbuf[0], sizeof(tmpbuf[0]));
  // dlog("a_rgbFF0102_def: %s", tmpbuf[0]);

  // fmtattr(a_white_red_dim, tmpbuf[0], sizeof(tmpbuf[0]));
  // dlog("a_white_red_dim: %s", tmpbuf[0]);

  // fmtattr(a_DEF_def, tmpbuf[0], sizeof(tmpbuf[0]));
  // dlog("a_DEF_def: %s", tmpbuf[0]);

  //exit(0);

  static struct {
    const char      input[64];
    const AEscAttr* expected_attrs[64];
  } tests[] = {
    {
      "hello \x1B[31mredfg \x1B[44mbluebg\x1B[49m redfg\x1B[39m",
      {
        [1]  = &a_def_def,
        [10] = &a_red_def, [21] = &a_red_blue, [32] = &a_red_def, [43] = &a_def_def,
      },
    },
    {
      "normal \x1B[1mbright\x1B[2mdim\x1B[22m end",
      { [10] = &a_DEF_def, [20] = &a_DEF_def_dim, [28] = &a_def_def, },
    },
    {
      "normal \x1B[2;37;41mdim white on red\x1B[m end",
      { [16] = &a_white_red_dim, [35] = &a_def_def, },
    },
    { // 256 color
      "normal \x1B[38;5;203mred \x1B[48;5;39mbluebg\x1B[49m red\x1B[39m end",
      { [17] = &a_203_def, [31] = &a_203_39, [42] = &a_203_def, [51] = &a_def_def },
    },
    { // RGB color
      "norm \x1B[38;2;255;1;2mred \x1B[48;2;2;1;255mbluebg\x1B[49m red\x1B[39m end",
      { [19] = &a_rgbFF0102_def, [38] = &a_rgbFF0102_rgb0201FF,
        [49] = &a_rgbFF0102_def, [58] = &a_def_def },
    },
  };

  for (u32 tidx = 0; tidx < countof(tests); tidx++) {
    auto t = &tests[tidx];
    usize len = strlen(t->input);
    AEscParser p = aesc_mkparser(AESC_DEFAULT_ATTR);

    sfmt_repr(tmpbuf[0], sizeof(tmpbuf[0]), t->input, len);
    //dlog("————— %s ————", tmpbuf[0]);

    for (usize i = 0; i < len; i++) {
      UNUSED auto prevstate = p.pstate[0];
      AEscParseState retval = aesc_parsec(&p, t->input[i]);
      const AEscAttr* expect_attr = t->expected_attrs[i];

      // expect return value AESC_P_ATTR if we are expecting an attribute
      if ( ( (i > 1 && expect_attr) && retval != AESC_P_ATTR) ||
           (!(i > 1 && expect_attr) && retval == AESC_P_ATTR) )
      {
        sfmt_repr(tmpbuf[2], sizeof(tmpbuf[2]), t->input, MIN(len, i+1));
        assertf(0, "[test#%u] s[%zu]: expected return value %s; got %s\n%s\n%*s↑\n",
          tidx, i,
          ((i > 1 && expect_attr) ? "ATTR" : "MORE|NONE"),
          AEscParseState_str(retval),
          tmpbuf[2], (int)strlen(tmpbuf[2])-1, "");
      }

      if (expect_attr && memcmp(expect_attr, &p.attr, sizeof(p.attr)) != 0) {
        sfmt_repr(tmpbuf[2], sizeof(tmpbuf[2]), t->input, len);
        log("\"%s\"", tmpbuf[2]);

        fmtattr(*expect_attr, tmpbuf[0], sizeof(tmpbuf[0]));
        fmtattr(p.attr, tmpbuf[1], sizeof(tmpbuf[1]));
        sfmt_repr(tmpbuf[2], sizeof(tmpbuf[2]), t->input, MIN(len, i+1));
        assertf(0, "[test#%u] s[%zu]: expected p.attr %s; got %s\n%s\n%*s↑\n",
          tidx, i, tmpbuf[0], tmpbuf[1],
          tmpbuf[2], (int)strlen(tmpbuf[2])-1, "");
      }

      // // Debug helper
      // sfmt_repr(tmpbuf[0], sizeof(tmpbuf[0]), t->input+i, 1);
      // if (retval == AESC_P_ATTR) {
      //   fmtattr(p.attr, tmpbuf[1], sizeof(tmpbuf[1]));
      // } else {
      //   tmpbuf[1][0] = 0;
      // }
      // log("[%2zu] %-10s ⟶ %02x %-4s ⟶ %-10s %s",
      //   i, aesc_parse_state_str(prevstate),
      //   t->input[i], tmpbuf[0],
      //   aesc_parse_state_str(p.pstate[0]), tmpbuf[1]);
    }
  }
}
