// build LLVM IR from co AST
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#include "llvmimpl.h"
#include "../parse/parse.h"

// DEBUG_BUILD_EXPR: define to dlog trace build_expr
#define DEBUG_BUILD_EXPR


// make the code more readable by using short name aliases
typedef LLVMValueRef      Val;
typedef LLVMTypeRef       Typ;
typedef LLVMBasicBlockRef Block;


typedef enum {
  Immutable = 0, // must be 0
  Mutable   = 1, // must be 1
} Mutability;


// B is internal data used during IR construction
typedef struct B {
  BuildCtx*       build; // Co build (package, mem allocator, etc)
  LLVMContextRef  ctx;
  LLVMModuleRef   mod;
  LLVMBuilderRef  builder;

  // debug info
  bool prettyIR; // if true, include names in the IR (function params, variables, etc)
  //std::unique_ptr<DIBuilder>   DBuilder;
  //DebugInfo                    debug;

  // development debugging support
  #ifdef DEBUG_BUILD_EXPR
  int log_indent;
  char dname_buf[128];
  #endif

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // build state
  bool       noload;        // for NVar
  Mutability mut;       // true if inside mutable data context
  u32        fnest;         // function nest depth
  Val        varalloc;      // memory preallocated for a var's init
  SymMap     internedTypes; // AST types, keyed by typeid
  PMap       defaultInits;  // constant initializers (Typ => Val)

  // memory generation check (specific to current function)
  Block mgen_failb;
  Val   mgen_alloca; // alloca for failed ref (type "REF" { i32*, i32 })

  // type constants
  Typ t_void;
  Typ t_bool;
  Typ t_i8;
  Typ t_i16;
  Typ t_i32;
  Typ t_i64;
  Typ t_i128;
  Typ t_int; // t_i* type of at least pointer size
  Typ t_i8ptr; // i8*
  Typ t_f32;
  Typ t_f64;
  Typ t_f128;

  // ref struct types
  Typ t_ref; // "mut&T", "&T"

  // value constants
  Val v_i32_0; // i32 0
  Val v_int_0; // int 0

  // metadata values
  Val md_br_likely;
  Val md_br_unlikely;

  // metadata "kind" identifiers
  u32 md_kind_prof; // "prof"

} B;


// development debugging support
#ifdef DEBUG_BUILD_EXPR
static char kSpaces[256];
#endif


// table mapping signed integer binary operators
static const u32 kOpTableSInt[T_PRIM_OPS_END] = {
  // op is LLVMOpcode
  [TPlus]    = LLVMAdd,    // +
  [TMinus]   = LLVMSub,    // -
  [TStar]    = LLVMMul,    // *
  [TSlash]   = LLVMSDiv,   // /
  [TPercent] = LLVMSRem,   // %
  [TShl]     = LLVMShl,    // <<
  // The shift operators implement arithmetic shifts if the left operand
  // is a signed integer and logical shifts if it is an unsigned integer.
  [TShr]     = LLVMAShr,   // >>
  [TAnd]     = LLVMAnd,    // &
  [TPipe]    = LLVMOr,     // |
  [THat]     = LLVMXor,    // ^
  // op is LLVMIntPredicate
  [TEq]      = LLVMIntEQ,  // ==
  [TNEq]     = LLVMIntNE,  // !=
  [TLt]      = LLVMIntSLT, // <
  [TLEq]     = LLVMIntSLE, // <=
  [TGt]      = LLVMIntSGT, // >
  [TGEq]     = LLVMIntSGE, // >=
};

// table mapping unsigned integer binary operators
static const u32 kOpTableUInt[T_PRIM_OPS_END] = {
  // op is LLVMOpcode
  [TPlus]    = LLVMAdd,    // +
  [TMinus]   = LLVMSub,    // -
  [TStar]    = LLVMMul,    // *
  [TSlash]   = LLVMUDiv,   // /
  [TPercent] = LLVMURem,   // %
  [TShl]     = LLVMShl,    // <<
  [TShr]     = LLVMLShr,   // >>
  [TAnd]     = LLVMAnd,    // &
  [TPipe]    = LLVMOr,     // |
  [THat]     = LLVMXor,    // ^
  // op is LLVMIntPredicate
  [TEq]      = LLVMIntEQ,  // ==
  [TNEq]     = LLVMIntNE,  // !=
  [TLt]      = LLVMIntULT, // <
  [TLEq]     = LLVMIntULE, // <=
  [TGt]      = LLVMIntUGT, // >
  [TGEq]     = LLVMIntUGE, // >=
};

// table mapping floating-point number binary operators
static const u32 kOpTableFloat[T_PRIM_OPS_END] = {
  // op is LLVMOpcode
  [TPlus]    = LLVMFAdd,  // +
  [TMinus]   = LLVMFSub,  // -
  [TStar]    = LLVMFMul,  // *
  [TSlash]   = LLVMFDiv,  // /
  [TPercent] = LLVMFRem,  // %
  // op is LLVMRealPredicate
  [TEq]      = LLVMRealOEQ, // ==
  [TNEq]     = LLVMRealUNE, // != (true if unordered or not equal)
  [TLt]      = LLVMRealOLT, // <
  [TLEq]     = LLVMRealOLE, // <=
  [TGt]      = LLVMRealOGT, // >
  [TGEq]     = LLVMRealOGE, // >=
};

// make sure table values fit the storage type
static_assert(sizeof(LLVMOpcode) <= sizeof(u32), "");
static_assert(sizeof(LLVMIntPredicate) <= sizeof(u32), "");
static_assert(sizeof(LLVMRealPredicate) <= sizeof(u32), "");


#define assert_llvm_type_iskind(llvmtype, expect_typekind) \
  asserteq(LLVMGetTypeKind(llvmtype), (expect_typekind))

#define assert_llvm_type_isptrkind(llvmtype, expect_typekind) do { \
  asserteq(LLVMGetTypeKind(llvmtype), LLVMPointerTypeKind); \
  asserteq(LLVMGetTypeKind(LLVMGetElementType(llvmtype)), (expect_typekind)); \
} while(0)

#define assert_llvm_type_isptr(llvmtype) \
  asserteq(LLVMGetTypeKind(llvmtype), LLVMPointerTypeKind)

// we check for so many null values in this file that a shorthand increases legibility
#define notnull assertnotnull_debug


#define FMTNODE(n,bufno) \
  fmtnode(n, b->build->tmpbuf[bufno], sizeof(b->build->tmpbuf[bufno]))


#define CHECKNOMEM(expr) \
  ( UNLIKELY(expr) ? (b_errf(b->build, (PosSpan){0}, "out of memory"), true) : false )


static error builder_init(B* b, CoLLVMModule* m) {
  #ifdef DEBUG_BUILD_EXPR
  memset(kSpaces, ' ', sizeof(kSpaces));
  #endif

  LLVMContextRef ctx = LLVMGetModuleContext(m->M);

  *b = (B){
    .build = m->build,
    .ctx = ctx,
    .mod = m->M,
    .builder = LLVMCreateBuilderInContext(ctx),
    .prettyIR = true,

    // constants
    // note: no disposal needed of built-in types
    .t_void = LLVMVoidTypeInContext(ctx),
    .t_bool = LLVMInt1TypeInContext(ctx),
    .t_i8   = LLVMInt8TypeInContext(ctx),
    .t_i16  = LLVMInt16TypeInContext(ctx),
    .t_i32  = LLVMInt32TypeInContext(ctx),
    .t_i64  = LLVMInt64TypeInContext(ctx),
    .t_i128 = LLVMInt128TypeInContext(ctx),
    .t_f32  = LLVMFloatTypeInContext(ctx),
    .t_f64  = LLVMDoubleTypeInContext(ctx),
    .t_f128 = LLVMFP128TypeInContext(ctx),

    // metadata "kind" identifiers
    .md_kind_prof = LLVMGetMDKindIDInContext(ctx, "prof", 4),
  };

  // initialize int/uint types
  LLVMTargetDataRef dlayout = LLVMGetModuleDataLayout(b->mod);
  u32 ptrsize = LLVMPointerSize(dlayout);
  if (ptrsize <= 1)       b->t_int = b->t_i8;
  else if (ptrsize == 2)  b->t_int = b->t_i16;
  else if (ptrsize <= 4)  b->t_int = b->t_i32;
  else if (ptrsize <= 8)  b->t_int = b->t_i64;
  else if (ptrsize <= 16) b->t_int = b->t_i128;
  else panic("target pointer size too large: %u B", ptrsize);
  assertf(TF_Size(b->build->sint_type->tflags) == LLVMGetIntTypeWidth(b->t_int)/8,
    "builder was configured with a different int type (%u) than module (%u)",
    TF_Size(b->build->sint_type->tflags), LLVMGetIntTypeWidth(b->t_int)/8);

  // initialize common types
  b->t_i8ptr = LLVMPointerType(b->t_i8, 0);
  b->v_i32_0 = LLVMConstInt(b->t_i32, 0, /*signext*/false);
  b->v_int_0 = LLVMConstInt(b->t_int, 0, /*signext*/false);

  // FPM: Apply per-function optimizations. (NULL to disable.)
  // Really only useful for JIT; for offline compilation we use module-wide passes.
  //if (m->build->opt == OptPerf)
  //  b->FPM = LLVMCreateFunctionPassManagerForModule(b->mod);

  // initialize containers
  if (symmap_init(&b->internedTypes, b->build->mem, 16) == NULL) {
    LLVMDisposeBuilder(b->builder);
    return err_nomem;
  }
  if (pmap_init(&b->defaultInits, b->build->mem, 16, MAPLF_2) == NULL) {
    LLVMDisposeBuilder(b->builder);
    symmap_free(&b->internedTypes);
    return err_nomem;
  }

  // initialize function pass manager
  if (b->FPM) {
    // add optimization passes
    LLVMAddInstructionCombiningPass(b->FPM);
    LLVMAddReassociatePass(b->FPM);
    LLVMAddDCEPass(b->FPM);
    LLVMAddGVNPass(b->FPM);
    LLVMAddCFGSimplificationPass(b->FPM);
    // initialize FPM
    LLVMInitializeFunctionPassManager(b->FPM);
  }

  return 0;
}


static void builder_dispose(B* b) {
  symmap_free(&b->internedTypes);
  hmap_dispose(&b->defaultInits);
  if (b->FPM)
    LLVMDisposePassManager(b->FPM);
  LLVMDisposeBuilder(b->builder);
}


static bool val_is_ret(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMRet;
}

static bool val_is_call(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMCall;
}


inline static Block get_current_block(B* b) {
  return LLVMGetInsertBlock(b->builder);
}

inline static Val get_current_fun(B* b) {
  return LLVMGetBasicBlockParent(get_current_block(b));
}


#define noload_scope() \
  for (bool prev__ = b->noload, tmp1__ = true; \
       b->noload = true, tmp1__; \
       tmp1__ = false, b->noload = prev__)

#define doload_scope() \
  for (bool prev__ = b->noload, tmp1__ = true; \
       b->noload = false, tmp1__; \
       tmp1__ = false, b->noload = prev__)


//———————————————————————————————————————————————————————————————————————————————————————
// begin type functions


#if DEBUG
  static const char* fmttyp(Typ t) {
    if (!t)
      return "(null)";
    static char* p[5] = {NULL};
    static u32 index = 0;
    u32 i = index++;
    if (index == countof(p))
      index = 0;
    if (p[i])
      LLVMDisposeMessage(p[i]);
    p[i] = LLVMPrintTypeToString(t);
    return p[i];
  }

  static const char* fmtval(Val v) {
    if (!v)
      return "(null)";
    static char* p[5] = {NULL};
    static u32 index = 0;

    u32 i = index++;
    if (index == countof(p))
      index = 0;

    char* s = p[i];
    if (s)
      LLVMDisposeMessage(s);

    // avoid printing entire function bodies (just use its type)
    Typ ty = LLVMTypeOf(v);
    LLVMTypeKind tk = LLVMGetTypeKind(ty);
    while (tk == LLVMPointerTypeKind) {
      ty = LLVMGetElementType(ty);
      tk = LLVMGetTypeKind(ty);
    }
    if (tk == LLVMFunctionTypeKind) {
      s = LLVMPrintTypeToString(ty);
    } else {
      s = LLVMPrintValueToString(v);
    }

    // trim leading space
    while (*s == ' ')
      s++;

    return s;
  }
#endif


static Typ nullable get_interned_type(B* b, Type* tn) {
  assert_is_Type(tn);
  Sym tid = b_typeid(b->build, tn);
  return symmap_find(&b->internedTypes, tid);
}


static bool set_interned_type(B* b, Type* tn, Typ tr) {
  assert_is_Type(tn);
  Sym tid = b_typeid(b->build, tn);
  void** valp = symmap_assign(&b->internedTypes, tid);
  if CHECKNOMEM(valp == NULL)
    return false;
  *valp = tr;
  return true;
}


//———————————————————————————————————————————————————————————————————————————————————————
// begin type build functions


// get_type(B, AST type) => LLVM type
#define get_type(b, ast_type) \
  ({ Type* tn__=as_Type(ast_type); tn__ ? _get_type((b),tn__) : (b)->t_void; })
static Typ nullable _get_type(B* b, Type* np);


static Typ build_funtype(B* b, FunTypeNode* tn) {
  // first register a marker for the function type to avoid cyclic get_type,
  // i.e. in case result or parameters referst to the same type.
  set_interned_type(b, as_Type(tn), b->t_void);

  Typ rettype = get_type(b, tn->result);
  Typ paramsv[16];
  auto paramtypes = array_make(Array(Typ), paramsv, sizeof(paramsv));

  if (tn->params) {
    for (u32 i = 0; i < tn->params->a.len; i++) {
      Typ t = get_type(b, assertnotnull(tn->params->a.v[i]->type));
      assertf(t != b->t_void, "invalid type: %s", fmttyp(t));
      CHECKNOMEM(!array_push(&paramtypes, t));
    }
  }

  bool isVarArg = false;
  Typ ft = LLVMFunctionType(rettype, paramtypes.v, paramtypes.len, isVarArg);

  set_interned_type(b, as_Type(tn), ft);
  array_free(&paramtypes);
  return ft;
}


static Typ nullable get_basic_type(B* b, BasicTypeNode* tn) {
  switch (tn->typecode) {
    case TC_bool:               return b->t_bool;
    case TC_i8:  case TC_u8:    return b->t_i8;
    case TC_i16: case TC_u16:   return b->t_i16;
    case TC_i32: case TC_u32:   return b->t_i32;
    case TC_i64: case TC_u64:   return b->t_i64;
    case TC_f32:                return b->t_f32;
    case TC_f64:                return b->t_f64;
    case TC_int: case TC_uint:  return b->t_int;
    case TC_nil: case TC_ideal: return b->t_void;
  }
  assertf(0,"unexpected type code %u", tn->typecode);
  return b->t_void;
}


static Typ nullable _get_type(B* b, Type* np) {
  if (np->kind == NBasicType)
    return get_basic_type(b, (BasicTypeNode*)np);

  Typ t = get_interned_type(b, np);
  if (t)
    return t;

  switch ((enum NodeKind)np->kind) { case NBad: {
    NCASE(TypeType)   panic("TODO %s", nodename(n));
    NCASE(NamedType)  panic("TODO %s", nodename(n));
    NCASE(AliasType)  panic("TODO %s", nodename(n));
    NCASE(RefType)    panic("TODO %s", nodename(n));
    NCASE(ArrayType)  panic("TODO %s", nodename(n));
    NCASE(TupleType)  panic("TODO %s", nodename(n));
    NCASE(StructType) panic("TODO %s", nodename(n));
    NCASE(FunType)    return build_funtype(b, n);
    NDEFAULTCASE      break;
  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  return NULL;
}


// end type build functions
//———————————————————————————————————————————————————————————————————————————————————————
// begin value build functions


static Val build_expr(B* b, Expr* n, const char* vname);

inline static Val build_expr_noload(B* b, Expr* n, const char* vname) {
  bool noload = b->noload; b->noload = true;
  Val v = build_expr(b, n, vname);
  b->noload = noload;
  return v;
}

inline static Val build_expr_doload(B* b, Expr* n, const char* vname) {
  bool noload = b->noload; b->noload = false;
  Val v = build_expr(b, n, vname);
  b->noload = noload;
  return v;
}


static Val build_store(B* b, Val dst, Val val) {
  #if DEBUG
  Typ dst_type = LLVMTypeOf(dst);
  asserteq(LLVMGetTypeKind(dst_type), LLVMPointerTypeKind);
  if (LLVMTypeOf(val) != LLVMGetElementType(dst_type)) {
    panic("store destination type %s != source type %s",
      fmttyp(LLVMGetElementType(dst_type)), fmttyp(LLVMTypeOf(val)));
  }
  #endif
  return LLVMBuildStore(b->builder, val, dst);
}


static Val build_load(B* b, Typ elem_ty, Val src, const char* vname) {
  #if DEBUG
  Typ src_type = LLVMTypeOf(src);
  asserteq(LLVMGetTypeKind(src_type), LLVMPointerTypeKind);
  if (elem_ty != LLVMGetElementType(src_type)) {
    panic("load destination type %s != source type %s",
      fmttyp(elem_ty), fmttyp(LLVMGetElementType(src_type)));
  }
  #endif
  return LLVMBuildLoad2(b->builder, elem_ty, src, vname);
}


static Val build_funproto(B* b, FunNode* n, const char* name) {
  // get or build function type
  Typ ft = get_type(b, n->type);
  if UNLIKELY(!ft)
    return NULL;
  Val fn = LLVMAddFunction(b->mod, name, ft);

  // set argument names (for debugging)
  if (b->prettyIR) {
    for (u32 i = 0, len = n->params ? n->params->a.len : 0; i < len; i++) {
      ParamNode* param = as_ParamNode(n->params->a.v[i]);
      Val p = LLVMGetParam(fn, i);
      LLVMSetValueName2(p, param->name, symlen(param->name));
    }
  }

  return fn;
}


static Val build_fun(B* b, FunNode* n, const char* vname) {
  vname = n->name ? n->name : vname;

  // build function prototype
  Val fn = build_funproto(b, n, vname);
  n->irval = fn;

  if (!n->body) { // external
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
  }

  if (vname[0] == '_' /*strcmp(vname, "main") != 0*/ ) {
    // Note on LLVMSetVisibility: visibility is different.
    // See https://llvm.org/docs/LangRef.html#visibility-styles
    // LLVMPrivateLinkage is like "static" in C but omit from symbol table
    // LLVMSetLinkage(fn, LLVMPrivateLinkage);
    LLVMSetLinkage(fn, LLVMInternalLinkage); // like "static" in C
  }

  // prepare to build function body by saving any current builder position
  Block prevb = get_current_block(b);
  Block mgen_failb = b->mgen_failb;
  Val mgen_alloca = b->mgen_alloca;
  b->mgen_failb = NULL;
  b->mgen_alloca = NULL;
  b->fnest++;

  // create a new basic block to start insertion into
  Block entryb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, entryb);

  // create local storage for mutable parameters
  for (u32 i = 0, len = n->params ? n->params->a.len : 0; i < len; i++) {
    ParamNode* pn = as_ParamNode(n->params->a.v[i]);
    Val pv = LLVMGetParam(fn, i);
    Typ pt = LLVMTypeOf(pv);
    if (NodeIsConst(pn) /*&& !arg_type_needs_alloca(pt)*/) {
      // immutable pimitive value does not need a local alloca
      pn->irval = pv;
      continue;
    }
    // give the local a helpful name
    const char* name = pn->name;
    #if DEBUG
      char namebuf[128];
      snprintf(namebuf, sizeof(namebuf), "arg_%s", name);
      name = namebuf;
    #endif
    pn->irval = LLVMBuildAlloca(b->builder, pt, name);
    build_store(b, pn->irval, pv);
  }

  // build body
  Val bodyval = build_expr(b, n->body, "");

  // handle implicit return at end of body
  if (!bodyval || !val_is_ret(bodyval)) {
    if (!bodyval || as_FunTypeNode(n->type)->result == kType_nil) {
      LLVMBuildRetVoid(b->builder);
    } else {
      if (val_is_call(bodyval)) {
        // TODO: might need to add a condition for matching parameters & return type
        LLVMSetTailCall(bodyval, true);
      }
      LLVMBuildRet(b->builder, bodyval);
    }
  }

  // make sure failure blocks are at the end of the function,
  // since they are less likely to be visited.
  if (b->mgen_failb) {
    Block lastb = LLVMGetLastBasicBlock(fn);
    if (lastb != b->mgen_failb)
      LLVMMoveBasicBlockAfter(b->mgen_failb, lastb);
  }

  // restore any current builder position
  if (prevb) {
    LLVMPositionBuilderAtEnd(b->builder, prevb);
    b->mgen_failb = mgen_failb;
    b->mgen_alloca = mgen_alloca;
  }
  b->fnest--;

  // run optimization passes if enabled
  if (b->FPM)
    LLVMRunFunctionPassManager(b->FPM, fn);

  return fn;
}


static Val build_global_var(B* b, VarNode* n) {
  dlog("TODO");
  return NULL;
}


static void build_file(B* b, FileNode* n) {
  // first build all globals ...
  for (u32 i = 0; i < n->a.len; i++) {
    Node* np = n->a.v[i];
    if (np->kind == NVar)
      build_global_var(b, as_VarNode(np));
  }

  // ... then functions
  for (u32 i = 0; i < n->a.len; i++) {
    Node* np = n->a.v[i];
    switch ((enum NodeKind)np->kind) { case NBad: {
      NCASE(Fun)
        assertnotnull(n->name); // top-level functions are named
        build_fun(b, n, n->name);
      NCASE(Var)
        // ignore
      NDEFAULTCASE
        panic("TODO: file-level %s", NodeKindName(np->kind));
    }}
  }
}


static void build_pkg(B* b, PkgNode* n) {
  NodeArray* na = &b->build->pkg.a;
  for (u32 i = 0; i < na->len; i++) {
    FileNode* file = as_FileNode(na->v[i]);
    if (i == 0) {
      Str dir = str_make(b->build->tmpbuf[0], sizeof(b->build->tmpbuf[0]));
      usize n = path_dir(&dir, file->name, strlen(file->name));
      LLVMSetSourceFileName(b->mod, dir.v, dir.len);
      str_free(&dir);
    }
    build_file(b, file);
  }
}


static Val build_nil(B* b, NilNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}


static Val build_boollit(B* b, BoolLitNode* n, const char* vname) {
  return n->irval = LLVMConstInt(b->t_bool, n->ival, false);
}


static Val build_intlit(B* b, IntLitNode* n, const char* vname) {
  bool sign_extend = n->type->tflags & TF_Signed;
  return n->irval = LLVMConstInt(get_type(b, n->type), n->ival, sign_extend);
}


static Val build_floatlit(B* b, FloatLitNode* n, const char* vname) {
  return n->irval = LLVMConstReal(get_type(b, n->type), n->fval);
}


static Val build_strlit(B* b, StrLitNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}


static Val build_id(B* b, IdNode* n, const char* vname) {
  assertnotnull(n->target); // must be resolved
  return build_expr(b, as_Expr(n->target), n->name);
}


static Val build_binop(B* b, BinOpNode* n, const char* vname) {
  BasicTypeNode* tn = as_BasicTypeNode(n->type);
  assert(tn->typecode < TC_NUM_END);

  Val left = build_expr(b, n->left, "");
  Val right = build_expr(b, n->right, "");
  assert(LLVMTypeOf(left) == LLVMTypeOf(right));

  u32 op = 0;
  bool isfloat = false;
  assert(n->op < T_PRIM_OPS_END);
  switch (tn->typecode) {
    case TC_bool:
      // the boolean type has just two operators defined
      switch (n->op) {
        case TEq:  op = LLVMIntEQ; break; // ==
        case TNEq: op = LLVMIntNE; break; // !=
      }
      break;
    case TC_i8: case TC_i16: case TC_i32: case TC_i64: case TC_int:
      op = kOpTableSInt[n->op];
      break;
    case TC_u8: case TC_u16: case TC_u32: case TC_u64: case TC_uint:
      op = kOpTableUInt[n->op];
      break;
    case TC_f32: case TC_f64:
      isfloat = true;
      op = kOpTableFloat[n->op];
      break;
    default:
      break;
  }

  if UNLIKELY(op == 0) {
    b_errf(b->build, NodePosSpan(n), "invalid operand type %s", FMTNODE(tn,0));
    return NULL;
  }

  if (tok_is_cmp(n->op)) {
    // See how Go compares values: https://golang.org/ref/spec#Comparison_operators
    if (isfloat)
      return LLVMBuildFCmp(b->builder, (LLVMRealPredicate)op, left, right, vname);
    return LLVMBuildICmp(b->builder, (LLVMIntPredicate)op, left, right, vname);
  }
  return LLVMBuildBinOp(b->builder, (LLVMOpcode)op, left, right, vname);
}


static Val build_prefixop(B* b, PrefixOpNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_postfixop(B* b, PostfixOpNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_return(B* b, ReturnNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_const(B* b, ConstNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_macroparam(B* b, MacroParamNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_var(B* b, VarNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}


static Val build_param(B* b, ParamNode* n, const char* vname) {
  Val paramval = assertnotnull(n->irval); // note: irval set by build_fun
  if (NodeIsConst(n) || b->noload)
    return paramval;
  assert_llvm_type_isptr(LLVMTypeOf(paramval));
  Typ elem_type = LLVMGetElementType(LLVMTypeOf(paramval));
  return build_load(b, elem_type, paramval, vname);
}


static Val build_assign_local(B* b, AssignNode* n, const char* vname) {
  LocalNode* dstn = as_LocalNode(n->dst);
  Val dst = build_expr_noload(b, n->dst, dstn->name);
  Val val = build_expr(b, n->val, "");
  build_store(b, dst, val);
  return val;
}


static Val build_assign_tuple(B* b, AssignNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_assign_index(B* b, AssignNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_assign_selector(B* b, AssignNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_assign(B* b, AssignNode* n, const char* vname) {
  switch (assertnotnull(n->dst)->kind) {
    case NLocal_BEG ... NLocal_END: return build_assign_local(b, n, vname);
    case NTuple:                    return build_assign_tuple(b, n, vname);
    case NIndex:                    return build_assign_index(b, n, vname);
    case NSelector:                 return build_assign_selector(b, n, vname);
  }
  assertf(0,"invalid assignment destination %s", nodename(n->dst));
  return NULL;
}


static Val build_tuple(B* b, TupleNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_array(B* b, ArrayNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}


static Val build_block(B* b, BlockNode* n, const char* vname) {
  assertf(n->a.len > 0, "empty block");
  u32 i = 0;
  for (; i < n->a.len - 1; i++)
     build_expr(b, n->a.v[i], "");
  // last expr of block is its value
  return build_expr(b, n->a.v[i], "");
}


static Val build_macro(B* b, MacroNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_call(B* b, CallNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_typecast(B* b, TypeCastNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_ref(B* b, RefNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_namedarg(B* b, NamedArgNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_selector(B* b, SelectorNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_index(B* b, IndexNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_slice(B* b, SliceNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}

static Val build_if(B* b, IfNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return NULL;
}


static Val build_expr(B* b, Expr* np, const char* vname) {
  switch ((enum NodeKind)np->kind) { case NBad: {
  NCASE(Nil)        return build_nil(b, n, vname);
  NCASE(BoolLit)    return build_boollit(b, n, vname);
  NCASE(IntLit)     return build_intlit(b, n, vname);
  NCASE(FloatLit)   return build_floatlit(b, n, vname);
  NCASE(StrLit)     return build_strlit(b, n, vname);
  NCASE(Id)         return build_id(b, n, vname);
  NCASE(BinOp)      return build_binop(b, n, vname);
  NCASE(PrefixOp)   return build_prefixop(b, n, vname);
  NCASE(PostfixOp)  return build_postfixop(b, n, vname);
  NCASE(Return)     return build_return(b, n, vname);
  NCASE(Assign)     return build_assign(b, n, vname);
  NCASE(Tuple)      return build_tuple(b, n, vname);
  NCASE(Array)      return build_array(b, n, vname);
  NCASE(Block)      return build_block(b, n, vname);
  NCASE(Fun)        return build_fun(b, n, vname);
  NCASE(Macro)      return build_macro(b, n, vname);
  NCASE(Call)       return build_call(b, n, vname);
  NCASE(TypeCast)   return build_typecast(b, n, vname);
  NCASE(Const)      return build_const(b, n, vname);
  NCASE(Var)        return build_var(b, n, vname);
  NCASE(Param)      return build_param(b, n, vname);
  NCASE(MacroParam) return build_macroparam(b, n, vname);
  NCASE(Ref)        return build_ref(b, n, vname);
  NCASE(NamedArg)   return build_namedarg(b, n, vname);
  NCASE(Selector)   return build_selector(b, n, vname);
  NCASE(Index)      return build_index(b, n, vname);
  NCASE(Slice)      return build_slice(b, n, vname);
  NCASE(If)         return build_if(b, n, vname);
  NDEFAULTCASE      break;
  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  return NULL;
}


// end build functions
//———————————————————————————————————————————————————————————————————————————————————————


error llvm_module_build(CoLLVMModule* m, const CoLLVMBuild* opt) {
  // initialize builder
  B b_; B* b = &b_;
  error err = builder_init(b, m);
  if (err)
    return err;

  // build package
  build_pkg(b, &m->build->pkg);

  // verify IR
  #ifdef DEBUG
    char* errmsg;
    bool ok = LLVMVerifyModule(b->mod, LLVMPrintMessageAction, &errmsg) == 0;
    if (!ok) {
      //errlog("=========== LLVMVerifyModule ===========\n%s\n", errmsg);
      LLVMDisposeMessage(errmsg);
      dlog("\n=========== LLVMDumpModule ===========");
      LLVMDumpModule(b->mod);
      builder_dispose(b);
      return err_invalid;
    }
  #endif

  // finalize all function passes scheduled in the function pass
  if (b->FPM)
    LLVMFinalizeFunctionPassManager(b->FPM);

  // log LLVM IR
  #ifdef DEBUG
    dlog("LLVM IR module as built:");
    LLVMDumpModule(b->mod);
  #endif

  // cleanup
  builder_dispose(b);
  return err;
}
