#pragma once
// Syntax-specific symbolic definitions
//
// Defines:
//
//   const Sym sym_{TOKEN_KEYWORDS...}
//   const Sym sym_{TYPE_SYMS...}
//   const Sym sym_{PREDEFINED_CONSTANTS...}
//   const Sym sym_{PREDEFINED_IDENTS...}
//
//   Node* Type_{TYPE_SYMS...}
//   Node* Type_nil
//   Node* Type_ideal
//
//   Node* Const_{PREDEFINED_CONSTANTS...}
//   Node* Const_nil
//
ASSUME_NONNULL_BEGIN

// universe_syms() returns a read-only SymPool which holds all predefined symbols of the language
const SymPool* universe_syms();

// TypeCodeToTypeNode returns the type Node for TypeCode t.
static Node* TypeCodeToTypeNode(TypeCode t);

// sym_langtok returns the Tok representing this sym in the language syntax.
// Either returns a keyword token or TId if s is not a keyword.
static Tok sym_langtok(Sym s);

// symbols for language keywords (defined in token.h)
#define SYM_DEF(str, _) \
  extern const Sym sym_##str;
TOKEN_KEYWORDS(SYM_DEF)
#undef SYM_DEF


// symbols and AST nodes for predefined types (defined in types.h)
typedef struct Node Node;
#define SYM_DEF(name) \
  extern const Sym sym_##name; \
  extern Node* Type_##name;
TYPE_SYMS(SYM_DEF)
TYPE_SYMS_PRIVATE(SYM_DEF)
#undef SYM_DEF

// nil is special and implemented without macros since its sym is defined by TOKEN_KEYWORDS
extern Node* Type_nil;

// "ideal" is the type of untyped constants like "x = 4"
extern Node* Type_ideal;

// IMPL TypeCodeToTypeNode
extern Node* const _TypeCodeToTypeNodeMap[TypeCode_CONCRETE_END];
inline static Node* TypeCodeToTypeNode(TypeCode t) {
 assert(t >= 0 && t < TypeCode_CONCRETE_END);
 return _TypeCodeToTypeNodeMap[t];
}

// symbols and AST nodes for predefined constants
#define PREDEFINED_CONSTANTS(_) \
  /* name   CType_  value */    \
  _( true,  bool,   1 )         \
  _( false, bool,   0 )         \
  _( nil,   nil,    0 )         \
/*END PREDEFINED_CONSTANTS*/
#define SYM_DEF(name, _type, _val) \
  extern const Sym sym_##name; \
  extern Node* Const_##name;
PREDEFINED_CONSTANTS(SYM_DEF)
#undef SYM_DEF


// symbols for predefined common identifiers
// predefined common identifiers (excluding types)
#define PREDEFINED_IDENTS(ID) \
  ID( _ ) \
/*END PREDEFINED_IDENTS*/
#define SYM_DEF(name) \
  extern const Sym sym_##name;
PREDEFINED_IDENTS(SYM_DEF)
#undef SYM_DEF

// -----------------------------------------------------------------------------------------------
// implementation

inline static Tok sym_langtok(Sym s) {
  // Bits [4-8) represents offset into Tok enum when s is a language keyword.
  u8 kwindex = symflags(s);
  return kwindex == 0 ? TId : TKeywordsStart + kwindex;
}

ASSUME_NONNULL_END
