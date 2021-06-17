#include <rbase/rbase.h>
#include "sexpr.h"

typedef struct SExprParser {
  Mem       mem;   // memory allocator for SExpr nodes
  const u8* start; // start of source
  const u8* end;   // end of source
  const u8* curr;  // current pointer in source
} SExprParser;


static SExpr* sexpr_parse_atom(SExprParser* p, SExpr* atom) {
  atom->atom.name = (const char*)p->curr;
  while (p->curr < p->end) {
    auto b = *p->curr;
    //dlog("A>> %C 0x%02X", b, b);
    switch (b) {
      case ' ': case '\t':
      case '\r': case '\n':
      case '(': case ')':
      case '[': case ']':
      case '{': case '}':
        goto end;
      default:
        break;
    }
    p->curr++;
  }
end:
  atom->atom.namelen = (u32)(uintptr_t)((const char*)p->curr - atom->atom.name);
  //dlog("A>> \"%.*s\"", (int)atom->atom.namelen, atom->atom.name);
  return atom;
}


inline static u8 sexpr_endtok(u8 starttok) {
  // ASCII/UTF8: ( ) ... [ \ ] ... { | }
  return starttok == '(' ? starttok + 1 : starttok + 2;
}


static SExpr* sexpr_parse_list(SExprParser* p, u8 endtok, SExpr* list) {
  SExpr* tail = NULL;
  while (p->curr < p->end) {
    auto b = *p->curr++;
    //dlog("L>> %C 0x%02X", b, b);
    SExpr* n = NULL;
    switch (b) {
      case ' ': case '\t':
      case '\r': case '\n':
        break;
      case '(': case '[': case '{':
        n = memalloct(p->mem, SExpr);
        n->list.kind = b;
        sexpr_parse_list(p, sexpr_endtok(b), n);
        break;
      case ')': case ']': case '}':
        if (b != endtok) {
          errlog("sexpr_parse_list: unexpected '%C' at offset %zu (expected '%C')",
                 b, (size_t)(p->curr - p->start), endtok);
        }
        goto end;
      default:
        n = memalloct(p->mem, SExpr);
        n->type = SExprAtom;
        p->curr--;
        sexpr_parse_atom(p, n);
        break;
    }

    if (n) {
      if (tail) {
        tail->next = n;
      } else {
        list->list.head = n;
      }
      tail = n;
    }
  }
end:
  return list;
}


SExpr* sexpr_parse(const u8* src, u32 srclen, Mem mem) {
  SExprParser p = {
    .mem   = mem,
    .start = src,
    .end   = src + srclen,
    .curr  = src,
  };
  SExpr* list = memalloct(p.mem, SExpr);
  list->list.kind = '(';
  return sexpr_parse_list(&p, '\0', list);
}


static Str sexpr_fmt1(SExprFmtFlags fl, const SExpr* n, Str s, int depth) {
  if (n->type == SExprAtom)
    return str_append(s, n->atom.name, n->atom.namelen);

  // SExprList
  s = str_appendc(s, n->list.kind);
  const SExpr* cn = n->list.head;
  bool linebreak = false;
  while (cn) {
    if (cn != n->list.head) {
      // separate values with linebreak either when its a list or if we have
      // already used linebreaks for this list.
      if ((fl & SExprFmtPretty) && (linebreak = linebreak || cn->type == SExprList)) {
        s = str_appendfmt(s, "\n%*s", (depth + 1) * 2, "");
      } else {
        s = str_appendc(s, ' ');
      }
    } else if ((fl & SExprFmtPretty) && cn->type == SExprList) {
      // special case for "((x))" -- list where the first child is another list
      s = str_appendfmt(s, "\n%*s", (depth + 1) * 2, "");
    }
    s = sexpr_fmt1(fl, cn, s, depth + 1);
    cn = cn->next;
  }
  s = str_appendc(s, sexpr_endtok(n->list.kind));

  return s;
}


Str sexpr_fmt(const SExpr* n, Str s, SExprFmtFlags fl) {
  return sexpr_fmt1(fl, n, s, 0);
}


Str sexpr_prettyprint(Str dst, const char* src, u32 srclen) {
  Mem mem = MemLinearAlloc();
  SExpr* root = sexpr_parse((const u8*)src, srclen, mem);
  dst = str_makeroom(dst, srclen);
  if (root)
    dst = sexpr_fmt(root, dst, SExprFmtPretty);
  MemLinearFree(mem);
  return dst;
}


static void sexpr_free1(Mem mem, SExpr* n) {
  if (n->type == SExprList) {
    SExpr* cn = n->list.head;
    while (cn) {
      auto next_cn = cn->next;
      sexpr_free1(mem, cn);
      cn = next_cn;
    }
  }
  memfree(mem, n);
}

void sexpr_free(SExpr* n, Mem mem) {
  sexpr_free1(mem, n);
}


// --------------------------------------------------------------------------------------
// test

R_TEST(sexpr_parse) {
  auto src = "hello [world 123 foo/bar {456(X Y Z)}] a + c ()";
  SExpr* n = sexpr_parse((const u8*)src, strlen(src), MemLibC());

  Str s = sexpr_fmt(n, str_new(32), SExprFmtDefault);
  // dlog("sexpr_fmt:\n%s", s);
  assertcstreq("(hello [world 123 foo/bar {456 (X Y Z)}] a + c ())", s);

  s = sexpr_fmt(n, str_trunc(s), SExprFmtPretty);
  // dlog("sexpr_fmt:\n%s", s);
  assertcstreq(
    "(hello\n"
    "  [world 123 foo/bar\n"
    "    {456\n"
    "      (X Y Z)}]\n"
    "  a\n"
    "  +\n"
    "  c\n"
    "  ())"
    "", s);

  // "((x))" should linebreak after first "("
  src = "(x)"; // parses as "((x))"
  n = sexpr_parse((const u8*)src, strlen(src), MemLibC());
  s = sexpr_fmt(n, str_trunc(s), SExprFmtPretty);
  // dlog("sexpr_fmt:\n%s", s);
  assertcstreq(
    "(\n"
    "  (x))"
    "", s);

  str_free(s);

  assertnotnull(n);
  asserteq(n->type, SExprList);
  assertnotnull(n->list.head);
  // TODO: more assertions
  sexpr_free(n, MemLibC());
}

R_TEST(sexpr_prettyprint) {
  auto src = "hello [world 123 foo/bar {456(X Y Z)}] a + c ()";
  auto s = sexpr_prettyprint(str_new(32), src, strlen(src));
  // dlog("sexpr_prettyprint:\n%s", s);
  assertcstreq(
    "(hello\n"
    "  [world 123 foo/bar\n"
    "    {456\n"
    "      (X Y Z)}]\n"
    "  a\n"
    "  +\n"
    "  c\n"
    "  ())"
    "", s);
  str_free(s);
}
