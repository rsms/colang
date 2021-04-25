#include <rbase/rbase.h>
#include "parse.h"

// Enable to dlog ">> TOKEN VALUE at SOURCELOC" on each call to SNext
// #define SCANNER_DEBUG_TOKEN_PRODUCTION


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
/* 0x20 */ 2,0,0,0,0,0,0,0,0,0,0,1,0,1,1,0,
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


bool ScannerInit(Scanner* s, BuildCtx* ctx, Source* src, ParseFlags flags) {
  memset(s, 0, sizeof(Scanner));

  if (!SourceOpenBody(src))
    return false;

  s->ctx       = ctx;
  s->src       = src;
  s->inp       = src->body;
  s->inp0      = src->body;
  s->inend     = src->body + src->len;
  s->flags     = flags;
  s->linestart = s->inp;

  return true;
}

void ScannerDispose(Scanner* s) {
  // free comments
  while (1) {
    auto c = ScannerCommentPop(s);
    if (!c)
      break;
    memfree(s->ctx->mem, c);
  }
}


// serr is called when an error occurs. It invokes s->errh
static void serr(Scanner* s, const char* format, ...) {
  auto pos = ScannerSrcPos(s);

  va_list ap;
  va_start(ap, format);
  auto msg = str_new(64);
  if (strlen(format) > 0)
    msg = str_appendfmtv(msg, format, ap);
  va_end(ap);

  // either pass to error handler or print to stderr as a fallback
  if (s->ctx->errh) {
    s->ctx->errh(pos, msg, s->ctx->userdata);
  } else {
    // TODO: Consider SrcPosStr to add source position to msg
    msg[str_len(msg)] = '\n'; // replace NUL with ln
    fwrite(msg, str_len(msg) + 1, 1, stderr);
  }

  str_free(msg);
}



#ifdef SCANNER_DEBUG_TOKEN_PRODUCTION
  static bool tok_has_value(Tok t) {
    return t == TId || t == TIntLit || t == TFloatLit || t == TIndent;
  }
  static void debug_token_production(Scanner* s) {
    auto posstr = SrcPosStr(ScannerSrcPos(s), str_new(32));
    if (tok_has_value(s->tok)) {
      size_t vallen;
      const u8* valptr = ScannerTokStr(s, &vallen);
      dlog(">> %-7s \"%.*s\"\tat %s", TokName(s->tok), (int)vallen, valptr, posstr);
    } else {
      dlog(">> %-7s\tat %s", TokName(s->tok), posstr);
    }
    str_free(posstr);
  }
#else
  #define debug_token_production(s) do{}while(0)
#endif


// // unreadrune sets the reading position to a previous reading position,
// // usually the one of the most recently read rune, but possibly earlier
// // (see unread below).
// inline static void unreadrune(Scanner* s) {
//   s->inp = s->inp0;
// }


Comment* ScannerCommentPop(Scanner* s) {
  auto c = s->comments_head;
  if (c) {
    s->comments_head = c->next;
    c->next = NULL;
  }
  return c;
}


static void comments_push_back(Scanner* s) {
  auto c = (Comment*)memalloc(s->ctx->mem, sizeof(Comment));
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


static void scomment(Scanner* s) {
  s->tokstart++; // exclude '#'
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
  s->name = symget(s->ctx->syms, (const char*)s->tokstart, s->tokend - s->tokstart);
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
  s->name = symget(s->ctx->syms, (const char*)s->tokstart, s->tokend - s->tokstart);
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


Tok ScannerNext(Scanner* s) {
  scan_again: {}  // jumped to when comments are skipped
  // dlog("-- '%c' 0x%02X (%zu)", *s->inp, *s->inp, (size_t)(s->inp - s->src->body));

  // whitespace
  bool islnstart = s->inp == s->linestart; // for flags&ParseIndent
  while (s->inp < s->inend && (charflags[*s->inp] & CH_WHITESPACE)) {
    if (*s->inp == '\n') {
      s->lineno++;
      s->linestart = s->inp + 1;
      if (s->insertSemi) {
        s->insertSemi = false;
        s->tokstart = s->inp;
        s->tokend = s->tokstart;
        s->inp++;
        s->tok = TSemi;
        debug_token_production(s);
        return s->tok;
      }
      islnstart = true;
    }
    s->inp++;
  }

  // EOF
  if (s->inp == s->inend) {
    s->tokstart = s->inp - 1;
    s->tokend = s->tokstart;
    if (s->insertSemi) {
      s->insertSemi = false;
      s->tok = TSemi;
    } else {
      s->tok = TNone;
    }
    return s->tok;
  }

  // indentation
  if ((s->flags & ParseIndent) && islnstart && s->inp > s->linestart && *s->inp != '#') {
    s->tokstart = s->linestart;
    s->tokend = s->inp;
    s->tok = TIndent;
    debug_token_production(s);
    return s->tok;
  }

  bool insertSemi = false;
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
      default:  s->tok = TMinus;                                          break;
    }
    break;

  case '+':  // "+" | "++" | "+="
    switch (nextc) {
      case '+': s->tok = TPlusPlus;   CONSUME_CHAR(); insertSemi = true; break;
      case '=': s->tok = TPlusAssign; CONSUME_CHAR();                    break;
      default:  s->tok = TPlus;                                          break;
    }
    break;

  case '&':  // "&" | "&&" | "&="
    switch (nextc) {
      case '&': s->tok = TAndAnd;    CONSUME_CHAR(); break;
      case '=': s->tok = TAndAssign; CONSUME_CHAR(); break;
      default:  s->tok = TAnd;                       break;
    }
    break;

  case '|':  // "|" | "||" | "|="
    switch (nextc) {
      case '|': s->tok = TPipePipe;   CONSUME_CHAR(); break;
      case '=': s->tok = TPipeAssign; CONSUME_CHAR(); break;
      default:  s->tok = TPipe;                       break;
    }
    break;

  case '!': s->tok = COND_CHAR('=', TExcalm,  TNEq);           break; // "!" | "!="
  case '%': s->tok = COND_CHAR('=', TPercent, TPercentAssign); break; // "%" | "%="
  case '*': s->tok = COND_CHAR('=', TStar,    TStarAssign);    break; // "*" | "*="
  case '/': s->tok = COND_CHAR('=', TSlash,   TSlashAssign);   break; // "/" | "/="
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
      default: s->tok = TLt; break; // <
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
      default: s->tok = TGt; break; // >
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

  case '#': // line comment
    // TODO: consider multiline/inline comment '#* ... *#' ?
    scomment(s);
    goto scan_again;
    break;

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
    break;

  } // switch

  s->insertSemi = insertSemi;
  debug_token_production(s);
  return s->tok;
}
