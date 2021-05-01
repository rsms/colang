#include <rbase/rbase.h>
#include "pos.h"
#include "util/tstyle.h"
#include "build.h"

void posmap_init(PosMap* pm, Mem nullable mem) {
  ArrayInitWithStorage(&pm->a, pm->a_storage, countof(pm->a_storage));
  // the first slot is used to return NULL in pos_source for unknown positions
  pm->a.v[0] = NULL;
  pm->a.len++;
}

void posmap_dispose(PosMap* pm) {
  ArrayFree(&pm->a, pm->mem);
}

u32 posmap_origin(PosMap* pm, void* origin) {
  assertnotnull(origin);
  for (u32 i = 0; i < pm->a.len; i++) {
    if (pm->a.v[i] == origin)
      return i;
  }
  u32 i = pm->a.len;
  ArrayPush(&pm->a, origin, pm->mem);
  return i;
}

static u32* computeLineOffsets(Source* s, u32* nlines_out) {
  if (!s->body)
    SourceOpenBody(s);

  size_t cap = 256; // best guess for common line numbers, to allocate up-front
  u32* lineoffs = (u32*)memalloc(NULL, sizeof(u32) * cap);
  lineoffs[0] = 0;

  u32 linecount = 1;
  u32 i = 0;
  while (i < s->len) {
    if (s->body[i++] == '\n') {
      if (linecount == cap) {
        // more lines
        cap = cap * 2;
        lineoffs = (u32*)memrealloc(NULL, lineoffs, sizeof(u32) * cap);
      }
      lineoffs[linecount] = i;
      linecount++;
    }
  }
  *nlines_out = linecount;
  return lineoffs;
}

static const u8* src_line_contents(Source* s, u32 line, u32* out_len) {
  //
  // TODO: this implementation is pretty terrible. We can do better.
  //
  u32 nlines;
  u32* lineoffs = computeLineOffsets(s, &nlines);
  if (line == 0 || line > nlines)
    return NULL;
  auto start = lineoffs[line - 1];
  const u8* lineptr = s->body + start;
  if (out_len) {
    if (line < nlines) {
      *out_len = (lineoffs[line] - 1) - start;
    } else {
      *out_len = (s->body + s->len) - lineptr;
    }
  }
  memfree(NULL, lineoffs);
  return lineptr;
}

static Str pos_add_src_context(const PosMap* pm, Pos start, Pos end, Str s, Source* src) {
  s = str_appendc(s, '\n');
  u32 linelen = 0;
  auto lineptr = src_line_contents(src, pos_line(start), &linelen);
  if (lineptr)
    s = str_append(s, (const char*)lineptr, linelen);
  s = str_appendc(s, '\n');

  // indentation
  u32 col = pos_col(start);
  if (col > 0)
    s = str_appendfill(s, col - 1, ' '); // indentation

  // squiggle "~~~" or arrow "^"
  u32 span = pos_span(start);
  dlog("endpos: %s", pos_str(pm, end, str_new(32)));
  if (pos_isknown(end)) {
    if (pos_line(start) == pos_line(end) && pos_isbefore(start, end))
      span = (u32)(pos_col(end) - pos_col(start)) + pos_span(end);
    // else: TODO: span lines
  }
  if (span > 0) {
    s = str_appendfill(s, span, '~'); // squiggle
    s = str_appendc(s, '\n');
  } else {
    s = str_appendcstr(s, "^\n");
  }
  return s;
}


Str pos_fmtv(const PosMap* pm, Pos start, Pos end, Str s, const char* fmt, va_list ap) {
  TStyleTable style = TStyle16;

  // "file:line:col: message ..."
  s = str_appendcstr(s, style[TStyle_bold]);
  s = pos_str(pm, start, s);
  s = str_appendcstr(s, ": ");
  s = str_appendcstr(s, style[TStyle_none]);
  s = str_appendfmtv(s, fmt, ap);

  // include line contents
  auto src = (Source*)pos_source(pm, start);
  if (src)
    s = pos_add_src_context(pm, start, end, s, src);

  return s;
}

Str pos_fmt(const PosMap* pm, Pos start, Pos end, Str s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s = pos_fmtv(pm, start, end, s, fmt, ap);
  va_end(ap);
  return s;
}

Str pos_str(const PosMap* pm, Pos p, Str s) {
  const char* filename = "<input>";
  size_t filenameLen = strlen("<input>");
  auto src = (Source*)pos_source(pm, p);
  if (src) {
    filename = src->filename;
    filenameLen = strlen(filename);
  }
  return str_appendfmt(s, "%s:%u:%u", filename, pos_line(p), pos_col(p));
}


R_UNIT_TEST(pos) {
  PosMap pm;
  posmap_init(&pm, NULL);
  uintptr_t source1 = 1;
  uintptr_t source2 = 2;

  // allocate origins for two sources
  auto o1 = posmap_origin(&pm, (void*)source1);
  auto o2 = posmap_origin(&pm, (void*)source2);
  assertop(o1,<,o2);

  // should get the same origin values on subsequent queries
  asserteq(o1, posmap_origin(&pm, (void*)source1));
  asserteq(o2, posmap_origin(&pm, (void*)source2));

  // make some positions (origin, line, column, span)
  auto p1_1_1 = pos_make(o1, 1, 1, 5);
  auto p1_1_9 = pos_make(o1, 1, 9, 4);
  auto p1_7_3 = pos_make(o1, 7, 3, 6);

  auto p2_1_1 = pos_make(o2, 1, 1, 5);
  auto p2_1_9 = pos_make(o2, 1, 9, 4);
  auto p2_7_3 = pos_make(o2, 7, 3, 6);

  // lookup source
  asserteq((uintptr_t)pos_source(&pm, p1_1_1), source1);
  asserteq((uintptr_t)pos_source(&pm, p1_1_9), source1);
  asserteq((uintptr_t)pos_source(&pm, p1_7_3), source1);
  asserteq((uintptr_t)pos_source(&pm, p2_1_1), source2);
  asserteq((uintptr_t)pos_source(&pm, p2_1_9), source2);
  asserteq((uintptr_t)pos_source(&pm, p2_7_3), source2);

  // make sure line and column getters works as expected
  asserteq(pos_line(p1_1_1), 1); asserteq(pos_col(p1_1_1), 1); asserteq(pos_span(p1_1_1), 5);
  asserteq(pos_line(p1_1_9), 1); asserteq(pos_col(p1_1_9), 9); asserteq(pos_span(p1_1_9), 4);
  asserteq(pos_line(p1_7_3), 7); asserteq(pos_col(p1_7_3), 3); asserteq(pos_span(p1_7_3), 6);
  asserteq(pos_line(p2_1_1), 1); asserteq(pos_col(p2_1_1), 1); asserteq(pos_span(p2_1_1), 5);
  asserteq(pos_line(p2_1_9), 1); asserteq(pos_col(p2_1_9), 9); asserteq(pos_span(p2_1_9), 4);
  asserteq(pos_line(p2_7_3), 7); asserteq(pos_col(p2_7_3), 3); asserteq(pos_span(p2_7_3), 6);

  // known
  asserteq(pos_isknown(NoPos), false);
  assert(pos_isknown(p1_1_1));
  assert(pos_isknown(p1_1_9));
  assert(pos_isknown(p1_7_3));
  assert(pos_isknown(p2_1_1));
  assert(pos_isknown(p2_1_9));
  assert(pos_isknown(p2_7_3));

  // pos_isbefore
  assert(pos_isbefore(p1_1_1, p1_1_9)); // column 1 is before column 9
  assert(pos_isbefore(p1_1_9, p1_7_3)); // line 1 is before line 7
  assert(pos_isbefore(p1_7_3, p2_1_1)); // o1 is before o2
  assert(pos_isbefore(p1_1_1, p2_1_1)); // o1 is before o2
  assert(pos_isbefore(p2_1_1, p2_1_9)); // column 1 is before column 9
  assert(pos_isbefore(p2_1_9, p2_7_3)); // line 1 is before line 7

  // pos_isafter
  assert(pos_isafter(p1_1_9, p1_1_1)); // column 9 is after column 1
  assert(pos_isafter(p1_7_3, p1_1_9)); // line 7 is before line 1
  assert(pos_isafter(p2_1_1, p1_7_3)); // o2 is after o1
  assert(pos_isafter(p2_1_1, p1_1_1)); // o2 is after o1
  assert(pos_isafter(p2_1_9, p2_1_1)); // column 9 is before column 1
  assert(pos_isafter(p2_7_3, p2_1_9)); // line 7 is before line 1


  posmap_dispose(&pm);
  // exit(0);
}


R_UNIT_TEST(pos_fuzz) {
  //
  // A kind of basic quickcheck: for up to mintime call pos_make with random values within
  // expected valid bounds and then verify that the "read" functions returns the expected values.
  // On an older macbook this runs about 1 000 000 iterations in 50ms in debug mode.
  //
  u64 mintime = 50*1000000; // 50ms
  auto starttm = nanotime();
  u32 randseed = (u32)starttm;
  //
  // if a test would fail, replace the value here with the seed reported for the failure:
  srandom(randseed);
  //
  while (nanotime() - starttm < mintime) {
    u32 origin = ((u32)random()) % (_pos_originMax+1);
    u32 line   = ((u32)random()) % (_pos_lineMax+1);
    u32 col    = ((u32)random()) % (_pos_colMax+1);
    u32 span   = ((u32)random()) % (_pos_spanMax+1);

    if (origin == 0 && line == 0 && col == 0 && span == 0)
      continue; // retry

    auto p = pos_make(origin, line, col, span);
    bool failed = false;
    failed = failed || (p == NoPos);
    failed = failed || pos_origin(p) != origin;
    failed = failed || pos_line(p) != line;
    failed = failed || pos_col(p) != col;
    failed = failed || pos_span(p) != span;
    if (failed) {
      fprintf(stderr, "seed: srandom(%u)\n", randseed);
      fprintf(stderr, "pos_make(%u, %u, %u, %u)\n", origin, line, col, span);
      asserteq(pos_origin(p), origin);
      asserteq(pos_line(p), line);
      asserteq(pos_col(p), col);
      asserteq(pos_span(p), span);
      assert(p != NoPos);
    }
  }
}
