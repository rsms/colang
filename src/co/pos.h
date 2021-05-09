#pragma once
#include "util/array.h"

// Pos is a compact representation of a source position: source file, line and column.
// Limits: 1048575 number of sources, 1048575 max line, 4095 max column, 4095 max width.
// Inspired by the Go compiler's xpos & lico.
typedef u64 Pos;

// PosMap maps sources to Pos indices
typedef struct PosMap {
  Mem nullable mem; // used to allocate extra memory for a
  Array        a;
  void*        a_storage[32]; // slot 0 is always NULL
} PosMap;

// PosSpan describes a span in a source
typedef struct PosSpan {
  Pos start;
  Pos end; // inclusive, unless it's NoPos
} PosSpan;

// NoPos is a valid unknown position; pos_isknown(NoPos) returns false.
static const Pos NoPos = 0;

void posmap_init(PosMap* pm, Mem nullable mem);
void posmap_dispose(PosMap* pm);

// posmap_origin retrieves the origin for source, allocating one if needed.
// See pos_source for the inverse function.
u32 posmap_origin(PosMap* pm, void* source);

// pos_source looks up the source for a pos. The inverse of posmap_origin.
// Returns NULL for unknown positions.
static void* nullable pos_source(const PosMap* pm, Pos p);

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

// pos_isknown reports whether the position is a known position.
static bool pos_isknown(Pos);

// pos_isbefore reports whether the position p comes before q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isbefore(Pos p, Pos q);

// pos_isafter reports whether the position p comes after q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isafter(Pos p, Pos q);

// pos_str appends "file:line:col" to s
Str pos_str(const PosMap*, Pos, Str s);

// pos_fmt appends "file:line:col: format ..." to s, including source context
Str pos_fmt(const PosMap*, PosSpan, Str s, const char* fmt, ...) ATTR_FORMAT(printf, 4, 5);
Str pos_fmtv(const PosMap*, PosSpan, Str s, const char* fmt, va_list);


// -----------------------------------------------------------------------------------------------
// implementations

// Layout constants: 20 bits origin, 20 bits line, 12 bits column, 12 bits width.
// Limits: sources: 1048575, lines: 1048575, columns: 4095, width: 4095
// If this is too tight, we can either make lico 64b wide, or we can introduce a tiered encoding
// where we remove column information as line numbers grow bigger; similar to what gcc does.
static const u64 _pos_widthBits   = 12;
static const u64 _pos_colBits    = 12;
static const u64 _pos_lineBits   = 20;
static const u64 _pos_originBits = 64 - _pos_lineBits - _pos_colBits - _pos_widthBits;

static const u64 _pos_originMax  = (1llu << _pos_originBits) - 1;
static const u64 _pos_lineMax    = (1llu << _pos_lineBits) - 1;
static const u64 _pos_colMax     = (1llu << _pos_colBits) - 1;
static const u64 _pos_widthMax    = (1llu << _pos_widthBits) - 1;

static const u64 _pos_originShift = _pos_originBits + _pos_colBits + _pos_widthBits;
static const u64 _pos_lineShift   = _pos_colBits + _pos_widthBits;
static const u64 _pos_colShift    = _pos_widthBits;

ALWAYS_INLINE static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width) {
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

ALWAYS_INLINE static u32 pos_origin(Pos p) { return p >> _pos_originShift; }
ALWAYS_INLINE static u32 pos_line(Pos p)   { return (p >> _pos_lineShift) & _pos_lineMax; }
ALWAYS_INLINE static u32 pos_col(Pos p)    { return (p >> _pos_colShift) & _pos_colMax; }
ALWAYS_INLINE static u32 pos_width(Pos p)   { return p & _pos_widthMax; }

// TODO: improve the efficiency of these
ALWAYS_INLINE static Pos pos_with_origin(Pos p, u32 origin) {
  return pos_make_unchecked(MIN(_pos_originMax, origin), pos_line(p), pos_col(p), pos_width(p));
}
ALWAYS_INLINE static Pos pos_with_line(Pos p, u32 line) {
  return pos_make_unchecked(pos_origin(p), MIN(_pos_lineMax, line), pos_col(p), pos_width(p));
}
ALWAYS_INLINE static Pos pos_with_col(Pos p, u32 col) {
  return pos_make_unchecked(pos_origin(p), pos_line(p), MIN(_pos_colMax, col), pos_width(p));
}
ALWAYS_INLINE static Pos pos_with_width(Pos p, u32 width) {
  return pos_make_unchecked(pos_origin(p), pos_line(p), pos_col(p), MIN(_pos_widthMax, width));
}

ALWAYS_INLINE static bool pos_isbefore(Pos p, Pos q) { return p < q; }
ALWAYS_INLINE static bool pos_isafter(Pos p, Pos q) { return p > q; }

inline static bool pos_isknown(Pos p) {
  return pos_origin(p) != 0 || pos_line(p) != 0;
}

ALWAYS_INLINE static void* pos_source(const PosMap* pm, Pos p) {
  return pm->a.v[pos_origin(p)];
}
