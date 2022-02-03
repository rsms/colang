#include "../coimpl.h"
#include "../tstyle.h"
#include "pos.h"

void posmap_init(PosMap* pm, Mem mem) {
  PtrArrayInitStorage(&pm->a, pm->a_storage, countof(pm->a_storage));
  // the first slot is used to return NULL in pos_source for unknown positions
  pm->a.v[0] = NULL;
  pm->a.len++;
}

void posmap_dispose(PosMap* pm) {
  PtrArrayFree(&pm->a, pm->mem);
}

u32 posmap_origin(PosMap* pm, Source* source) {
  assert(source != NULL);
  for (u32 i = 0; i < pm->a.len; i++) {
    if (pm->a.v[i] == source)
      return i;
  }
  u32 i = pm->a.len;
  PtrArrayPush(&pm->a, source, pm->mem);
  return i;
}

Pos pos_with_adjusted_start(Pos p, i32 deltacol) {
  i32 c = (i32)pos_col(p);
  i32 w = (i32)pos_width(p);
  deltacol = (deltacol > 0) ? MIN(deltacol, w) : MAX(deltacol, -c);
  return pos_make_unchecked(
    pos_origin(p),
    pos_line(p),
    (u32)(c + deltacol),
    (u32)(w - deltacol)
  );
}

Pos pos_union(Pos a, Pos b) {
  if (pos_line(a) != pos_line(b)) {
    // cross-line pos union not supported (can't be expressed with Pos; use PosSpan instead)
    return a;
  }
  if (b < a) {
    Pos tmp = a;
    a = b;
    b = tmp;
  }
  u32 c = pos_col(a);
  u32 w = pos_width(a);
  u32 aend = c + w;
  u32 bstart = pos_col(b);
  u32 bw = pos_width(b);
  if (bstart > aend)
    aend = bstart;
  w = aend - c + bw;
  return pos_make_unchecked(pos_origin(a), pos_line(a), c, w);
}

static u32* compute_line_offsets(Source* s, Mem mem, u32* nlines_out) {
  if (!s->body)
    source_body_open(s);

  usize cap = 256; // best guess for common line numbers, to allocate up-front
  u32* lineoffs = (u32*)memalloc(mem, sizeof(u32) * cap);
  if (!lineoffs)
    return NULL;
  lineoffs[0] = 0;

  u32 linecount = 1;
  u32 i = 0;
  while (i < s->len) {
    if (s->body[i++] == '\n') {
      if (linecount == cap) {
        // more lines
        cap = cap * 2;
        lineoffs = (u32*)memrealloc(mem, lineoffs, sizeof(u32) * cap);
        if (!lineoffs) {
          memfree(mem, lineoffs);
          return NULL;
        }
      }
      lineoffs[linecount] = i;
      linecount++;
    }
  }
  *nlines_out = linecount;
  return lineoffs;
}

static const u8* src_line_contents(Source* s, Mem mem, u32 line, u32* out_len) {
  //
  // TODO: this implementation is pretty terrible. We can do better.
  //
  if (line == 0)
    return NULL;

  u32 nlines;
  u32* lineoffs = compute_line_offsets(s, mem, &nlines);
  if (!lineoffs)
    return NULL;
  if (line == 0 || line > nlines) {
    memfree(mem, lineoffs);
    return NULL;
  }

  u32 start = lineoffs[line - 1];
  const u8* lineptr = s->body + start;
  if (out_len) {
    if (line < nlines) {
      *out_len = (lineoffs[line] - 1) - start;
    } else {
      *out_len = (s->body + s->len) - lineptr;
    }
  }
  memfree(mem, lineoffs);
  return lineptr;
}

static Str pos_add_src_context(const PosMap* pm, PosSpan span, Str s, Source* src) {
  Pos start = span.start;
  Pos end = span.end;
  s = str_append(s, '\n');
  u32 linelen = 0;
  const u8* lineptr = src_line_contents(src, s->mem, pos_line(start), &linelen);
  if (lineptr)
    s = str_appendn(s, (const char*)lineptr, linelen);
  s = str_append(s, '\n');

  // indentation
  u32 col = pos_col(start);
  if (col > 0)
    s = str_appendfill(s, col - 1, ' '); // indentation

  // squiggle "~~~" or arrow "^"
  u32 width = pos_width(start);
  if (pos_isknown(end) &&
      pos_line(start) == pos_line(end) &&
      (start == end || pos_isbefore(start, end)))
  {
    width = (u32)(pos_col(end) - pos_col(start)) + pos_width(end);
  } // else if (pos_isknown(end)) TODO: spans lines

  if (width > 0) {
    s = str_appendfill(s, width, '~'); // squiggle
    s = str_append(s, '\n');
  } else {
    s = str_append(s, "^\n");
  }

  return s;
}


Str pos_fmtv(const PosMap* pm, PosSpan span, Str s, const char* fmt, va_list ap) {
  TStyles style = TStylesForStderr();

  // "file:line:col: message ..." <LF>
  s = str_append(s, TStyleStr(style, TS_BOLD));
  s = pos_str(pm, span.start, s);
  s = str_append(s, ": ");
  s = str_append(s, TStyleStr(style, TS_RESET));
  s = str_appendfmtv(s, fmt, ap);

  // include line contents
  Source* src = (Source*)pos_source(pm, span.start);
  if (src) {
    s = pos_add_src_context(pm, span, s, src);
  } else {
    s = str_append(s, '\n');
  }

  return s;
}

Str pos_fmt(const PosMap* pm, PosSpan span, Str s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s = pos_fmtv(pm, span, s, fmt, ap);
  va_end(ap);
  return s;
}

Str pos_str(const PosMap* pm, Pos p, Str s) {
  const char* filename = "<input>";
  Source* src = (Source*)pos_source(pm, p);
  if (src)
    filename = src->filename->p;
  return str_appendfmt(s, "%s:%u:%u", filename, pos_line(p), pos_col(p));
}
