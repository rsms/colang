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


const char* TokName(Tok t) {
  switch (t) {
    #define I_ENUM(name, str) case name: return str;
    DEF_TOKENS(I_ENUM)
    #undef I_ENUM

    #define I_ENUM(name, str) case name: return "keyword \"" #str "\"";
    DEF_TOKENS_KEYWORD(I_ENUM)
    #undef I_ENUM

    #if DEBUG
      case TKeywordsStart: return "TKeywordsStart";
      case TKeywordsEnd:   return "TKeywordsEnd";
    #else
      case TKeywordsStart:
      case TKeywordsEnd:
        break;
    #endif // DEBUG
  }
  return "?"; // TODO
}


error ScannerInit(Scanner* s, BuildCtx* build, Source* src, ParseFlags flags) {
  error err = source_body_open(src);
  if (err)
    return err;

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

  str_init(&s->sbuf, s->sbuf_storage, sizeof(s->sbuf_storage));

  s->linestart = s->inp;
  s->lineno    = 1;

  s->comments_head = NULL;
  s->comments_tail = NULL;

  return 0;
}

void ScannerDispose(Scanner* s) {
  if (s->indentStack.v != s->indentStack.storage)
    mem_free(s->build->mem, s->indentStack.v, sizeof(Indent) * s->indentStack.cap);

  // free comments
  for (Comment* c; (c = ScannerCommentPop(s)); )
    mem_free(s->build->mem, c, sizeof(Comment));
}

static u32 scolumn_at(const Scanner* s, const u8* src) {
  const u8* p = src;
  while (*p != '\n' && p > s->src->body)
    p--;
  return (u32)(uintptr)(src - p);
}

static u32 scolumn(const Scanner* s) {
  if (s->tokstart >= s->linestart)
    return 1 + (u32)(uintptr)(s->tokstart - s->linestart);
  return 1;
}

Pos ScannerPos(const Scanner* s) {
  assert(s->tokend >= s->tokstart);
  u32 width = s->tokend - s->tokstart;
  return pos_make(s->srcposorigin, s->lineno, scolumn(s), width);
}


// serr is called when an error occurs. It invokes s->errh
static void serr(Scanner* s, const char* fmt, ...) {
  Pos pos = ScannerPos(s);
  va_list ap;
  va_start(ap, fmt);
  b_diagv(s->build, DiagError, (PosSpan){pos, pos}, fmt, ap);
  va_end(ap);
}


#ifdef SCANNER_DEBUG_TOKEN_PRODUCTION
  static bool tok_has_value(Tok t) {
    return t == TId || t == TIntLit || t == TFloatLit;
  }
  static void debug_token_production(Scanner* s) {
    Str posstr = pos_str(&s->build->posmap, ScannerPos(s), str_make(s->build->mem, 32));
    static usize vallen_max = 8; // global; yolo
    const int tokname_max = (int)strlen("keyword interface");
    usize vallen = 0;
    const char* valptr = NULL;
    if (tok_has_value(s->tok))
      valptr = (const char*)ScannerTokStr(s, &vallen);

    vallen_max = MAX(vallen_max, vallen);
    dlog(">> %-*s %.*s%*s %s",
        tokname_max, TokName(s->tok),
        (int)vallen, valptr,
        (int)(vallen_max - vallen), "",
        posstr->p);
    str_free(posstr);
  }
#else
  #define debug_token_production(s) do{}while(0)
#endif


Comment* ScannerCommentPop(Scanner* s) {
  Comment* c = s->comments_head;
  if (c) {
    s->comments_head = c->next;
    c->next = NULL;
  }
  return c;
}


static void snewline(Scanner* s) {
  s->lineno++;
  s->linestart = s->inp + 1;
}


static void comments_push_back(Scanner* s) {
  Comment* c = mem_alloct(s->build->mem, Comment);
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
        snewline(s);
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
    } else if (!utf8_decode(&s->inp, s->inend, &r)) {
      serr(s, "invalid UTF-8 data");
    }
  }
  s->tokend = s->inp;
  s->name = symget(&s->build->syms, (const char*)s->tokstart, s->tokend - s->tokstart);
  s->tok = langtok(s->name); // TId or a T* keyword
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
  usize len = (usize)(uintptr)(s->tokend - s->tokstart);
  s->name = symget(&s->build->syms, (const char*)s->tokstart, len);
  s->tok = langtok(s->name); // TId or a T* keyword
}


static void snumber(Scanner* s) {
  while (s->inp < s->inend) {
    u8 c = *s->inp;
    if (!ascii_isdigit(c))
      break;
    s->inp++;
  }
  s->tokend = s->inp;
  s->tok = TIntLit;
}


static u32 sstring_multiline(Scanner* s, const u8* start, const u8* end) {
  // Note: We perform manual source position calculations in this function
  // since the scanner is already positioned at the end of the string literal.
  if UNLIKELY(*start != '\n') {
    b_errf(s->build, (PosSpan){ pos_with_width(s->tokpos, 2) },
      "multiline string must start with \"|\" on a new line");
    return U32_MAX;
  }

  u32 extralen = 0;
  const u8* src = start;
  const u8* ind = start;
  const u8* linestart = start;
  u32 indlen = U32_MAX;
  u32 lineno = pos_line(s->tokpos);

  while (src != end) {
    if (*src++ != '\n')
      continue;

    lineno++;
    linestart = src;

    // find '|', leaving while loop with src positioned just after '|'
    u8 c = 0;
    while (src != end) {
      c = *src++;
      if (c == '|') break;
      if UNLIKELY(c != ' ' && c != '\t') {
        end = src; // trigger error message below
        break;
      }
    }
    if UNLIKELY(src == end && c != '|') {
      u32 col = (u32)(uintptr)(src - linestart);
      PosSpan ps = { pos_make(pos_origin(s->tokpos), lineno, col, 0) };
      b_errf(s->build, ps, "missing \"|\" after linebreak in multiline string");
      return U32_MAX;
    }

    u32 pipeoffs = (u32)(uintptr)((src - 1) - linestart);
    extralen += pipeoffs;

    if (indlen == U32_MAX) {
      indlen = pipeoffs;
      ind = linestart;
    } else if UNLIKELY(indlen != pipeoffs || memcmp(linestart, ind, pipeoffs) != 0) {
      u32 col = (u32)(uintptr)(src - linestart);
      PosSpan ps = { pos_make(pos_origin(s->tokpos), lineno, col, 0) };
      b_errf(s->build, ps, "inconsitent indentation of multiline string");
      return U32_MAX;
    }
  }

  if UNLIKELY(indlen == U32_MAX) {
    // no "|" found
    PosSpan ps = { s->tokpos };
    b_errf(s->build, ps, "missing \"|\" in multiline string");
    return U32_MAX;
  }

  return extralen;
}


static bool schar_escape(Scanner* s, const u8** srcp, const u8* end, u32* v) {
  // enters with src positioned just after "\"
  u8 b = *(*srcp)++;
  u32 n; // extra bytes to read as an integer
  switch (b) {
    case '"':
    case '\'':
    case '\\': *v = (u32)b;   return true;
    case '0':  *v = (u32)0;   return true;
    case 'a':  *v = (u32)0x7; return true;
    case 'b':  *v = (u32)0x8; return true;
    case 't':  *v = (u32)0x9; return true;
    case 'n':  *v = (u32)0xA; return true;
    case 'v':  *v = (u32)0xB; return true;
    case 'f':  *v = (u32)0xC; return true;
    case 'r':  *v = (u32)0xD; return true;
    case 'x':  n = 2; break; // \xXX
    case 'u':  n = 4; break; // \uXXXX
    case 'U':  n = 8; break; // \UXXXXXXXX
    default: return false;
  }

  u32 availsrc = (u32)(uintptr)(end - *srcp);
  if UNLIKELY(availsrc < n)
    return false;

  error err = sparse_u32((const char*)*srcp, n, 16, v);
  *srcp += n;
  return !err;
}


static void sstring_finalize(Scanner* s) {
  s->tokend = s->inp;
  if (scolumn(s) == pos_col(s->tokpos))
    pos_set_width(&s->tokpos, (u32)(uintptr)(s->tokend - s->tokstart));

  const u8* badbyte = utf8_validate((const u8*)s->sval.p, (usize)s->sval.len);

  if UNLIKELY(badbyte) {
    Pos p = s->tokpos;
    if (scolumn(s) == pos_col(s->tokpos) && badbyte != (const u8*)s->sval.p) {
      // point to the bad data
      pos_set_width(&p, 0);
      p = pos_with_col(p, pos_col(p) + (u32)(uintptr)(badbyte - (const u8*)s->sval.p) + 1);
    }
    b_errf(s->build, (PosSpan){p}, "invalid UTF-8 data");
  }
}


static void sstring_buffered(Scanner* s, u32 extralen, bool ismultiline) {
  const u8* src = s->tokstart + 1; // +1 to skip initial '"'
  u32 maxlen = CAST_U32((s->inp - 1) - src);
  if UNLIKELY(maxlen == U32_MAX)
    return serr(s, "string literal too large");

  // track source position
  u32 lineno = pos_line(s->tokpos);

  // calculate effective string length for multiline strings
  if (ismultiline) {
    // verify indentation and calculate nbytes used for indentation
    u32 indentextralen = sstring_multiline(s, src, src + maxlen);
    if UNLIKELY(indentextralen == U32_MAX) // an error occured
      return;

    // Add the number of bytes used for multiline syntax to extralen.
    // Overflow should not be possible since we checked it earlier in this function.
    assert((u64)extralen + (u64)indentextralen <= (u64)U32_MAX);
    extralen += indentextralen;

    // sans leading '\n'
    src++;
    maxlen--;
    lineno++;
  }

  assert(extralen <= maxlen);
  maxlen -= extralen;

  // allocate space needed up front since we already know the size
  if UNLIKELY(!str_reserve(&s->sbuf, maxlen)) {
    serr(s, "failed to allocate memory for string literal");
    return;
  }

  // set sval to point to sbuf
  s->sbuf.len = 0; // reset
  u8* dst = (u8*)s->sbuf.v;

  const u8* chunkstart = src;

  #define FLUSH_BUF() { \
    usize nbyte = (usize)((src) - chunkstart); \
    memcpy(dst, chunkstart, nbyte); \
    dst += nbyte; \
  }

  if (ismultiline) {
    while (src < s->inend && *src++ != '|') {}
    chunkstart = src;
  }

  while (src < s->inend) {
    switch (*src) {
      case '\\': {
        FLUSH_BUF();
        src++;
        const u8* src_start = src;
        u32 value;
        bool ok = schar_escape(s, &src, s->inend, &value);
        u32 nbytes_consumed = (u32)(uintptr)(src - src_start);
        if UNLIKELY(!ok) {
          Pos p = pos_with_width(s->tokpos, nbytes_consumed + 1); // +1 for '\'
          p = pos_with_line(p, lineno);
          p = pos_with_col(p, scolumn_at(s, src_start - 1));
          b_errf(s->build, (PosSpan){p}, "invalid string escape sequence");
          return;
        }
        *dst++ = value; // optimistically assume \xXX
        if (nbytes_consumed > 3) {
          // \uXXXX or \UXXXXXXXX
          dst--; // undo advance for \xXX
          if UNLIKELY(!utf8_encode(&dst, dst + 4, value)) {
            Pos p = pos_with_width(s->tokpos, nbytes_consumed + 1); // +1 for '\'
            p = pos_with_line(p, lineno);
            p = pos_with_col(p, scolumn_at(s, src_start - 1));
            b_errf(s->build, (PosSpan){p}, "invalid Unicode codepoint U+%04X", value);
            return;
          }
        } else {
          assert(value <= 0xff);
        }
        chunkstart = src;
        break;
      }
      case '\n':
        src++;
        lineno++;
        FLUSH_BUF();
        // note: sstring_multiline has verified syntax already
        while (*src++ != '|') {}
        chunkstart = src;
        break;
      case '"':
        goto done;
      default:
        src++;
    }
  }
done:
  FLUSH_BUF();
  s->sval.p = s->sbuf.v;
  s->sval.len = (usize)(uintptr)(dst - (u8*)s->sval.p);
  assertf(s->sval.len <= maxlen, "dst overflow: %u > %u", s->sval.len, maxlen);
  return sstring_finalize(s);
}


static void sstring(Scanner* s) {
  // Optimistically assumes the string literal is verbatim.
  // Accumulate number of "extra bytes" encountered from escapes in extralen.
  // If the string is not verbatim, switch to sstring_buffered.
  s->insertSemi = true;
  u32 extralen = 0;
  bool ismultiline = false;
  s->tokpos = ScannerPos(s);

  while (s->inp < s->inend) {
    u8 c = *s->inp;
    switch (c) {
      case '\\': {
        s->inp++;
        extralen++;
        // note: you might wonder why we're setting extralen exactly here for the
        // cases of \xXX \uXXXX and \UXXXXXXXX.
        //
        // extralen is a number to _subtract_ from the string literal as it
        // appears in the source text. I.e. "foo\nbar\n" is 10 bytes of input but
        // results in only 8 bytes of actual text data; extralen is 2 because of
        // the two "\n" escapes. That's easy as we know all "verbatim" \c escapes
        // results in exactly one byte overhead, but that's not true for \x \u \U
        // escapes. In particular, \u and \U yields 1-4 bytes of actual text data,
        // depending on their Unicode value (and thus its UTF-8 encoding.)
        //
        // So we know that the overhead is _at least_ 2 bytes, but that's it.
        //
        break;
      }
      case '\n':
        pos_set_width(&s->tokpos, scolumn(s) - pos_col(s->tokpos));
        snewline(s);
        ismultiline = true;
        extralen++;
        break;
      case '"': {
        s->inp++;
        s->tokend = s->inp;
        if (extralen || ismultiline)
          return sstring_buffered(s, extralen, ismultiline);
        pos_set_width(&s->tokpos, scolumn(s) - pos_col(s->tokpos) - 1);
        usize len = (usize)(uintptr)(s->inp - s->tokstart) - 2; // -2 to skip '"'
        if UNLIKELY(len >= (usize)U32_MAX) {
          b_errf(s->build, (PosSpan){ s->tokpos }, "string literal too large");
          return;
        }
        s->sval.p = (const char*)s->tokstart + 1;
        s->sval.len = (u32)len;
        return sstring_finalize(s);
      }
    }
    s->inp++;
  }

  // we get here if the string is not terminated
  s->sval.p = "";
  s->sval.len = 0;
  s->tokend = s->inp;
  serr(s, "unterminated string literal");
}


static void indent_stack_grow(Scanner* s) {
  u32 newcap = s->indentStack.cap * 2;
  if (s->indentStack.v != s->indentStack.storage) {
    s->indentStack.v = mem_resizev(
      s->build->mem, s->indentStack.v, sizeof(Indent), s->indentStack.cap, newcap);
  } else {
    // moving array from stack to heap
    Indent* v = (Indent*)mem_allocv(s->build->mem, sizeof(Indent), newcap);
    memcpy(v, s->indentStack.v, sizeof(Indent) * s->indentStack.len);
    s->indentStack.v = v;
  }
  s->indentStack.cap = newcap;
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

  if (UNLIKELY(s->indentStack.len == s->indentStack.cap))
    indent_stack_grow(s);

  s->indentStack.v[s->indentStack.len++] = s->indent;
  s->indent = s->indentDst;
}


static bool indent_pop(Scanner* s) {
  // decrease indentation
  assert(s->indent.n > s->indentDst.n);

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


void ScannerNext(Scanner* s) {
  s->prevtokend = s->tokend;
  scan_again: {}  // jumped to when comments are skipped
  // dlog("-- '%c' 0x%02X (%zu)", *s->inp, *s->inp, (usize)(s->inp - s->src->body));

  // unwind >1-level indent
  if (s->indent.n > s->indentDst.n) {
    bool isblock = indent_pop(s);
    if (isblock) {
      s->tok = TRBrace;
      debug_token_production(s);
      return;
    }
  }

  // whitespace
  bool islnstart = s->inp == s->linestart;
  while (s->inp < s->inend && (charflags[*s->inp] & CH_WHITESPACE)) {
    if (*s->inp == '\n') {
      snewline(s);
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
        return;
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
          return;
        }
      }
      if (s->insertSemi) {
        s->insertSemi = false;
        s->tok = TSemi;
        debug_token_production(s);
        return;
      }
    }
  }

  // EOF
  if (UNLIKELY(s->inp == s->inend)) {
    s->tokstart = s->inp - 1;
    s->tokend = s->tokstart;
    s->indentDst.n = 0;
    if (s->indent.n > 0 && indent_pop(s) /*isblock*/) {
      // decrease indentation to 0 if source ends at indentation
      s->tok = TRBrace;
      s->insertSemi = false;
      debug_token_production(s);
      return;
    }
    if (s->insertSemi) {
      s->insertSemi = false;
      s->tok = TSemi;
    } else {
      s->tok = TNone;
    }
    debug_token_production(s);
    return;
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
  case '"': s->tok = TStrLit; MUSTTAIL return sstring(s);

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
      case TUnsafe:
      case TVar:
      case TConst:
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
}
