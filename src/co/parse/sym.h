#pragma once

// Sym is a type of Str, compatible with Str functions, with an additional header
// containing a precomputed FNV1a hash. Sym is immutable.
typedef const char* Sym;

// Sym interning
// typedef struct SymPool { void* p; } SymPool; // zero-initialized pool of symbols
// Sym symget(SymPool* p, const u8* data, size_t len, u32 hash); // intern a symbol
// Sym symgeth(SymPool* p, const u8* data, size_t len); // hashes data, then calls symget
// static Sym symgetcstr(SymPool* p, const char* cstr); // hashes data, then calls symget
Sym symget(const u8* data, size_t len, u32 hash); // intern a symbol
Sym symgeth(const u8* data, size_t len); // hashes data, then calls symget
static Sym symgetcstr(const char* cstr); // hashes data, then calls symget

static int symcmp(Sym a, Sym b); // Compare two Sym's string values
static u32 symhash(Sym s); // the symbol's precomputed hash
static u32 symlen(Sym s);  // faster alternative to strlen (equivalent to str_len)

// ---------------------------------------------------------------------------
// implementation

typedef struct __attribute__((__packed__)) SymHeader {
  u32              hash;
  struct StrHeader sh;
} SymHeader;

#define _SYM_HEADER(s) ((const SymHeader*)((s) - (sizeof(SymHeader))))

inline static Sym symgetcstr(const char* cstr) {
  return symgeth((const u8*)cstr, strlen(cstr));
}
// inline static Sym symgetcstr(SymPool* p, const char* cstr) {
//   return symgeth(p, (const u8*)cstr, strlen(cstr));
// }

inline static int symcmp(Sym a, Sym b) { return a == b ? 0 : strcmp(a, b); }
inline static u32 symhash(Sym s) { return _SYM_HEADER(s)->hash; }
inline static u32 symlen(Sym s) { return _SYM_HEADER(s)->sh.len; }
