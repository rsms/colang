// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"

void posmap_init(PosMap* pm) {
  array_init(&pm->a, pm->a_storage, sizeof(pm->a_storage));
  // the first slot is used to return NULL in pos_source for unknown positions
  pm->a.v[0] = NULL;
  pm->a.len++;
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
  if (b == NoPos)
    return a;
  if (a == NoPos)
    return b;
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


// returns false if memory allocation failed
static bool pos_add_context(const PosMap* pm, PosSpan span, Str* s, Source* src) {
  Pos start = span.start;
  Pos end = span.end;
  str_appendc(s, '\n');

  const u8* lineptr;
  u32 linelen;
  error err = source_line_bytes(src, pos_line(start), &lineptr, &linelen);
  if (err != 0)
    return err != err_nomem;

  str_append(s, (const char*)lineptr, linelen);
  str_appendc(s, '\n');

  // indentation
  u32 col = pos_col(start);
  if (col > 0) {
    // indentation
    if UNLIKELY(!str_appendfill(s, ' ', col - 1))
      return false;
  }

  // squiggle "~~~" or arrow "^"
  u32 width = pos_width(start);
  if (pos_isknown(end) &&
      pos_line(start) == pos_line(end) &&
      (start == end || pos_isbefore(start, end)))
  {
    width = (u32)(pos_col(end) - pos_col(start)) + pos_width(end);
  } // else if (pos_isknown(end)) TODO: spans lines

  if (width > 0) {
    str_appendfill(s, '~', width); // squiggle
    return str_appendc(s, '\n');
  }

  return str_appendcstr(s, "^\n");
}


bool pos_str(const PosMap* pm, Pos p, Str* dst) {
  const char* filename = "<input>";
  Source* src = (Source*)pos_source(pm, p);
  if (src)
    filename = src->filename;
  return str_appendfmt(dst, "%s:%u:%u", filename, pos_line(p), pos_col(p));
}


bool pos_fmtv(const PosMap* pm, PosSpan span, Str* dst, const char* fmt, va_list ap) {
  TStyles style = TStylesForStderr();

  // "file:line:col: message ..." <LF>
  str_appendcstr(dst, tstyle_str(style, TS_BOLD));
  pos_str(pm, span.start, dst);
  str_appendcstr(dst, ": ");
  str_appendcstr(dst, tstyle_str(style, TS_RESET));
  str_appendfmtv(dst, fmt, ap);

  // include line contents
  Source* src = (Source*)pos_source(pm, span.start);
  if (src)
    return pos_add_context(pm, span, dst, src);
  return str_appendc(dst, '\n');
}


bool pos_fmt(const PosMap* pm, PosSpan span, Str* dst, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  bool ok = pos_fmtv(pm, span, dst, fmt, ap);
  va_end(ap);
  return ok;
}
