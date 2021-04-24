#pragma once

// Sym is an immutable type of Str with a precomputed hash.
// Being a Str it is also a valid C-string (i.e. null-terminated.)
// Sym is compatible with all Str functions.
// Sym functions are tuned toward lookup rather than insertion or deletion.
typedef const char* Sym;

// SYM_FLAGS_MAX defines the largest possible flags value
#define SYM_FLAGS_MAX 15

// SYM_LEN_MAX defines the largest possible length of a symbol
#define SYM_LEN_MAX 0xfffffff /* 268 435 455 (28 bits) */

// SymRBNode is a red-black tree node
typedef struct SymRBNode {
  Sym               key;
  bool              isred;
  struct SymRBNode* left;
  struct SymRBNode* right;
} SymRBNode;

// SymPool holds a set of syms unique to the pool
typedef struct SymPool SymPool;
typedef struct SymPool {
  SymRBNode*        root;
  SymPool* nullable base;
  Mem nullable      mem;
  rwmtx_t           mu;
} SymPool;

// sympool_init initialized a SymPool
// base is an optional "parent" or "outer" read-only symbol pool to use for secondary lookups
//   when a symbol is not found in the pool.
// mem is the memory to use for SymRBNodes.
// root may be a preallocated red-black tree. Be mindful of interactions with sympool_dispose.
void sympool_init(SymPool* p, SymPool* nullable base, Mem nullable mem, SymRBNode* nullable root);

// sympool_dispose frees up memory used by p (but does not free p itself)
// When a SymPool has been disposed, all symbols in it becomes invalid.
void sympool_dispose(SymPool* p);

// sympool_repr appends a printable list representation of the symbols in p
Str sympool_repr(const SymPool* p, Str s);

// symget "interns" a Sym in p.
// All symget functions are thread safe.
Sym symget(SymPool* p, const char* data, size_t len);

// symgeth is like symget but allows you to provide a precomputed hash.
// The hash must be an xxHash32 using sym_xxhash32_seed.
Sym symgeth(SymPool* p, const char* data, size_t len, u32 hash);

// symgetcstr is a convenience around symget for C-strings (calls strlen for you.)
static Sym symgetcstr(SymPool* p, const char* cstr);

// symadd adds a symbol to p unless it already exists in p in which case the existing symbol
// is returned. The difference between symget and symadd is what happens when a base pool is used:
// In the case of symget the entire base pool chain is traversed looking for the symbol and only
// if that fails is a new symbol added to p.
// However with symadd p's base is not searched and a new symbol is added to p regardless if it
// exists in base pools. Additionally, the implementation of this function assumes that the
// common case is that there's no symbol for data.
Sym symadd(SymPool* p, const char* data, size_t len);

// symaddh is like symadd but allows you to provide a precomputed hash.
// The hash must be an xxHash32 using sym_xxhash32_seed.
Sym symaddh(SymPool* p, const char* data, size_t len, u32 hash);

// symaddcstr is a convenience around symadd for C-strings (calls strlen for you.)
static Sym symaddcstr(SymPool* p, const char* cstr);

// symcmp compares two Sym's string values, like memcmp
// Note that to check equiality of syms, simply compare their pointer values (e.g. a==b)
static int symcmp(Sym a, Sym b);

// symhash returns the symbol's precomputed hash
static u32 symhash(Sym s);

// symlen returns a symbols precomputed string length
static u32 symlen(Sym s);

// symflags returns a symbols flags
static u8 symflags(Sym s);

// sym_xxhash32_seed is the xxHash seed used for hashing sym data
static const u32 sym_xxhash32_seed = 578;

// ---------------------------------------------------------------------------
// implementation

typedef struct __attribute__((__packed__)) SymHeader {
  u32              hash;
  struct StrHeader sh;
} SymHeader;

#define _SYM_HEADER(s) ((const SymHeader*)((s) - (sizeof(SymHeader))))

// these work for little endian only. Sym implementation relies on LE to be able to simply
// increment the length value.
#if defined(__ARMEB__) || defined(__ppc__) || defined(__powerpc__)
#error "big-endian arch not supported"
#endif
#define _SYM_FLAG_BITS 4
#define _SYM_FLAG_MASK ((1 << _SYM_FLAG_BITS) - 1)    /* e.g. 0b11110000...0000 */
#define _SYM_LEN_MASK  (0xffffffff >> _SYM_FLAG_BITS) /* e.g. 0b00001111...1111 */

inline static Sym symgetcstr(SymPool* p, const char* cstr) {
  return symget(p, cstr, strlen(cstr));
}

inline static Sym symaddcstr(SymPool* p, const char* cstr) {
  return symadd(p, cstr, strlen(cstr));
}

inline static int symcmp(Sym a, Sym b) { return a == b ? 0 : strcmp(a, b); }
inline static u32 symhash(Sym s) { return _SYM_HEADER(s)->hash; }
inline static u32 symlen(Sym s) { return str_len((Str)s) & _SYM_LEN_MASK; }

inline static u8 symflags(Sym s) {
  return (str_len((Str)s) & _SYM_FLAG_MASK) >> (32 - _SYM_FLAG_BITS);
}

// SYM_MAKELEN(u32 len, u8 flags) is a helper macro for making the "length" portion of
// the StrHeader, useful when creating Syms at compile time.
#define SYM_MAKELEN(len, flags) \
  ( ( ((u32)(flags) << (32 - _SYM_FLAG_BITS)) & _SYM_FLAG_MASK ) | ((len) & _SYM_LEN_MASK) )

// sym_dangerously_set_flags mutates a Sym by setting its flags.
// Use with caution as Syms are assumed to be constant and immutable.
inline static Sym sym_dangerously_set_flags(Sym s, u8 flags) {
  u32 u = str_len((Str)s);
  u = (u32)((flags << (32 - _SYM_FLAG_BITS)) & _SYM_FLAG_MASK) | (u & _SYM_LEN_MASK);
  return (Sym)str_setlen((Str)s, u);
}

// sym_dangerously_set_len mutates a Sym by setting its length.
// Use with caution as Syms are assumed to be constant and immutable.
inline static Sym sym_dangerously_set_len(Sym s, u32 len) {
  assert(len <= SYM_LEN_MAX);
  u32 u = str_len((Str)s);
  u = (u & _SYM_FLAG_MASK) | len;
  return (Sym)str_setlen((Str)s, u);
}
