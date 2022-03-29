// Pos is a compact representation of a source position: source file, line and column.
// Limits: 1048575 source files, 1048575 lines, 4095 columns, 4095 span width.
// Inspired by the Go compiler's xpos & lico.
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
#define NoPos ((Pos)0)

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


// returns false if memory allocation failed
static bool pos_add_context(const PosMap* pm, PosSpan span, Str* s, Source* src) {
  Pos start = span.start;
  Pos end = span.end;
  str_appendc(s, '\n');

  const u8* lineptr;
  u32 linelen;
  if (source_line_bytes(src, pos_line(start), &lineptr, &linelen) != 0)
    return false;

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

#endif // PARSE_POS_IMPLEMENTATION
