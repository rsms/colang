#include "colib.h"


enum pstate {
  PS_START,   // root state; ESC starts a sequence, any other char is ignored
  PS_TYPE,    // expect '[', 'P' or ']' after ESC
  PS_ATTR,    // expect sequence data
  PS_ATTR2,
  PS_ATTR3,
  PS_ATTR4,
  PS_XCOLOR1,
  PS_XCOLOR2,
  PS_INT8_START,
  PS_INT8_NEXT,
  PS_INT8,
  PS_END,     // expect end of data (';') or end of sequence ('m')
};
static_assert(PS_START == 0, "PS_START must be value 0");


#ifdef CO_TESTING_ENABLED
  char* aesc_parse_state_str(u8 s) {
    switch ((enum pstate)s) {
      case PS_START:      return "START";
      case PS_TYPE:       return "TYPE";
      case PS_ATTR:       return "ATTR";
      case PS_ATTR2:      return "ATTR2";
      case PS_ATTR3:      return "ATTR3";
      case PS_ATTR4:      return "ATTR4";
      case PS_XCOLOR1:    return "XCOLOR1";
      case PS_XCOLOR2:    return "XCOLOR2";
      case PS_INT8_START: return "INT8_START";
      case PS_INT8_NEXT:  return "INT8_NEXT";
      case PS_INT8:       return "INT8";
      case PS_END:        return "END";
    }
    return "?";
  }
#endif


AEscParseState aesc_parsec(AEscParser* p, char c) {
  AEscAttr* na = &p->nextattr;

  #define TR(to_state)    { p->pstate[0] = (to_state); break; }
  #define NOT_IMPL(what)  { dlog("TODO NOT_IMPL %s", what); goto endnone; }
  #define INVALID()       TR(PS_START)
  #define PUSH_PSTATE(state) { \
    p->pstate[1 + !!p->pstate[1]] = (state); /* [2] if [1] is non-zero, else [1] */ \
    /*dlog("** push ; pstate is now: %-10s %-10s %-10s", \
      p->pstate[0] ? aesc_parse_state_str(p->pstate[0]) : "-", \
      p->pstate[1] ? aesc_parse_state_str(p->pstate[1]) : "-", \
      p->pstate[2] ? aesc_parse_state_str(p->pstate[2]) : "-");*/ \
  }
  #define POP_PSTATE() { \
    p->pstate[0] = p->pstate[1]; \
    p->pstate[1] = p->pstate[2]; \
    p->pstate[2] = 0; \
    /*dlog("** pop  ; pstate is now: %-10s %-10s %-10s", \
      p->pstate[0] ? aesc_parse_state_str(p->pstate[0]) : "-", \
      p->pstate[1] ? aesc_parse_state_str(p->pstate[1]) : "-", \
      p->pstate[2] ? aesc_parse_state_str(p->pstate[2]) : "-");*/ \
    if (p->pstate[0]) break; \
  }

  switch ((enum pstate)p->pstate[0]) {

  case PS_START: switch (c) {
    case '\x1B': *na = p->attr; TR(PS_TYPE);
    default:                    return AESC_P_NONE; // remain at PS_START
  } break;

  case PS_TYPE: switch (c) { // ESC…
    case '[': TR(PS_ATTR);     // CSI - Control Sequence Introducer
    case 'P': NOT_IMPL("DCS"); // DCS - Device Control String
    case ']': NOT_IMPL("OSC"); // OSC - Operating System Command
    default:  goto endnone;
  } break;

  case PS_ATTR: switch (c) { // ESC[…
    case '0': *na = p->defaultattr;            TR(PS_END);
    case '1': na->bold = 1; na->fg8bright = 1; TR(PS_END);
    case '2':                                  TR(PS_ATTR2);
    case '3':                                  TR(PS_ATTR3);
    case '4':                                  TR(PS_ATTR4);
    case '5': na->blink = 1;                   TR(PS_END);
    case '7': na->inverse = 1;                 TR(PS_END);
    case '8': na->hidden = 1;                  TR(PS_END);
    case '9': na->strike = 1;                  TR(PS_END);
    case ';': *na = p->defaultattr;            TR(PS_ATTR);
    case 'm': *na = p->defaultattr;            goto endattr;
    default:                                   goto endnone;
  } break;

  case PS_ATTR2: switch (c) { // ESC[2…
    case '2': na->dim = 0; na->bold = 0; na->fg8bright = 0; TR(PS_END);
    case '3': na->italic = 0;                               TR(PS_END);
    case '4': na->underline = 0;                            TR(PS_END);
    case '5': na->blink = 0;                                TR(PS_END);
    case '7': na->inverse = 0;                              TR(PS_END);
    case '8': na->hidden = 0;                               TR(PS_END);
    case '9': na->strike = 0;                               TR(PS_END);
    case ';': na->dim = 1; na->fg8bright = 0;               TR(PS_ATTR);
    case 'm': na->dim = 1; na->fg8bright = 0;               goto endattr;
    default:                                                goto endnone;
  } break;

  case PS_ATTR3: switch (c) { // ESC[3…
    case '0' ... '7': na->fgtype = 0; na->fg8 = (c - '0');              TR(PS_END);
    case '8': p->int8p = &na->fg256;                                    TR(PS_XCOLOR1);
    case '9': na->fgtype = 0; memcpy(na->fgrgb,p->defaultattr.fgrgb,3); TR(PS_END);
    case ';': na->italic = 1;                                           TR(PS_ATTR);
    case 'm': na->italic = 1;                                           goto endattr;
    default:                                                            goto endnone;
  } break;

  case PS_ATTR4: switch (c) { // ESC[4…
    case '0' ... '7': na->bgtype = 0; na->bg8 = (c - '0');              TR(PS_END);
    case '8': p->int8p = &na->bg256;                                    TR(PS_XCOLOR1);
    case '9': na->bgtype = 0; memcpy(na->bgrgb,p->defaultattr.bgrgb,3); TR(PS_END);
    case ';': na->underline = 1;                                        TR(PS_ATTR);
    case 'm': na->underline = 1;                                        goto endattr;
    default:                                                            goto endnone;
  } break;

  case PS_XCOLOR1: switch (c) { // ESC[{3,4}8…   ESC[x8;5;{ID}m | ESC[x8;2;{r};{g};{b}m
    case ';': TR(PS_XCOLOR2);
    default:  goto endnone;
  } break;

  case PS_XCOLOR2: switch (c) { // ESC[{3,4}8;…
    case '2':
      p->int8p == &na->fg256 ? (na->fgtype = 2) : (na->bgtype = 2);
      // we want two more 8-bit ints after the first one, i.e. ESC[x8;2;{r};{g};{b}m
      PUSH_PSTATE(PS_INT8_NEXT);
      PUSH_PSTATE(PS_INT8_NEXT);
      TR(PS_INT8_START);
    case '5':
      p->int8p == &na->fg256 ? (na->fgtype = 1) : (na->bgtype = 1);
      TR(PS_INT8_START);
    default: goto endnone;
  } break;

  case PS_INT8_START: switch (c) { // ;…
    case ';': *p->int8p = 0; TR(PS_INT8);
    default:                 goto endnone;
  } break;

  case PS_INT8_NEXT:
    p->int8p++;
    *p->int8p = 0;
    p->pstate[0] = PS_INT8;
    FALLTHROUGH;
  case PS_INT8: switch (c) { // unsigned 8-bit integer [0…255]
    case '0' ... '9':
      if (check_add_overflow((u8)(*p->int8p * 10), (u8)(c - '0'), p->int8p))
        goto endnone;
      break; // remain at pstate PS_INT8
    case ';': POP_PSTATE(); /* else: */ TR(PS_ATTR);
    case 'm': goto endattr;
    default:  goto endnone;
  } break;

  case PS_END: switch (c) {
    case ';': TR(PS_ATTR);
    case 'm': goto endattr;
    default:  goto endnone;
  } break;

  } // switch
  return p->pstate[0] ? AESC_P_MORE : AESC_P_NONE;
endattr:
  *(u32*)p->pstate = 0; // clear stack and set p->pstate[0]=PS_START
  p->attr = p->nextattr;
  return AESC_P_ATTR;
endnone:
  *(u32*)p->pstate = 0;
  return AESC_P_NONE;
}
