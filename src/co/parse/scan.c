#include "../common.h"
#include "parse.h"

// Enable to dlog ">> TOKEN VALUE at SOURCELOC" on each call to SNext
//#define SCANNER_DEBUG_TOKEN_PRODUCTION


// character flags. (bit flags)
// * + - . / 0-9 A-Z _ a-z
//
#define CH_IDENT      1 << 0  /* 1 = valid in middle of identifier */
#define CH_WHITESPACE 1 << 1  /* 2 = whitespace */
//
static u8 charflags[256] = {
        /* 0 1 2 3 4 5 6 7 8 9 A B C D E F */
//         <CTRL> ...    9=TAB, A=LF, D=CR
/* 0x00 */ 0,0,0,0,0,0,0,0,0,2,2,0,0,2,0,0,
//         <CTRL> ...
/* 0x10 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//           ! " # $ % & ' ( ) * + , - . /
/* 0x20 */ 2,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,
//         0 1 2 3 4 5 6 7 8 9 : ; < = > ?
/* 0x30 */ 1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
//         @ A B C D E F G H I J K L M N O
/* 0x40 */ 0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
//         P Q R S T U V W X Y Z [ \ ] ^ _
/* 0x50 */ 1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,
//         ` a b c d e f g h i j k l m n o
/* 0x60 */ 0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
//         p q r s t u v w x y z { | } ~ <DEL>
/* 0x70 */ 1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
//         <CTRL> ...
/* 0x80 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//         <CTRL> ...
/* 0x90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//    <NBSP> ¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ <SOFTHYPEN> ® ¯
/* 0xA0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//         ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿
/* 0xB0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//         À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
/* 0xC0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//         Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
/* 0xD0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//         à á â ã ä å æ ç è é ê ë ì í î ï
/* 0xE0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
//         ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ
/* 0xF0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};


bool ScannerInit(Scanner* s, Build* build, Source* src, ParseFlags flags) {
  if (!SourceOpenBody(src))
    return false;

  s->build        = build;
  s->src          = src;
  s->srcposorigin = posmap_origin(&build->posmap, src);
  s->flags        = flags;
  s->inp          = src->body;
  s->inend        = src->body + src->len;
  s->insertSemi   = false;

  s->indent = (Indent){0,0};
  s->indentDst = (Indent){0,0};
  s->indentStack.len = 0;
  if (s->indentStack.cap == 0) {
    s->indentStack.v = s->indentStack.storage;
    s->indentStack.cap = countof(s->indentStack.storage);
  }

  s->tok        = TNone;
  s->tokstart   = s->inp;
  s->tokend     = s->inp;
  s->prevtokend = s->inp;

  s->linestart = s->inp;
  s->lineno    = 1;

  return true;
}

void ScannerDispose(Scanner* s) {
  if (s->indentStack.v != s->indentStack.storage) {
    memfree(s->build->mem, s->indentStack.v);
  }

  // free comments
  while (1) {
    auto c = ScannerCommentPop(s);
    if (!c)
      break;
    memfree(s->build->mem, c);
  }
}


// serr is called when an error occurs. It invokes s->errh
static void serr(Scanner* s, const char* fmt, ...) {
  auto pos = ScannerPos(s);
  va_list ap;
  va_start(ap, fmt);
  build_diagv(s->build, DiagError, (PosSpan){pos, pos}, fmt, ap);
  va_end(ap);
}


#ifdef SCANNER_DEBUG_TOKEN_PRODUCTION
  static bool tok_has_value(Tok t) {
    return t == TId || t == TIntLit || t == TFloatLit;
  }
  static void debug_token_production(Scanner* s) {
    auto posstr = pos_str(&s->build->posmap, ScannerPos(s), str_new(32));
    static size_t vallen_max = 8; // global; yolo
    const int tokname_max = (int)strlen("keyword interface");
    size_t vallen = 0;
    const char* valptr = NULL;
    if (tok_has_value(s->tok))
      valptr = (const char*)ScannerTokStr(s, &vallen);

    vallen_max = MAX(vallen_max, vallen);
    dlog(">> %-*s %.*s%*s %s",
        tokname_max, TokName(s->tok),
        (int)vallen, valptr,
        (int)(vallen_max - vallen), "",
        posstr);
    str_free(posstr);
  }
#else
  #define debug_token_production(s) do{}while(0)
#endif


Comment* ScannerCommentPop(Scanner* s) {
  auto c = s->comments_head;
  if (c) {
    s->comments_head = c->next;
    c->next = NULL;
  }
  return c;
}


static void comments_push_back(Scanner* s) {
  auto c = (Comment*)memalloc(s->build->mem, sizeof(Comment));
  c->next = NULL;
  c->src = s->src;
  c->ptr = s->tokstart;
  c->len = s->tokend - s->tokstart;
  if (s->comments_head) {
    s->comments_tail->next = c;
  } else {
    s->comments_head = c;
  }
  s->comments_tail = c;
}


static void scomment_block(Scanner* s) {
  s->tokstart += 2; // exclude "/*"
  u8 prevc = 0;
  while (s->inp < s->inend) {
    switch (*s->inp) {
      case '/':
        if (prevc == '*') {
          s->tokend = s->inp - 1; // -1 to skip '*'
          s->inp++; // consume '*'
          if (s->flags & ParseComments)
            comments_push_back(s);
          return;
        }
        break;
      case '\n':
        // update line state
        s->lineno++;
        s->linestart = s->inp + 1;
        break;
      default:
        break;
    }
    prevc = *s->inp;
    s->inp++;
  }
}


static void scomment(Scanner* s) {
  s->tokstart += 2; // exclude "//"
  // line comment
  // advance s->inp until next <LF> or EOF. Leave s->inp at \n or EOF.
  while (s->inp < s->inend && *s->inp != '\n') {
    s->inp++;
  }
  s->tokend = s->inp;
  if (s->flags & ParseComments)
    comments_push_back(s);
}


// read unicode name
static void snameuni(Scanner* s) {
  while (s->inp < s->inend) {
    u8 b = *s->inp;
    Rune r;
    if (b < RuneSelf) {
      if (!(charflags[b] & CH_IDENT))
        break;
      r = b;
      s->inp++;
    } else {
      u32 w = 0;
      r = utf8decode(s->inp, s->inend - s->inp, &w);
      s->inp += w;
      if (r == RuneErr) {
        serr(s, "invalid UTF-8 encoding");
      }
    }
  }
  s->tokend = s->inp;
  s->name = symget(s->build->syms, (const char*)s->tokstart, s->tokend - s->tokstart);
  s->tok = sym_langtok(s->name); // TId or a T* keyword
}


// read ASCII name (may switch over to snameuni)
static void sname(Scanner* s) {
  while (s->inp < s->inend && charflags[*s->inp] & CH_IDENT)
    s->inp++;

  if (*s->inp >= RuneSelf && s->inp < s->inend) {
    // s->inp = s->tokstart;
    return snameuni(s);
  }

  s->tokend = s->inp;
  size_t len = (size_t)(uintptr_t)(s->tokend - s->tokstart);
  s->name = symget(s->build->syms, (const char*)s->tokstart, len);
  s->tok = sym_langtok(s->name); // TId or a T* keyword
}


// scan a number
static void snumber(Scanner* s) {
  while (s->inp < s->inend) {
    u8 c = *s->inp;
    if (c > '9' || c < '0') {
      break;
    }
    s->inp++;
  }
  s->tokend = s->inp; // XXX
  s->tok = TIntLit;
}


static void indent_stack_grow(Scanner* s) {
  u32 cap = s->indentStack.cap * 2;
  if (s->indentStack.v != s->indentStack.storage) {
    s->indentStack.v = memrealloc(s->build->mem, s->indentStack.v, sizeof(Indent) * cap);
  } else {
    // moving array from stack to heap
    Indent* v = (Indent*)memalloc(s->build->mem, sizeof(Indent) * cap);
    memcpy(v, s->indentStack.v, sizeof(Indent) * s->indentStack.len);
    s->indentStack.v = v;
  }
  s->indentStack.cap = cap;
}


static void check_mixed_indent(Scanner* s) {
  const u8* p = &s->linestart[1];
  u8 c = *s->linestart;
  while (p < s->inp) {
    if (c != *p) {
      dlog("mixed indent '%C' != '%C'", c, *p);
      serr(s, "mixed whitespace characters in indentation");
      return;
    }
    p++;
  }
}


static void indent_push(Scanner* s) {
  #ifdef SCANNER_DEBUG_TOKEN_PRODUCTION
  dlog(">> INDENT PUSH %u (%s) -> %u (%s)",
    s->indent.n, s->indent.isblock ? "block" : "space",
    s->indentDst.n, s->indentDst.isblock ? "block" : "space");
  #endif

  if (R_UNLIKELY(s->indentStack.len == s->indentStack.cap))
    indent_stack_grow(s);

  s->indentStack.v[s->indentStack.len++] = s->indent;
  s->indent = s->indentDst;
}


static bool indent_pop(Scanner* s) {
  // decrease indentation
  assert_debug(s->indent.n > s->indentDst.n);

  #ifdef SCANNER_DEBUG_TOKEN_PRODUCTION
  Indent prev_indent = s->indent;
  #endif

  bool isblock = s->indent.isblock;

  if (s->indentStack.len == 0) {
    s->indent = s->indentDst;
  } else {
    s->indent = s->indentStack.v[--s->indentStack.len];
  }

  #ifdef SCANNER_DEBUG_TOKEN_PRODUCTION
  dlog(">> INDENT POP %u (%s) -> %u (%s)",
    prev_indent.n, prev_indent.isblock ? "block" : "space",
    s->indent.n, s->indent.isblock ? "block" : "space");
  #endif

  return isblock;
}


Tok ScannerNext(Scanner* s) {
  s->prevtokend = s->tokend;
  scan_again: {}  // jumped to when comments are skipped
  // dlog("-- '%c' 0x%02X (%zu)", *s->inp, *s->inp, (size_t)(s->inp - s->src->body));

  // unwind >1-level indent
  if (s->indent.n > s->indentDst.n) {
    bool isblock = indent_pop(s);
    if (isblock) {
      s->tok = TRBrace;
      debug_token_production(s);
      return s->tok;
    }
  }

  // whitespace
  bool islnstart = s->inp == s->linestart;
  while (s->inp < s->inend && (charflags[*s->inp] & CH_WHITESPACE)) {
    if (*s->inp == '\n') {
      s->lineno++;
      s->linestart = s->inp + 1;
      islnstart = true;
    }
    s->inp++;
  }

  // implicit semicolon, '{' or '}'
  if (islnstart) {
    s->tokstart = s->linestart - 1;
    s->tokend = s->linestart - 1;
    s->indentDst = (Indent){
      .isblock = s->insertSemi,
      .n = (i32)(s->inp - s->linestart),
    };
    if (s->indentDst.n > s->indent.n) {
      // increase in indentation; produce "{"
      indent_push(s);
      if (s->insertSemi) {
        if (s->build->debug)
          check_mixed_indent(s);
        s->insertSemi = false;
        s->tok = TLBrace;
        debug_token_production(s);
        return s->tok;
      }
    } else {
      if (s->build->debug)
        check_mixed_indent(s);
      if (s->indentDst.n < s->indent.n) {
        // decrease in indentation
        bool isblock = indent_pop(s);
        if (isblock) {
          s->insertSemi = false;
          s->tok = TRBrace;
          debug_token_production(s);
          return s->tok;
        }
      }
      if (s->insertSemi) {
        s->insertSemi = false;
        s->tok = TSemi;
        debug_token_production(s);
        return s->tok;
      }
    }
  }

  // EOF
  if (R_UNLIKELY(s->inp == s->inend)) {
    s->tokstart = s->inp - 1;
    s->tokend = s->tokstart;
    s->indentDst.n = 0;
    if (s->indent.n > 0 && indent_pop(s) /*isblock*/) {
      // decrease indentation to 0 if source ends at indentation
      s->tok = TRBrace;
      s->insertSemi = false;
      debug_token_production(s);
      return s->tok;
    }
    if (s->insertSemi) {
      s->insertSemi = false;
      s->tok = TSemi;
    } else {
      s->tok = TNone;
    }
    debug_token_production(s);
    return s->tok;
  }

  bool insertSemi = false; // in a temp var because of scan_again
  s->tokstart = s->inp;
  s->tokend = s->tokstart + 1;

  u8 c = *s->inp++; // current char
  u8 nextc = (s->inp+1 < s->inend) ? *s->inp : 0; // next char

  // CONSUME_CHAR advances the scanner to the next input byte
  #define CONSUME_CHAR() ({ s->inp++; s->tokend++; })

  // COND_CHAR includes next char in token if nextc==c. Returns tok1 if nextc==c, else tok2.
  #define COND_CHAR(c, tok2, tok1) \
    (nextc == (c)) ? ({ CONSUME_CHAR(); (tok1); }) : (tok2)

  // COND_CHAR_SEMIC is like COND_CHAR but sets insertSemi=true if nextc==c
  #define COND_CHAR_SEMIC(c, tok2, tok1) \
    (nextc == (c)) ? ({ CONSUME_CHAR(); insertSemi = true; (tok1); }) : (tok2)

  switch (c) {

  case '-':  // "-" | "->" | "--" | "-="
    switch (nextc) {
      case '>': s->tok = TRArr;        CONSUME_CHAR();                    break;
      case '-': s->tok = TMinusMinus;  CONSUME_CHAR(); insertSemi = true; break;
      case '=': s->tok = TMinusAssign; CONSUME_CHAR();                    break;
      default:  s->tok = TMinus;
    }
    break;

  case '+':  // "+" | "++" | "+="
    switch (nextc) {
      case '+': s->tok = TPlusPlus;   CONSUME_CHAR(); insertSemi = true; break;
      case '=': s->tok = TPlusAssign; CONSUME_CHAR();                    break;
      default:  s->tok = TPlus;
    }
    break;

  case '&':  // "&" | "&&" | "&="
    switch (nextc) {
      case '&': s->tok = TAndAnd;    CONSUME_CHAR(); break;
      case '=': s->tok = TAndAssign; CONSUME_CHAR(); break;
      default:  s->tok = TAnd;
    }
    break;

  case '|':  // "|" | "||" | "|="
    switch (nextc) {
      case '|': s->tok = TPipePipe;   CONSUME_CHAR(); break;
      case '=': s->tok = TPipeAssign; CONSUME_CHAR(); break;
      default:  s->tok = TPipe;
    }
    break;

  case '/': // "/" | "/=" | "//" | "/*"
    switch (nextc) {
      case '=': CONSUME_CHAR(); s->tok = TSlashAssign; break;       // "/="
      case '/': CONSUME_CHAR(); scomment(s);       goto scan_again; // "//"
      case '*': CONSUME_CHAR(); scomment_block(s); goto scan_again; // "/*"
      default:  s->tok = TSlash;
    }
    break;

  case '!': s->tok = COND_CHAR('=', TExcalm,  TNEq);           break; // "!" | "!="
  case '%': s->tok = COND_CHAR('=', TPercent, TPercentAssign); break; // "%" | "%="
  case '*': s->tok = COND_CHAR('=', TStar,    TStarAssign);    break; // "*" | "*="
  case '=': s->tok = COND_CHAR('=', TAssign,  TEq);            break; // "=" | "=="
  case '^': s->tok = COND_CHAR('=', THat,     THatAssign);     break; // "^" | "^="
  case '~': s->tok = COND_CHAR('=', TTilde,   TTildeAssign);   break; // "~" | "~="

  case '<': // "<" | "<=" | "<<" | "<<="
    switch (nextc) {
      case '=': s->tok = TLEq;  CONSUME_CHAR(); break;  // "<="
      case '<': // "<<" | "<<="
        CONSUME_CHAR();
        if (s->inp+1 < s->inend && *s->inp == '=') { // "<<="
          s->tok = TShlAssign; CONSUME_CHAR();
        } else { // "<<"
          s->tok = TShl;
        }
        break;
      default: s->tok = TLt; // <
    }
    break;

  case '>': // ">" | ">=" | ">>" | ">>="
    switch (nextc) {
      case '=': s->tok = TLEq;  CONSUME_CHAR(); break;  // ">="
      case '>': // ">>" | ">>="
        CONSUME_CHAR();
        if (s->inp+1 < s->inend && *s->inp == '=') { // ">>="
          s->tok = TShrAssign; CONSUME_CHAR();
        } else { // ">>"
          s->tok = TShr;
        }
        break;
      default: s->tok = TGt; // >
    }
    break;

  case '(': s->tok = TLParen;                    break;
  case ')': s->tok = TRParen; insertSemi = true; break;
  case '{': s->tok = TLBrace;                    break;
  case '}': s->tok = TRBrace; insertSemi = true; break;
  case '[': s->tok = TLBrack;                    break;
  case ']': s->tok = TRBrack; insertSemi = true; break;
  case ',': s->tok = TComma;                     break;
  case ';': s->tok = TSemi;                      break;
  case ':': s->tok = TColon;                     break;
  case '.': s->tok = TDot;                       break;

  case '0'...'9':
    snumber(s);
    insertSemi = true;
    break;

  case '$':
  case '_':
  case 'A'...'Z':
  case 'a'...'z':
    sname(s);
    switch (s->tok) {
      case TId:
      case TBreak:
      case TContinue:
      case TReturn:
      case TNil:
      case TStruct:
      case TAuto:
        insertSemi = true;
        break;
      default:
        break;
    }
    break;

  default:
    if (c >= RuneSelf) {
      s->inp--;
      snameuni(s);
      insertSemi = true;
      break;
    }
    // invariant: c < RuneSelf
    s->tokend = s->tokstart;
    s->tok = TNone;
    if (c >= 0x20 && c < 0x7F) {
      serr(s, "invalid input character '%C' 0x%x", c, c);
    } else {
      serr(s, "invalid input character 0x%x", c);
    }

  } // switch

  s->insertSemi = insertSemi;
  debug_token_production(s);
  return s->tok;
}
