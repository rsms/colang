// generated by gen_constants at the end of this file, included by parse_universe.c
//
// TODO: Move this generator to a script.
//       Currently it is really hard to use when e.g. changing types (TypeCode_*).
//
// Define to run the generator during program initialization:
#define RUN_GENERATOR 0

// static const Node _kType_type = {.kind=NTypeType};
// Node* kType_type = (Node*)&_kType_type;
static const Node _kNode_bad = {.kind=NBad};
Node* kNode_bad = (Node*)&_kNode_bad;

//-- BEGIN gen_constants()

const Sym kSym_as = &"\x87\x3D\x7F\xCD\x02\x00\x00\x08""as\0"[8];
const Sym kSym_auto = &"\xAD\xF8\x95\xF5\x04\x00\x00\x10""auto\0"[8];
const Sym kSym_break = &"\xFF\x55\x83\xD2\x05\x00\x00\x18""break\0"[8];
const Sym kSym_continue = &"\xB4\xFF\xAB\xF1\x08\x00\x00\x20""continue\0"[8];
const Sym kSym_defer = &"\xAD\x43\x24\x74\x05\x00\x00\x28""defer\0"[8];
const Sym kSym_else = &"\xAD\x2A\x2A\x55\x04\x00\x00\x30""else\0"[8];
const Sym kSym_enum = &"\x00\xE0\xE4\xCD\x04\x00\x00\x38""enum\0"[8];
const Sym kSym_for = &"\x2E\x77\xA4\x49\x03\x00\x00\x40""for\0"[8];
const Sym kSym_fun = &"\x1B\xD7\x8A\x8A\x03\x00\x00\x48""fun\0"[8];
const Sym kSym_if = &"\xF5\x8D\x92\xCF\x02\x00\x00\x50""if\0"[8];
const Sym kSym_import = &"\xF8\xA4\x4A\xF8\x06\x00\x00\x58""import\0"[8];
const Sym kSym_in = &"\x74\x9A\xDF\xDD\x02\x00\x00\x60""in\0"[8];
const Sym kSym_nil = &"\x8F\x7E\x4F\xEF\x03\x00\x00\x68""nil\0"[8];
const Sym kSym_return = &"\xEB\xA6\x08\xA3\x06\x00\x00\x70""return\0"[8];
const Sym kSym_struct = &"\x97\xFC\x80\x50\x06\x00\x00\x78""struct\0"[8];
const Sym kSym_switch = &"\x37\xE0\x68\x4B\x06\x00\x00\x80""switch\0"[8];
const Sym kSym_type = &"\x52\x1E\xB2\xD6\x04\x00\x00\x88""type\0"[8];
const Sym kSym_const = &"\x0A\x54\xDC\xAD\x05\x00\x00\x90""const\0"[8];
const Sym kSym_mut = &"\x7D\x83\xBC\x41\x03\x00\x00\x98""mut\0"[8];
const Sym kSym_var = &"\x27\xBE\x0A\xFD\x03\x00\x00\xA0""var\0"[8];
const Sym kSym_bool = &"\x70\x6D\x7D\x3D\x04\x00\x00\x00""bool\0"[8];
const Sym kSym_i8 = &"\x9D\xE2\x63\xDB\x02\x00\x00\x00""i8\0"[8];
const Sym kSym_u8 = &"\xEF\xCA\xBF\x76\x02\x00\x00\x00""u8\0"[8];
const Sym kSym_i16 = &"\xC2\xF3\x4A\x52\x03\x00\x00\x00""i16\0"[8];
const Sym kSym_u16 = &"\xF8\x6B\xDE\x1D\x03\x00\x00\x00""u16\0"[8];
const Sym kSym_i32 = &"\x4A\x53\xEC\xE0\x03\x00\x00\x00""i32\0"[8];
const Sym kSym_u32 = &"\x61\x13\x38\xD1\x03\x00\x00\x00""u32\0"[8];
const Sym kSym_i64 = &"\xA1\x93\xF3\x6C\x03\x00\x00\x00""i64\0"[8];
const Sym kSym_u64 = &"\xBA\x1C\x93\x2D\x03\x00\x00\x00""u64\0"[8];
const Sym kSym_f32 = &"\x9E\x9C\x5E\xFD\x03\x00\x00\x00""f32\0"[8];
const Sym kSym_f64 = &"\x90\x7B\xEB\x9C\x03\x00\x00\x00""f64\0"[8];
const Sym kSym_int = &"\xCD\x9E\x65\xA5\x03\x00\x00\x00""int\0"[8];
const Sym kSym_uint = &"\xFA\x0A\xDC\xFB\x04\x00\x00\x00""uint\0"[8];
const Sym kSym_ideal = &"\x5B\xB4\x1C\xC8\x05\x00\x00\x00""ideal\0"[8];
const Sym kSym_str = &"\x12\xAE\x4D\xED\x03\x00\x00\x00""str\0"[8];
const Sym kSym_true = &"\xA2\x11\x64\xDB\x04\x00\x00\x00""true\0"[8];
const Sym kSym_false = &"\xAC\x1C\xED\xD5\x05\x00\x00\x00""false\0"[8];
const Sym kSym__ = &"\xE7\x52\x89\xD0\x01\x00\x00\x00""_\0"[8];
const Sym kSym_b = &"\xBA\x97\x7C\xF4\x01\x00\x00\x00""b\0"[8];
const Sym kSym_1 = &"\xC4\xD0\xFC\x8C\x01\x00\x00\x00""1\0"[8];
const Sym kSym_2 = &"\xBA\xB3\xD5\x74\x01\x00\x00\x00""2\0"[8];
const Sym kSym_3 = &"\x3B\x29\x53\x12\x01\x00\x00\x00""3\0"[8];
const Sym kSym_4 = &"\x22\xEF\xF2\x1D\x01\x00\x00\x00""4\0"[8];
const Sym kSym_5 = &"\x5C\xCE\x89\xEE\x01\x00\x00\x00""5\0"[8];
const Sym kSym_6 = &"\xBE\x2F\xCC\x8D\x01\x00\x00\x00""6\0"[8];
const Sym kSym_7 = &"\x3F\xD4\xD9\xA7\x01\x00\x00\x00""7\0"[8];
const Sym kSym_8 = &"\x37\x74\x69\x1E\x01\x00\x00\x00""8\0"[8];
const Sym kSym_f = &"\xE9\x46\xFD\x9C\x01\x00\x00\x00""f\0"[8];
const Sym kSym_F = &"\x5C\x05\x48\xBA\x01\x00\x00\x00""F\0"[8];
const Sym kSym_i = &"\x3A\x16\x35\x80\x01\x00\x00\x00""i\0"[8];
const Sym kSym_u = &"\x32\x6A\x3E\x41\x01\x00\x00\x00""u\0"[8];
const Sym kSym_0 = &"\xFE\xED\xFA\xA1\x01\x00\x00\x00""0\0"[8];
const Sym kSym_$2A = &"\x9C\x1C\x9F\x3C\x01\x00\x00\x00""*\0"[8];
const Sym kSym_s = &"\xC0\xB7\x96\x15\x01\x00\x00\x00""s\0"[8];
const Sym kSym_a = &"\x60\xE5\x1B\x67\x01\x00\x00\x00""a\0"[8];

static SymRBNode n_3 = { kSym_3, true, NULL, NULL };
static SymRBNode n_s = { kSym_s, false, &n_3, NULL };
static SymRBNode n_4 = { kSym_4, false, NULL, NULL };
static SymRBNode n_u16 = { kSym_u16, true, &n_s, &n_4, };
static SymRBNode n_u64 = { kSym_u64, true, NULL, NULL };
static SymRBNode n_$2A = { kSym_$2A, false, &n_u64, NULL };
static SymRBNode n_8 = { kSym_8, false, &n_u16, &n_$2A, };
static SymRBNode n_u = { kSym_u, true, NULL, NULL };
static SymRBNode n_mut = { kSym_mut, false, &n_u, NULL };
static SymRBNode n_switch = { kSym_switch, false, NULL, NULL };
static SymRBNode n_for = { kSym_for, false, &n_mut, &n_switch, };
static SymRBNode n_bool = { kSym_bool, false, &n_8, &n_for, };
static SymRBNode n_i16 = { kSym_i16, false, NULL, NULL };
static SymRBNode n_a = { kSym_a, true, NULL, NULL };
static SymRBNode n_i64 = { kSym_i64, false, &n_a, NULL };
static SymRBNode n_else = { kSym_else, true, &n_i16, &n_i64, };
static SymRBNode n_2 = { kSym_2, true, NULL, NULL };
static SymRBNode n_i = { kSym_i, true, NULL, NULL };
static SymRBNode n_u8 = { kSym_u8, false, &n_2, &n_i, };
static SymRBNode n_defer = { kSym_defer, false, &n_else, &n_u8, };
static SymRBNode n_1 = { kSym_1, false, NULL, NULL };
static SymRBNode n_f64 = { kSym_f64, true, NULL, NULL };
static SymRBNode n_0 = { kSym_0, true, NULL, NULL };
static SymRBNode n_f = { kSym_f, false, &n_f64, &n_0, };
static SymRBNode n_6 = { kSym_6, false, &n_1, &n_f, };
static SymRBNode n_fun = { kSym_fun, true, &n_defer, &n_6, };
static SymRBNode n_int = { kSym_int, true, NULL, NULL };
static SymRBNode n_7 = { kSym_7, false, &n_int, NULL };
static SymRBNode n_F = { kSym_F, true, NULL, NULL };
static SymRBNode n_ideal = { kSym_ideal, false, &n_F, NULL };
static SymRBNode n_const = { kSym_const, false, &n_7, &n_ideal, };
static SymRBNode n_return = { kSym_return, false, &n_fun, &n_const, };
static SymRBNode n_struct = { kSym_struct, true, &n_bool, &n_return, };
static SymRBNode n_enum = { kSym_enum, false, NULL, NULL };
static SymRBNode n__ = { kSym__, true, NULL, NULL };
static SymRBNode n_u32 = { kSym_u32, false, &n__, NULL };
static SymRBNode n_if = { kSym_if, true, &n_enum, &n_u32, };
static SymRBNode n_false = { kSym_false, true, NULL, NULL };
static SymRBNode n_type = { kSym_type, false, &n_false, NULL };
static SymRBNode n_break = { kSym_break, false, &n_if, &n_type, };
static SymRBNode n_true = { kSym_true, true, NULL, NULL };
static SymRBNode n_in = { kSym_in, false, &n_true, NULL };
static SymRBNode n_str = { kSym_str, true, NULL, NULL };
static SymRBNode n_5 = { kSym_5, false, &n_str, NULL };
static SymRBNode n_i32 = { kSym_i32, false, &n_in, &n_5, };
static SymRBNode n_i8 = { kSym_i8, true, &n_break, &n_i32, };
static SymRBNode n_continue = { kSym_continue, true, NULL, NULL };
static SymRBNode n_b = { kSym_b, false, &n_continue, NULL };
static SymRBNode n_import = { kSym_import, true, NULL, NULL };
static SymRBNode n_uint = { kSym_uint, false, &n_import, NULL };
static SymRBNode n_auto = { kSym_auto, true, &n_b, &n_uint, };
static SymRBNode n_f32 = { kSym_f32, false, NULL, NULL };
static SymRBNode n_var = { kSym_var, false, &n_auto, &n_f32, };
static SymRBNode n_nil = { kSym_nil, false, &n_i8, &n_var, };
static SymRBNode n_as = { kSym_as, false, &n_struct, &n_nil, };

static SymRBNode* _symroot = &n_as;

#define _(NAME, TID, TFLAGS) \
  {.kind=NBasicType, .tflags=TFLAGS, .tid=TID, TC_##NAME, kSym_##NAME}
static const BasicTypeNode _kType_bool = _(bool, kSym_b, TF_KindBool);
static const BasicTypeNode _kType_i8 = _(i8, kSym_1, TF_KindInt | TF_Size1 | TF_Signed);
static const BasicTypeNode _kType_u8 = _(u8, kSym_2, TF_KindInt | TF_Size1);
static const BasicTypeNode _kType_i16 = _(i16, kSym_3, TF_KindInt | TF_Size2 | TF_Signed);
static const BasicTypeNode _kType_u16 = _(u16, kSym_4, TF_KindInt | TF_Size2);
static const BasicTypeNode _kType_i32 = _(i32, kSym_5, TF_KindInt | TF_Size4 | TF_Signed);
static const BasicTypeNode _kType_u32 = _(u32, kSym_6, TF_KindInt | TF_Size4);
static const BasicTypeNode _kType_i64 = _(i64, kSym_7, TF_KindInt | TF_Size8 | TF_Signed);
static const BasicTypeNode _kType_u64 = _(u64, kSym_8, TF_KindInt | TF_Size8);
static const BasicTypeNode _kType_f32 = _(f32, kSym_f, TF_KindF32 | TF_Size4 | TF_Signed);
static const BasicTypeNode _kType_f64 = _(f64, kSym_F, TF_KindF64 | TF_Size8 | TF_Signed);
static const BasicTypeNode _kType_int = _(int, kSym_i, TF_KindInt | TF_Signed);
static const BasicTypeNode _kType_uint = _(uint, kSym_u, TF_KindInt);
static const BasicTypeNode _kType_nil = _(nil, kSym_0, TF_KindVoid);
static const BasicTypeNode _kType_ideal = _(ideal, kSym_$2A, TF_KindVoid);
static const BasicTypeNode _kType_str = _(str, kSym_s, TF_KindPointer);
static const BasicTypeNode _kType_auto = _(auto, kSym_a, TF_KindVoid);
#undef _
Type* kType_bool = (Type*)&_kType_bool;
Type* kType_i8 = (Type*)&_kType_i8;
Type* kType_u8 = (Type*)&_kType_u8;
Type* kType_i16 = (Type*)&_kType_i16;
Type* kType_u16 = (Type*)&_kType_u16;
Type* kType_i32 = (Type*)&_kType_i32;
Type* kType_u32 = (Type*)&_kType_u32;
Type* kType_i64 = (Type*)&_kType_i64;
Type* kType_u64 = (Type*)&_kType_u64;
Type* kType_f32 = (Type*)&_kType_f32;
Type* kType_f64 = (Type*)&_kType_f64;
Type* kType_int = (Type*)&_kType_int;
Type* kType_uint = (Type*)&_kType_uint;
Type* kType_nil = (Type*)&_kType_nil;
Type* kType_ideal = (Type*)&_kType_ideal;
Type* kType_str = (Type*)&_kType_str;
Type* kType_auto = (Type*)&_kType_auto;

static const NilNode _kExpr_nil =
 {.kind=NNil,.flags=NF_Const|NF_RValue,.type=(Type*)&_kType_nil};
static const BoolLitNode _kExpr_true =
 {.kind=NBoolLit,.flags=NF_Const|NF_RValue,.type=(Type*)&_kType_bool,.ival=1};
static const BoolLitNode _kExpr_false =
 {.kind=NBoolLit,.flags=NF_Const|NF_RValue,.type=(Type*)&_kType_bool,.ival=0};
Expr* kExpr_nil = (Expr*)&_kExpr_nil;
Expr* kExpr_true = (Expr*)&_kExpr_true;
Expr* kExpr_false = (Expr*)&_kExpr_false;

#ifndef NDEBUG
__attribute__((used)) static const char* const debugSymCheck =
  "kw:as=TAs kw:auto=TAuto kw:break=TBreak kw:continue=TContinue kw:defer=TDefer kw:else=TElse kw:enum=TEnum kw:for=TFor kw:fun=TFun kw:if=TIf kw:import=TImport kw:in=TIn kw:nil=TNil kw:return=TReturn kw:struct=TStruct kw:switch=TSwitch kw:type=TType kw:const=TConst kw:mut=TMut kw:var=TVar tc:bool tc:i8 tc:u8 tc:i16 tc:u16 tc:i32 tc:u32 tc:i64 tc:u64 tc:f32 tc:f64 tc:int tc:uint tc:nil tc:ideal tc:str tc:auto sym:_ const:nil,Nil,nil= const:true,BoolLit,bool=.ival=1 const:false,BoolLit,bool=.ival=0";
#endif

//-- END gen_constants()

// ---------------------------------------------------------------------------------------
// sym constant data generator

#if RUN_GENERATOR || !defined(NDEBUG)
  static Str gen_checksum(Str s) {

    #define _(tok, str) s = str_appendfmt(s, "kw:%s=%s ", #str, #tok);
    DEF_TOKENS_KEYWORD(_)
    #undef _

    #define _(name, ...) s = str_append(s, "tc:" #name " ");
    DEF_TYPE_CODES_BASIC_PUB(_)
    DEF_TYPE_CODES_BASIC(_)
    DEF_TYPE_CODES_PUB(_)
    #undef _

    #define _(name, ...) s = str_append(s, "sym:" #name " ");
    DEF_SYMS_PUB(_)
    #undef _

    #define _(name, nkind, typecode_suffix, structinit) \
      s = str_appendfmt(s, "const:%s,%s,%s=%s ", #name, #nkind, #typecode_suffix, structinit);
    DEF_CONST_NODES_PUB(_)
    #undef _

    if (s->len > 0) {
      // trim trailing space
      s->len--;
      s->p[s->len] = 0;
    }
    return s;
  }
#endif


#if RUN_GENERATOR

#ifndef CO_WITH_LIBC
  #error Generator depends on libc
#endif
#include <stdio.h>

// red-black tree implementation used for interning
// RBKEY must match that in sym.c
#define RBKEY      Sym
#define RBUSERDATA Mem _Nonnull
#include "../rbtree.h"


static RBNode* RBAllocNode(Mem mem) {
  return (RBNode*)memalloct(mem, RBNode);
}

static void RBFreeNode(RBNode* node, Mem mem) {
}

static int RBCmp(Sym a, Sym b, Mem mem) {
  if (symhash(a) < symhash(b))
    return -1;
  if (symhash(a) > symhash(b))
    return 1;
  int cmp = (int)symlen(a) - (int)symlen(b);
  if (cmp == 0) {
    // hash is identical and length is identical; compare bytes
    cmp = memcmp(a, b, symlen(a));
  }
  return cmp;
}


// is_cident_nth returns true if c is a valid character in a C identifier as the
// 2nd or later character.
static inline bool is_cident_nth(char c) {
  return (
    ('0' <= c && c <= '9') ||
    ('A' <= c && c <= 'Z') ||
    ('a' <= c && c <= 'z') ||
    c == '_'
    // note: excluding '$' since we use that to encode other chars
  );
}

static const char* cidentc(char c) {
  static char buf[16];
  buf[0] = c;
  buf[1] = 0;
  if (!is_cident_nth(c))
    snprintf(buf, sizeof(buf), "$%02X", c);
  return buf;
}

static Str str_append_cident1(Str s, const char* name, u32 len) {
  s = str_makeroom(s, len*2);
  for (u32 i = 0; i < len; i++) {
    char c = name[i];
    if (is_cident_nth(c)) {
      s = str_appendc(s, c);
    } else {
      s = str_appendc(s, '$');
      s = str_appendhex(s, (const u8*)&c, 1);
    }
  }
  return s;
}

static Str str_append_cident(Str s, const char* name) {
  u32 len = strlen(name);
  for (u32 i = 0; i < len; i++) {
    if (!is_cident_nth(name[i]))
      return str_append_cident1(s, name, len);
  }
  s = str_appendn(s, name, len);
  return s;
}


inline static Str fmt_rbnodes(const RBNode* n, Str s) {
  // descent first
  if (n->left) {
    s = fmt_rbnodes(n->left, s);
  }
  if (n->right) {
    s = fmt_rbnodes(n->right, s);
  }

  s = str_appendcstr(s, "static SymRBNode n_");
  s = str_append_cident(s, n->key);
  s = str_appendcstr(s, " = { ");

  // { key, isred, left, right }

  s = str_append(s, "kSym_");
  s = str_append_cident(s, n->key);
  s = str_append(s, ", ");

  s = str_appendcstr(s, n->isred ? "true, " : "false, ");
  if (n->left) {
    s = str_append(s, "&n_");
    s = str_append_cident(s, n->left->key);
    s = str_append(s, ", ");
  } else {
    s = str_appendcstr(s, "NULL, ");
  }
  if (n->right) {
    s = str_append(s, "&n_");
    s = str_append_cident(s, n->right->key);
    s = str_append(s, ", ");
  } else {
    s = str_appendcstr(s, "NULL ");
  }
  s = str_appendcstr(s, "};\n");
  return s;
}


static bool gen_append_symdef(Str* sp, RBNode** rp, Sym sym, const char* name) {
  assert(symlen(sym) < 1000);
  bool added;
  *rp = RBInsert(*rp, sym, &added, mem_libc_allocator());
  if (!added)
    return false;

  Str s = str_trunc(*sp);
  s = str_append(s, "const Sym kSym_");
  s = str_append_cident(s, name);
  s = str_append(s, " = &\"");

  const SymHeader* h = _SYM_HEADER(sym);
  const u8* hash = (const u8*)&h->hash;
  const u8* len  = (const u8*)&h->len;

  // hash, len, cap
  s = str_appendfmt(s, "\\x%02X\\x%02X\\x%02X\\x%02X", hash[0], hash[1], hash[2], hash[3]);
  s = str_appendfmt(s, "\\x%02X\\x%02X\\x%02X\\x%02X", len[0], len[1], len[2], len[3]);

  s = str_appendcstr(s, "\"\""); // in case sym starts with a hex digit
  s = str_appendrepr(s, sym, symlen(sym));
  s = str_appendfmt(s, "\\0\"[%d];\n", (int)sizeof(SymHeader));
  *sp = s;
  return true;
}


static bool gen_append_symdef_lit(Str* sp, RBNode** rp, Sym sym, const char* name, u8 flags) {
  sym_dangerously_set_flags(sym, flags);
  return gen_append_symdef(sp, rp, sym, name);
}


static bool gen_append_symdef_typecode_lit(
  Str* sp, RBNode** rp, SymPool* syms, char typecode_ch)
{
  char buf[2];
  buf[0] = typecode_ch;
  buf[1] = 0;
  Sym sym = symadd(syms, buf, 1);
  return gen_append_symdef(sp, rp, sym, buf);
}


__attribute__((constructor,used))
static void gen_constants() {
  printf("\n//-- BEGIN gen_constants()\n\n");

  Mem mem = mem_libc_allocator();

  Str tmpstr = str_make(mem, 512);

  SymPool syms;
  sympool_init(&syms, NULL, mem, NULL);

  // // define temporary runtime symbols
  // #define SYM_DEF1(name)              symaddcstr(&syms, #name);
  // #define SYM_DEF2(str, tok)          symaddcstr(&syms, #str);
  // #define SYM_DEF1_IGN2(name, _t, _v) symaddcstr(&syms, #name);
  // TOKEN_KEYWORDS(SYM_DEF2)
  // TYPE_SYMS(SYM_DEF1)
  // DEF_TYPE_CODES_BASIC(SYM_DEF1)
  // DEF_CONST_NODES_PUB(SYM_DEF1_IGN2)
  // DEF_SYMS_PUB(SYM_DEF1)
  // #undef SYM_DEF1
  // #undef SYM_DEF2
  // #undef SYM_DEF1_IGN2


  // ------------------------------------------------------------------------------------
  // generate symbol constants
  RBNode* root = NULL;
  str_trunc(tmpstr);

  #define SYM_GEN_NOFLAGS(name, ...)                                               \
    if (gen_append_symdef_lit(&tmpstr, &root, symgetcstr(&syms, #name), #name, 0)) \
      printf("%s", tmpstr->p);

  #define SYM_GEN_KEYWORD(tok, name) {                               \
    Sym sym = symgetcstr(&syms, #name);                              \
    u8 flags = (tok - TKeywordsStart);                               \
    if (!gen_append_symdef_lit(&tmpstr, &root, sym, #name, flags)) { \
      errlog("duplicate keyword symbol definition: %s", sym);        \
      exit(1);                                                       \
    }                                                                \
    printf("%s", tmpstr->p);                                         \
  }

  // keyword symbols must be generated first as they use custom Sym flags
  DEF_TOKENS_KEYWORD(SYM_GEN_KEYWORD)
  DEF_TYPE_CODES_BASIC_PUB(SYM_GEN_NOFLAGS)
  DEF_TYPE_CODES_BASIC(SYM_GEN_NOFLAGS)
  DEF_TYPE_CODES_PUB(SYM_GEN_NOFLAGS)
  DEF_CONST_NODES_PUB(SYM_GEN_NOFLAGS)
  DEF_SYMS_PUB(SYM_GEN_NOFLAGS)

  // generate typeid syms for use in Type constants
  #define SYM_GEN_TYPECODE(_name, encoding, ...)                         \
    if (gen_append_symdef_typecode_lit(&tmpstr, &root, &syms, encoding)) \
      printf("%s", tmpstr->p);

  DEF_TYPE_CODES_BASIC_PUB(SYM_GEN_TYPECODE)
  DEF_TYPE_CODES_BASIC(SYM_GEN_TYPECODE)
  DEF_TYPE_CODES_PUB(SYM_GEN_TYPECODE)

  // output rbtree
  Str sympool_s = fmt_rbnodes(root, str_make(mem, 0));
  printf("\n%s\n", sympool_s->p);
  printf("static SymRBNode* _symroot = &n_%s;\n", root->key);

  #undef SYM_GEN_NOFLAGS
  #undef SYM_GEN_TYPECODE
  #undef SYM_GEN_KEYWORD


  // ------------------------------------------------------------------------------------
  // generate AST nodes

  // DEF_TYPE_CODES_*
  printf(
    "\n"
    "#define _(NAME, TID, TFLAGS) \\\n"
    "  {.kind=NBasicType, .tflags=TFLAGS, .tid=TID, TC_##NAME, kSym_##NAME}\n");
  #define _(name, encoding, typeflags) printf(                       \
    "static const BasicTypeNode _kType_%s = _(%s, kSym_%s, %s);\n",  \
    #name, #name, cidentc(encoding), #typeflags );
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  printf("#undef _\n");
  #undef _
  #define _(name, ...) \
    printf("Type* kType_%s = (Type*)&_kType_%s;\n", #name, #name);
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  #undef _

  // DEF_CONST_NODES_PUB
  printf("\n");
  #define _(name, AST_TYPE, typecode_suffix, structinit) printf(             \
    "static const %sNode _kExpr_%s =\n"                                      \
    " {.kind=N%s,.flags=NF_Const|NF_RValue,.type=(Type*)&_kType_%s%s%s};\n", \
    #AST_TYPE, #name,                                                        \
    #AST_TYPE, #typecode_suffix,                                             \
    (structinit[0]==0 ? "" : ","), structinit);
  DEF_CONST_NODES_PUB(_)
  #undef _
  #define _(name, AST_TYPE, ...) printf( \
    "Expr* kExpr_%s = (Expr*)&_kExpr_%s;\n", #name, #name);
  DEF_CONST_NODES_PUB(_)
  #undef _


  // ------------------------------------------------------------------------------------
  // generate a sort of checksum used in debug mode to make sure the generator is updated
  // when keywords change. See the function debug_check() below as well.
  tmpstr = gen_checksum(str_trunc(tmpstr));
  printf(
    "\n"
    "#ifndef NDEBUG\n"
    "__attribute__((used)) static const char* const debugSymCheck =\n"
    "  \"%s\";\n#endif\n",
    tmpstr->p);


  // ------------------------------------------------------------------------------------

  printf("\n//-- END gen_constants()\n\n");
  str_free(tmpstr);
  sympool_dispose(&syms);
  exit(1);
}

/* end if RUN_GENERATOR */
#elif !defined(NDEBUG)

__attribute__((constructor)) static void debug_check() {
  u8 membuf[2048];
  DEF_MEM_STACK_BUF_ALLOCATOR(mem, membuf);
  Str s = gen_checksum(str_make(mem, sizeof(membuf)/2));
  if (strcmp(debugSymCheck, s->p) != 0) {
    errlog(
      "——————————————————————————————————————————————————————————————————————\n"
      "                    WARNING: Keywords changed\n"
      "——————————————————————————————————————————————————————————————————————\n"
      "Define RUN_GENERATOR in %s to run code generator.\n"
      "\ndebugSymCheck:\n%s\n\ndetected:\n%s\n"
      "——————————————————————————————————————————————————————————————————————\n\n",
      __FILE__, debugSymCheck, s->p);
  }
}
#endif
