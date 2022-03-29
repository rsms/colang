#include "../coimpl.h"
#include "../tstyle.h"
#include "pos.h"

void posmap_init(PosMap* pm) {
  array_init(&pm->a, pm->a_storage, sizeof(pm->a_storage));
  // the first slot is used to return NULL in pos_source for unknown positions
  pm->a.v[0] = NULL;
  pm->a.len++;
}

void posmap_dispose(PosMap* pm) {
  array_free(&pm->a);
}

u32 posmap_origin(PosMap* pm, Source* source) {
  assert(source != NULL);
  for (u32 i = 0; i < pm->a.len; i++) {
    if (pm->a.v[i] == source)
      return i;
  }
  u32 i = pm->a.len;
  array_push(&pm->a, source);
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

static u32* compute_line_offsets(Source* s, u32* nlinesp, usize* nbytesp) {
  if (!s->body)
    source_body_open(s);

  usize cap = 256; // best guess for common line numbers, to allocate up-front
  usize nbytes = sizeof(u32) * cap;
  u32* lineoffs = (u32*)memallocv(sizeof(u32), cap);
  if (!lineoffs)
    return NULL;
  lineoffs[0] = 0;

  u32 linecount = 1;
  u32 i = 0;
  while (i < s->len) {
    if (s->body[i++] == '\n') {
      if (linecount == cap) {
        // more lines
        usize newsize = nbytes * 2; // TODO check_mul_overflow
        u32* lineoffs2 = memresize(lineoffs, nbytes, newsize);
        if UNLIKELY(!lineoffs2) {
          memfree(lineoffs, nbytes);
          return NULL;
        }
        nbytes *= 2;
        lineoffs = lineoffs2;
      }
      lineoffs[linecount] = i;
      linecount++;
    }
  }
  *nlinesp = linecount;
  *nbytesp = nbytes;
  return lineoffs;
}

static const u8* src_line_contents(Source* s, u32 line, u32* out_len) {
  //
  // TODO: this implementation is pretty terrible. We can do better.
  //
  if (line == 0)
    return NULL;

  u32 nlines;
  usize nbytes;
  u32* lineoffs = compute_line_offsets(s, &nlines, &nbytes);
  if (!lineoffs)
    return NULL;
  if (line == 0 || line > nlines) {
    memfree(lineoffs, nbytes);
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
  memfree(lineoffs, nbytes);
  return lineptr;
}

// returns false if memory allocation failed
static bool pos_add_context(const PosMap* pm, PosSpan span, Str* s, Source* src) {
  Pos start = span.start;
  Pos end = span.end;
  str_push(s, '\n');
  u32 linelen = 0;
  const u8* lineptr = src_line_contents(src, pos_line(start), &linelen);
  if (lineptr)
    str_append(s, (const char*)lineptr, linelen);
  str_push(s, '\n');

  // indentation
  u32 col = pos_col(start);
  if (col > 0)
    str_appendfill(s, col - 1, ' '); // indentation

  // squiggle "~~~" or arrow "^"
  u32 width = pos_width(start);
  if (pos_isknown(end) &&
      pos_line(start) == pos_line(end) &&
      (start == end || pos_isbefore(start, end)))
  {
    width = (u32)(pos_col(end) - pos_col(start)) + pos_width(end);
  } // else if (pos_isknown(end)) TODO: spans lines

  if (width > 0) {
    str_appendfill(s, width, '~'); // squiggle
    return str_push(s, '\n');
  }

  return str_appendcstr(s, "^\n");
}


bool pos_str(const PosMap* pm, Pos p, Str* s) {
  const char* filename = "<input>";
  Source* src = (Source*)pos_source(pm, p);
  if (src)
    filename = src->filename;
  return str_appendfmt(s, "%s:%u:%u", filename, pos_line(p), pos_col(p));
}

bool pos_fmtv(const PosMap* pm, PosSpan span, Str* s, const char* fmt, va_list ap) {
  TStyles style = TStylesForStderr();

  // "file:line:col: message ..." <LF>
  str_appendcstr(s, tstyle_str(style, TS_BOLD));
  pos_str(pm, span.start, s);
  str_appendcstr(s, ": ");
  str_appendcstr(s, tstyle_str(style, TS_RESET));
  str_appendfmtv(s, fmt, ap);

  // include line contents
  Source* src = (Source*)pos_source(pm, span.start);
  if (src)
    return pos_add_context(pm, span, s, src);
  return str_push(s, '\n');
}

bool pos_fmt(const PosMap* pm, PosSpan span, Str* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  bool ok = pos_fmtv(pm, span, s, fmt, ap);
  va_end(ap);
  return ok;
}
