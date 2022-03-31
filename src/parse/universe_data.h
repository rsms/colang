// generated by gen_constants at the end of this file, included by universe.c
//
// TODO: Move this generator to a script.
//       Currently it is really hard to use when e.g. changing types (TypeCode_*).
//
// Define to run the generator during program initialization:
#define RUN_GENERATOR 0

static const Type _kType_type = {.kind=NTypeType};
Type* kType_type = (Type*)&_kType_type;

static const Node _kNode_bad = {.kind=NBad};
Node* kNode_bad = (Node*)&_kNode_bad;

//-- BEGIN gen_constants()

const Sym kSym_as = &"\x19\xDB\x2E\xA0\x02\x00\x00\x08""as\0"[8];
const Sym kSym_auto = &"\x20\x06\xDB\x16\x04\x00\x00\x10""auto\0"[8];
const Sym kSym_break = &"\xA7\xDF\x5B\xDD\x05\x00\x00\x18""break\0"[8];
const Sym kSym_continue = &"\x97\x80\xFE\x4A\x08\x00\x00\x20""continue\0"[8];
const Sym kSym_defer = &"\xF3\x4C\x86\x1F\x05\x00\x00\x28""defer\0"[8];
const Sym kSym_else = &"\x94\x5F\x3E\xCD\x04\x00\x00\x30""else\0"[8];
const Sym kSym_enum = &"\x04\x0D\x27\xC5\x04\x00\x00\x38""enum\0"[8];
const Sym kSym_for = &"\xCA\x11\x5A\xA8\x03\x00\x00\x40""for\0"[8];
const Sym kSym_fun = &"\x20\x8F\x7E\x6F\x03\x00\x00\x48""fun\0"[8];
const Sym kSym_if = &"\xC9\x99\x85\x4D\x02\x00\x00\x50""if\0"[8];
const Sym kSym_import = &"\x98\xAA\x01\xF9\x06\x00\x00\x58""import\0"[8];
const Sym kSym_in = &"\x44\xE9\x31\xB9\x02\x00\x00\x60""in\0"[8];
const Sym kSym_nil = &"\x63\x79\xC4\xDB\x03\x00\x00\x68""nil\0"[8];
const Sym kSym_return = &"\xF2\x32\x75\xDD\x06\x00\x00\x70""return\0"[8];
const Sym kSym_struct = &"\x7F\xF5\x48\xD3\x06\x00\x00\x78""struct\0"[8];
const Sym kSym_switch = &"\x33\xA7\x6E\x6B\x06\x00\x00\x80""switch\0"[8];
const Sym kSym_type = &"\xBE\xAD\x4A\xE8\x04\x00\x00\x88""type\0"[8];
const Sym kSym_const = &"\xF9\xF3\x7D\x5B\x05\x00\x00\x90""const\0"[8];
const Sym kSym_mut = &"\xD9\xAA\x8A\xEA\x03\x00\x00\x98""mut\0"[8];
const Sym kSym_var = &"\x88\xDB\x36\xDB\x03\x00\x00\xA0""var\0"[8];
const Sym kSym_bool = &"\x68\xB8\x13\xA1\x04\x00\x00\x00""bool\0"[8];
const Sym kSym_i8 = &"\x10\x10\x1E\x14\x02\x00\x00\x00""i8\0"[8];
const Sym kSym_u8 = &"\x62\x07\xF2\x58\x02\x00\x00\x00""u8\0"[8];
const Sym kSym_i16 = &"\x7D\x38\x03\xA7\x03\x00\x00\x00""i16\0"[8];
const Sym kSym_u16 = &"\x1D\x39\x9D\x29\x03\x00\x00\x00""u16\0"[8];
const Sym kSym_i32 = &"\x77\x0C\xAC\xA3\x03\x00\x00\x00""i32\0"[8];
const Sym kSym_u32 = &"\xEB\x5A\x7F\x99\x03\x00\x00\x00""u32\0"[8];
const Sym kSym_i64 = &"\x9F\x90\x38\x42\x03\x00\x00\x00""i64\0"[8];
const Sym kSym_u64 = &"\x21\x47\xF0\xA8\x03\x00\x00\x00""u64\0"[8];
const Sym kSym_f32 = &"\xB7\x18\x6E\xF2\x03\x00\x00\x00""f32\0"[8];
const Sym kSym_f64 = &"\x47\xEA\x4B\xB7\x03\x00\x00\x00""f64\0"[8];
const Sym kSym_int = &"\xD9\xD4\xAC\x6A\x03\x00\x00\x00""int\0"[8];
const Sym kSym_uint = &"\x60\x48\xFD\x9C\x04\x00\x00\x00""uint\0"[8];
const Sym kSym_ideal = &"\x3C\x78\xBD\x6C\x05\x00\x00\x00""ideal\0"[8];
const Sym kSym_str = &"\xF8\xE1\xAF\x23\x03\x00\x00\x00""str\0"[8];
const Sym kSym_true = &"\x1E\xA2\x73\x61\x04\x00\x00\x00""true\0"[8];
const Sym kSym_false = &"\xAF\x1E\x0E\xF7\x05\x00\x00\x00""false\0"[8];
const Sym kSym__ = &"\xAC\x26\xED\xBD\x01\x00\x00\x00""_\0"[8];
const Sym kSym_b = &"\x37\xB8\xD4\x56\x01\x00\x00\x00""b\0"[8];
const Sym kSym_c = &"\x6F\x1F\xBB\xA5\x01\x00\x00\x00""c\0"[8];
const Sym kSym_B = &"\xE8\x8A\xC4\xF7\x01\x00\x00\x00""B\0"[8];
const Sym kSym_s = &"\x84\x12\x4E\xD3\x01\x00\x00\x00""s\0"[8];
const Sym kSym_S = &"\x7B\x6F\x18\xCA\x01\x00\x00\x00""S\0"[8];
const Sym kSym_w = &"\x04\xC6\x24\xFC\x01\x00\x00\x00""w\0"[8];
const Sym kSym_W = &"\x1F\xDB\x11\x23\x01\x00\x00\x00""W\0"[8];
const Sym kSym_d = &"\xA5\x13\x61\xEC\x01\x00\x00\x00""d\0"[8];
const Sym kSym_D = &"\x6A\x8C\xF1\x6B\x01\x00\x00\x00""D\0"[8];
const Sym kSym_f = &"\x2A\x42\xC1\x5E\x01\x00\x00\x00""f\0"[8];
const Sym kSym_F = &"\xA4\xF4\xCC\x17\x01\x00\x00\x00""F\0"[8];
const Sym kSym_i = &"\x09\x8C\x6F\xBF\x01\x00\x00\x00""i\0"[8];
const Sym kSym_u = &"\x2E\x0F\xF1\x26\x01\x00\x00\x00""u\0"[8];
const Sym kSym_0 = &"\x81\xEF\x0D\xE6\x01\x00\x00\x00""0\0"[8];
const Sym kSym_$2A = &"\xC3\x15\x5C\x91\x01\x00\x00\x00""*\0"[8];
const Sym kSym_$22 = &"\x2E\x7F\x10\xD9\x01\x00\x00\x00""\"\0"[8];
const Sym kSym_a = &"\x3E\xF6\x16\x1D\x01\x00\x00\x00""a\0"[8];

static SymRBNode n_i8 = { kSym_i8, false, NULL, NULL };
static SymRBNode n_F = { kSym_F, true, NULL, NULL };
static SymRBNode n_a = { kSym_a, false, &n_F, NULL };
static SymRBNode n_auto = { kSym_auto, true, &n_i8, &n_a, };
static SymRBNode n_W = { kSym_W, false, NULL, NULL };
static SymRBNode n_defer = { kSym_defer, false, &n_auto, &n_W, };
static SymRBNode n_u = { kSym_u, true, NULL, NULL };
static SymRBNode n_u16 = { kSym_u16, false, &n_u, NULL };
static SymRBNode n_continue = { kSym_continue, false, NULL, NULL };
static SymRBNode n_i64 = { kSym_i64, false, &n_u16, &n_continue, };
static SymRBNode n_str = { kSym_str, true, &n_defer, &n_i64, };
static SymRBNode n_b = { kSym_b, true, NULL, NULL };
static SymRBNode n_u8 = { kSym_u8, false, &n_b, NULL };
static SymRBNode n_f = { kSym_f, true, NULL, NULL };
static SymRBNode n_int = { kSym_int, true, NULL, NULL };
static SymRBNode n_true = { kSym_true, false, &n_f, &n_int, };
static SymRBNode n_const = { kSym_const, false, &n_u8, &n_true, };
static SymRBNode n_D = { kSym_D, false, NULL, NULL };
static SymRBNode n_fun = { kSym_fun, true, NULL, NULL };
static SymRBNode n_$2A = { kSym_$2A, false, &n_fun, NULL };
static SymRBNode n_ideal = { kSym_ideal, true, &n_D, &n_$2A, };
static SymRBNode n_uint = { kSym_uint, false, NULL, NULL };
static SymRBNode n_u32 = { kSym_u32, false, &n_ideal, &n_uint, };
static SymRBNode n_switch = { kSym_switch, true, &n_const, &n_u32, };
static SymRBNode n_if = { kSym_if, false, &n_str, &n_switch, };
static SymRBNode n_bool = { kSym_bool, true, NULL, NULL };
static SymRBNode n_c = { kSym_c, true, NULL, NULL };
static SymRBNode n_i32 = { kSym_i32, false, &n_bool, &n_c, };
static SymRBNode n_for = { kSym_for, true, NULL, NULL };
static SymRBNode n_f64 = { kSym_f64, true, NULL, NULL };
static SymRBNode n_u64 = { kSym_u64, false, &n_for, &n_f64, };
static SymRBNode n_i16 = { kSym_i16, false, &n_i32, &n_u64, };
static SymRBNode n__ = { kSym__, true, NULL, NULL };
static SymRBNode n_i = { kSym_i, false, &n__, NULL };
static SymRBNode n_S = { kSym_S, false, NULL, NULL };
static SymRBNode n_enum = { kSym_enum, false, &n_i, &n_S, };
static SymRBNode n_in = { kSym_in, true, &n_i16, &n_enum, };
static SymRBNode n_struct = { kSym_struct, true, NULL, NULL };
static SymRBNode n_$22 = { kSym_$22, true, NULL, NULL };
static SymRBNode n_s = { kSym_s, false, &n_struct, &n_$22, };
static SymRBNode n_nil = { kSym_nil, false, NULL, NULL };
static SymRBNode n_var = { kSym_var, false, &n_s, &n_nil, };
static SymRBNode n_else = { kSym_else, false, &n_in, &n_var, };
static SymRBNode n_as = { kSym_as, true, &n_if, &n_else, };
static SymRBNode n_return = { kSym_return, true, NULL, NULL };
static SymRBNode n_0 = { kSym_0, false, &n_return, NULL };
static SymRBNode n_mut = { kSym_mut, true, NULL, NULL };
static SymRBNode n_d = { kSym_d, false, &n_mut, NULL };
static SymRBNode n_type = { kSym_type, false, &n_0, &n_d, };
static SymRBNode n_false = { kSym_false, false, NULL, NULL };
static SymRBNode n_import = { kSym_import, true, NULL, NULL };
static SymRBNode n_w = { kSym_w, false, &n_import, NULL };
static SymRBNode n_B = { kSym_B, false, &n_false, &n_w, };
static SymRBNode n_f32 = { kSym_f32, false, &n_type, &n_B, };
static SymRBNode n_break = { kSym_break, false, &n_as, &n_f32, };

static SymRBNode* _symroot = &n_break;

#define _(NAME, TID, TFLAGS) \
  {.kind=NBasicType, .tflags=TFLAGS, .tid=TID, TC_##NAME, kSym_##NAME}
static const BasicTypeNode _kType_bool = _(bool, kSym_b, TF_KindBool);
static const BasicTypeNode _kType_i8 = _(i8, kSym_c, TF_KindInt | TF_Size1 | TF_Signed);
static const BasicTypeNode _kType_u8 = _(u8, kSym_B, TF_KindInt | TF_Size1);
static const BasicTypeNode _kType_i16 = _(i16, kSym_s, TF_KindInt | TF_Size2 | TF_Signed);
static const BasicTypeNode _kType_u16 = _(u16, kSym_S, TF_KindInt | TF_Size2);
static const BasicTypeNode _kType_i32 = _(i32, kSym_w, TF_KindInt | TF_Size4 | TF_Signed);
static const BasicTypeNode _kType_u32 = _(u32, kSym_W, TF_KindInt | TF_Size4);
static const BasicTypeNode _kType_i64 = _(i64, kSym_d, TF_KindInt | TF_Size8 | TF_Signed);
static const BasicTypeNode _kType_u64 = _(u64, kSym_D, TF_KindInt | TF_Size8);
static const BasicTypeNode _kType_f32 = _(f32, kSym_f, TF_KindF32 | TF_Size4 | TF_Signed);
static const BasicTypeNode _kType_f64 = _(f64, kSym_F, TF_KindF64 | TF_Size8 | TF_Signed);
static const BasicTypeNode _kType_int = _(int, kSym_i, TF_KindInt | TF_Signed);
static const BasicTypeNode _kType_uint = _(uint, kSym_u, TF_KindInt);
static const BasicTypeNode _kType_nil = _(nil, kSym_0, TF_KindVoid);
static const BasicTypeNode _kType_ideal = _(ideal, kSym_$2A, TF_KindVoid);
static const BasicTypeNode _kType_str = _(str, kSym_$22, TF_KindPointer);
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

#define kUniverseScopeLen 19

//-- END gen_constants()

// ---------------------------------------------------------------------------------------
// sym constant data generator

#if RUN_GENERATOR || defined(DEBUG)
  static usize gen_checksum(char* buf, usize bufcap) {
    ABuf s = abuf_make(buf, bufcap);

    #define _(tok, str) abuf_fmt(&s, "kw:%s=%s ", #str, #tok);
    DEF_TOKENS_KEYWORD(_)
    #undef _

    #define _(name, ...) abuf_cstr(&s, "tc:" #name " ");
    DEF_TYPE_CODES_BASIC_PUB(_)
    DEF_TYPE_CODES_BASIC(_)
    DEF_TYPE_CODES_PUB(_)
    #undef _

    #define _(name, ...) abuf_cstr(&s, "sym:" #name " ");
    DEF_SYMS_PUB(_)
    #undef _

    #define _(name, nkind, typecode_suffix, structinit) \
      abuf_fmt(&s, "const:%s,%s,%s=%s ", #name, #nkind, #typecode_suffix, structinit);
    DEF_CONST_NODES_PUB(_)
    #undef _

    if (s.len > 0) {
      // trim trailing space
      s.len--;
      s.p--;
    }

    return abuf_terminate(&s);
  }
#endif


#if RUN_GENERATOR
#if !defined(DEBUG)
  #error Trying to run the generator in a non-debug build
#endif

#ifdef CO_NO_LIBC
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

  Str tmpstr = str_make(mem, 512); // TODO: convert to (array-backed) Str or ABuf

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
  // generate scope map
  {
    // count scope map entries
    u32 keycount = 0;
    #define _(...) keycount++;
    DEF_TYPE_CODES_BASIC_PUB(_)
    DEF_TYPE_CODES_BASIC(_)
    DEF_TYPE_CODES_PUB(_)
    DEF_CONST_NODES_PUB(_)
    #undef _
    keycount--; // don't count kType_nil

    printf("\n");
    printf("#define kUniverseScopeLen %u\n", keycount);

    // TODO: look into generating the HMap. The main challenge with doing this is that
    // we use pointers to kSym_*s as keys, which vary per runtime session, so we would
    // have to use symhash with symmap to get stable keys. However the issue with doing
    // that is that then we would have poor cache locality as every internal map
    // comparison would have to load a symbol's hash from wherever that symbol lives in
    // memory.
  }

  // ------------------------------------------------------------------------------------

  printf("\n//-- END gen_constants()\n\n");
  str_free(tmpstr);
  sympool_dispose(&syms);
  exit(1);
}

/* end if RUN_GENERATOR */
#elif !defined(NDEBUG)

__attribute__((constructor)) static void debug_check() {
  char buf[2048];
  gen_checksum(buf, sizeof(buf));
  if (strcmp(debugSymCheck, buf) != 0) {
    errlog(
      "——————————————————————————————————————————————————————————————————————\n"
      "                    WARNING: Keywords changed\n"
      "——————————————————————————————————————————————————————————————————————\n"
      "Define RUN_GENERATOR in %s to run code generator.\n"
      "\ndebugSymCheck:\n%s\n\ndetected:\n%s\n"
      "——————————————————————————————————————————————————————————————————————\n\n",
      __FILE__, debugSymCheck, buf);
  }
}
#endif