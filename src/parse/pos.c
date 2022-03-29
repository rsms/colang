// compact representation of source positions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PARSE_POS_IMPLEMENTATION
#endif
#include "source.c"
#include "string.c"
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

// Pos is a compact representation of a source position: source file, line and column.
// Limits: 1048575 number of sources, 1048575 max lines, 4095 max columns, 4095 max width.
// Inspired by the Go compiler's xpos & lico.
typedef u64            Pos;
typedef struct PosMap  PosMap;  // maps Source to Pos indices
typedef struct PosSpan PosSpan; // span in a Source

struct PosMap {
  SourceArray a;
  Source*     a_storage[32]; // slot 0 is always NULL
};

struct PosSpan {
  Pos start;
  Pos end; // inclusive, unless it's NoPos
};

// NoPos is a valid unknown position; pos_isknown(NoPos) returns false.
static const Pos NoPos = 0;

void posmap_init(PosMap* pm);
void posmap_dispose(PosMap* pm);

// posmap_origin retrieves the origin for source, allocating one if needed.
// See pos_source for the inverse function.
u32 posmap_origin(PosMap* pm, Source* source);

// pos_source looks up the source for a pos. The inverse of posmap_origin.
// Returns NULL for unknown positions.
static Source* nullable pos_source(const PosMap* pm, Pos p);

static Pos pos_make(u32 origin, u32 line, u32 col, u32 width);
static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width); // no bounds checks!

static u32 pos_origin(Pos p); // 0 for pos without origin
static u32 pos_line(Pos p);
static u32 pos_col(Pos p);
static u32 pos_width(Pos p);

static Pos pos_with_origin(Pos p, u32 origin); // returns copy of p with specific origin
static Pos pos_with_line(Pos p, u32 line);   // returns copy of p with specific line
static Pos pos_with_col(Pos p, u32 col);    // returns copy of p with specific col
static Pos pos_with_width(Pos p, u32 width);  // returns copy of p with specific width

// pos_with_adjusted_start returns a copy of p with its start and width adjusted by deltacol.
// Can not overflow; the result is clamped.
Pos pos_with_adjusted_start(Pos p, i32 deltacol);

// pos_union returns a Pos that covers the column extent of both a and b.
// a and b must be on the same line.
Pos pos_union(Pos a, Pos b);

// pos_isknown reports whether the position is a known position.
static bool pos_isknown(Pos);

// pos_isbefore reports whether the position p comes before q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isbefore(Pos p, Pos q);

// pos_isafter reports whether the position p comes after q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isafter(Pos p, Pos q);

// pos_str appends "file:line:col" to dst. Returns false if memory allocation failed
bool pos_str(const PosMap*, Pos, Str* dst);

// pos_fmt appends "file:line:col: format ..." to dst, including source context.
// Returns false if memory allocation failed
bool pos_fmt(const PosMap*, PosSpan, Str* dst, const char* fmt, ...)
  ATTR_FORMAT(printf, 4, 5);
bool pos_fmtv(const PosMap*, PosSpan, Str* dst, const char* fmt, va_list);


//———————————————————————————————————————————————————————————————————————————————————————
// internal

// Layout constants: 20 bits origin, 20 bits line, 12 bits column, 12 bits width.
// Limits: sources: 1048575, lines: 1048575, columns: 4095, width: 4095
// If this is too tight, we can either make lico 64b wide, or we can introduce a tiered encoding
// where we remove column information as line numbers grow bigger; similar to what gcc does.
static const u64 _pos_widthBits  = 12;
static const u64 _pos_colBits    = 12;
static const u64 _pos_lineBits   = 20;
static const u64 _pos_originBits = 64 - _pos_lineBits - _pos_colBits - _pos_widthBits;

static const u64 _pos_originMax = (1llu << _pos_originBits) - 1;
static const u64 _pos_lineMax   = (1llu << _pos_lineBits) - 1;
static const u64 _pos_colMax    = (1llu << _pos_colBits) - 1;
static const u64 _pos_widthMax  = (1llu << _pos_widthBits) - 1;

static const u64 _pos_originShift = _pos_originBits + _pos_colBits + _pos_widthBits;
static const u64 _pos_lineShift   = _pos_colBits + _pos_widthBits;
static const u64 _pos_colShift    = _pos_widthBits;

inline static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width) {
  return (Pos)( ((u64)origin << _pos_originShift)
              | ((u64)line << _pos_lineShift)
              | ((u64)col << _pos_colShift)
              | width );
}
inline static Pos pos_make(u32 origin, u32 line, u32 col, u32 width) {
  return pos_make_unchecked(
    MIN(_pos_originMax, origin),
    MIN(_pos_lineMax, line),
    MIN(_pos_colMax, col),
    MIN(_pos_widthMax, width));
}
inline static u32 pos_origin(Pos p) { return p >> _pos_originShift; }
inline static u32 pos_line(Pos p)   { return (p >> _pos_lineShift) & _pos_lineMax; }
inline static u32 pos_col(Pos p)    { return (p >> _pos_colShift) & _pos_colMax; }
inline static u32 pos_width(Pos p)   { return p & _pos_widthMax; }

// TODO: improve the efficiency of these
inline static Pos pos_with_origin(Pos p, u32 origin) {
  return pos_make_unchecked(
    MIN(_pos_originMax, origin), pos_line(p), pos_col(p), pos_width(p));
}
inline static Pos pos_with_line(Pos p, u32 line) {
  return pos_make_unchecked(
    pos_origin(p), MIN(_pos_lineMax, line), pos_col(p), pos_width(p));
}
inline static Pos pos_with_col(Pos p, u32 col) {
  return pos_make_unchecked(
    pos_origin(p), pos_line(p), MIN(_pos_colMax, col), pos_width(p));
}
inline static Pos pos_with_width(Pos p, u32 width) {
  return pos_make_unchecked(
    pos_origin(p), pos_line(p), pos_col(p), MIN(_pos_widthMax, width));
}
inline static bool pos_isbefore(Pos p, Pos q) { return p < q; }
inline static bool pos_isafter(Pos p, Pos q) { return p > q; }
inline static bool pos_isknown(Pos p) {
  return pos_origin(p) != 0 || pos_line(p) != 0;
}
inline static Source* nullable pos_source(const PosMap* pm, Pos p) {
  return (Source*)pm->a.v[pos_origin(p)];
}

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PARSE_POS_IMPLEMENTATION

#include "tstyle.c"

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

#endif // PARSE_POS_IMPLEMENTATION
