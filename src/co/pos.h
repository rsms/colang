#pragma once
#include "util/array.h"

// Pos is a compact representation of a source position: source file, line and column.
// It can represent up to 16 777 215 sources, 16 777 215 lines and 65 535 columns.
// Inspired by the Go compiler's xpos & lico.
typedef u64 Pos;

// PosMap maps sources to Pos indices
typedef struct PosMap {
  Mem nullable mem; // used to allocate extra memory for a
  Array        a;
  void*        a_storage[32];
} PosMap;

// NoPos is a valid unknown position; pos_isknown(NoPos) returns false.
static const Pos NoPos = 0;

void posmap_init(PosMap* pm, Mem nullable mem);
void posmap_dispose(PosMap* pm);

// posmap_origin retrieves the origin for source, allocating one if needed.
// See pos_source for the inverse function.
u32 posmap_origin(PosMap* pm, void* source);

// pos_source looks up the source for a pos. The inverse of posmap_origin.
// Returns NULL for unknown positions.
static void* nullable pos_source(PosMap* pm, Pos p);

static Pos pos_make(u32 origin, u32 line, u32 col);
static Pos pos_make_unchecked(u32 origin, u32 line, u32 col); // careful; no bounds checks!
static u32 pos_origin(Pos p);
static u32 pos_line(Pos p);
static u32 pos_col(Pos p);

// pos_isknown reports whether the position is a known position.
static bool pos_isknown(Pos);

// pos_isbefore reports whether the position p comes before q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isbefore(Pos p, Pos q);

// pos_isafter reports whether the position p comes after q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isafter(Pos p, Pos q);


// -----------------------------------------------------------------------------------------------
// implementations

// Layout constants: 24 bits for origin, 24 bits for line, 16 bits for column.
// Limits: max source origins: 16 777 215, max lines: 16 777 215, max columns: 65 535.
// If this is too tight, we can either make lico 64b wide, or we can introduce a tiered
// encoding where we remove column information as line numbers grow bigger; similar to what
// gcc does.
// layout: 24 bits origin, 24 bits line, 16 bits col
static const u64 _pos_colBits    = 16;
static const u64 _pos_lineBits   = 24;
static const u64 _pos_originBits = 64 - _pos_lineBits - _pos_colBits;

static const u64 _pos_originMax  = (1 << _pos_colBits) - 1;
static const u64 _pos_lineMax    = (1 << _pos_lineBits) - 1;
static const u64 _pos_colMax     = (1llu << _pos_originBits) - 1;

static const u64 _pos_originShift = _pos_originBits + _pos_colBits;
static const u64 _pos_lineShift   = _pos_colBits;

ALWAYS_INLINE static Pos pos_make_unchecked(u32 origin, u32 line, u32 col) {
  return (Pos)( ((u64)origin << _pos_originShift) | ((u64)line << _pos_lineShift) | col );
}

inline static Pos pos_make(u32 origin, u32 line, u32 col) {
  return pos_make_unchecked(
    MIN(_pos_originMax, origin),
    MIN(_pos_lineMax, line),
    MIN(_pos_colMax, col));
}

ALWAYS_INLINE static u32 pos_origin(Pos p) { return p >> _pos_originShift; }
ALWAYS_INLINE static u32 pos_line(Pos p)   { return (p >> _pos_lineShift) & _pos_colMax; }
ALWAYS_INLINE static u32 pos_col(Pos p)    { return p & _pos_originMax; }

ALWAYS_INLINE static bool pos_isbefore(Pos p, Pos q) { return p < q; }
ALWAYS_INLINE static bool pos_isafter(Pos p, Pos q) { return p > q; }

inline static bool pos_isknown(Pos p) {
  return pos_origin(p) != 0 || pos_line(p) != 0;
}

ALWAYS_INLINE static void* pos_source(PosMap* pm, Pos p) {
  return pm->a.v[pos_origin(p)];
}
