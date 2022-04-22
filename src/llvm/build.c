// build LLVM IR from co AST
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#include "llvmimpl.h"
#include "../parse/parse.h"

// DEBUG_BUILD_EXPR: define to dlog trace build_expr
#define DEBUG_BUILD_EXPR

#if defined(DEBUG_BUILD_EXPR) && !defined(DEBUG)
  #undef DEBUG_BUILD_EXPR
#elif defined(DEBUG_BUILD_EXPR) && !defined(CO_NO_LIBC)
  #include <unistd.h> // isatty
#endif


// make the code more readable by using short name aliases
typedef LLVMValueRef      Val;
typedef LLVMTypeRef       Typ;
typedef LLVMBasicBlockRef Block;


typedef enum {
  BFL_MUT  = 1 << 0,
  BFL_RVAL = 1 << 1,
} BFlags;


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
  int debug_depth;
  char vname_buf[128];
  #endif

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // build state
  BFlags     flags;             // contextual
  u32        fnest;             // function nest depth
  Val        varalloc;          // memory preallocated for a var's init
  SymMap     internedTypes;     // AST types, keyed by typeid
  PSet       templateInstances; // used template instances (TemplateNode*)
  ASTVisitor astVisitor;

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
  Val v_i32_0;    // (i32)0
  Val v_int_0;    // (int)0
  Val v_intptr_0; // (int*)NULL

  // metadata values
  Val md_br_likely;
  Val md_br_unlikely;

  // metadata "kind" identifiers
  u32 md_kind_prof; // "prof"

} B;


// development debugging support
#ifdef DEBUG_BUILD_EXPR
  //static B b = {0}; // in case panic or dlog2 is called in scope without b
  #ifdef CO_NO_LIBC
    #define isatty(fd) false
  #endif
  #define dlog2(format, args...) ({                                               \
    if (isatty(2)) log("\e[1;30m▍\e[0m%*s" format, b->debug_depth*2, "", ##args); \
    else           log("[build] %*s" format, b->debug_depth*2, "", ##args);       \
    fflush(stderr); })
  #undef panic
  #define panic(format, args...) ({ \
    log("\e[33;1m—————————————————————————————— panic ——————————————————————————————\e[0m");\
    log(format, ##args); \
    log("\e[2m%s:%d  %s\e[0m", __FILE__, __LINE__, __PRETTY_FUNCTION__); \
    log("\e[33;1m———————————————————————— llvm module state ————————————————————————\e[0m");\
    LLVMDumpModule(b->mod); \
    log("\e[33;1m—————————————————————————————— trace ——————————————————————————————\e[0m");\
    sys_stacktrace_fwrite(stderr, /*offset*/0, /*limit*/100); \
    abort(); \
  })
#else
  #define dlog2(...) ((void)0)
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

// TODO: Find a different way to implement this macro since
// calling LLVMGetElementType on a pointer type is being deprecated in LLVM
// as part of the migration to opaque pointers.
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

  // initialize common constant values
  b->v_i32_0 = LLVMConstInt(b->t_i32, 0, /*signext*/false);
  b->v_int_0 = LLVMConstInt(b->t_int, 0, /*signext*/false);
  b->v_intptr_0 = LLVMConstNull(LLVMPointerType(b->t_int, 0));

  // FPM: Apply per-function optimizations. (NULL to disable.)
  // Really only useful for JIT; for offline compilation we use module-wide passes.
  //if (m->build->opt == OptPerf)
  //  b->FPM = LLVMCreateFunctionPassManagerForModule(b->mod);

  // initialize containers
  if (symmap_init(&b->internedTypes, b->build->mem, 16) == NULL) {
    LLVMDisposeBuilder(b->builder);
    return err_nomem;
  }
  if (pset_init(&b->templateInstances, b->build->mem, 16, MAPLF_2) == NULL) {
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
  pset_free(&b->templateInstances);
  if (b->FPM)
    LLVMDisposePassManager(b->FPM);
  LLVMDisposeBuilder(b->builder);
  if (b->astVisitor.ctx)
    ASTVisitorDispose(&b->astVisitor);
}


// vnamef formats IR value names
#if defined(DEBUG) && defined(DEBUG_BUILD_EXPR)
  ATTR_FORMAT(printf, 2, 3)
  static const char* vnamef(B* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    b->vname_buf[0] = 0;
    vsnprintf(b->vname_buf, sizeof(b->vname_buf), fmt, ap);
    va_end(ap);
    return b->vname_buf;
  }
#else
  #define vnamef(b, fmt, ...) ""
#endif


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

// inline static Val get_current_fun(B* b) {
//   return LLVMGetBasicBlockParent(get_current_block(b));
// }


#define flag_scope(newflags) \
  for (BFlags prev__ = b->flags, tmp1__ = 1; \
       tmp1__ && (b->flags = (newflags), 1); \
       tmp1__ = 0, b->flags = prev__)



//———————————————————————————————————————————————————————————————————————————————————————
// begin misc helper functions


#if DEBUG
  static const char* fmttyp(Typ nullable t) {
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

  UNUSED static const char* fmtval(Val nullable v) {
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
      // TODO: LLVMGetElementType on pointers is deprecated; find another way to do this,
      // to get the function type from a value.
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


static Typ get_basic_type(B* b, BasicTypeNode* tn) {
  switch ((enum TypeCodeBasic)tn->typecode) {
    case TC_bool:               return b->t_bool;
    case TC_i8:   case TC_u8:   return b->t_i8;
    case TC_i16:  case TC_u16:  return b->t_i16;
    case TC_i32:  case TC_u32:  return b->t_i32;
    case TC_i64:  case TC_u64:  return b->t_i64;
    case TC_i128: case TC_u128: return b->t_i128;
    case TC_int:  case TC_uint: return b->t_int;

    case TC_f32:  return b->t_f32;
    case TC_f64:  return b->t_f64;
    case TC_f128: return b->t_f128;

    case TC_rawptr: return b->t_i8ptr;

    case TC_nil:
    case TC_ideal:
      return b->t_void;
  }
  assertf(0,"unexpected type code %u", tn->typecode);
  return b->t_void;
}


static Typ make_fun_type(B* b, FunTypeNode* tn) {
  // first register a marker for the function type to avoid cyclic get_type,
  // i.e. in case result or parameters referst to the same type.
  set_interned_type(b, as_Type(tn), b->t_void);

  Typ rettype = get_type(b, tn->result);
  Typ paramsv[16];
  auto paramtypes = array_make(Array(Typ), paramsv, sizeof(paramsv));
  CHECKNOMEM(!array_reserve(&paramtypes, tn->params->len));
  paramtypes.len = tn->params->len;

  for (u32 i = 0; i < tn->params->len; i++) {
    Typ t = get_type(b, assertnotnull(tn->params->v[i]->type));
    assertf(t != b->t_void, "invalid type: %s", fmttyp(t));
    paramtypes.v[i] = t;
  }

  bool isVarArg = false;
  Typ ft = LLVMFunctionType(rettype, paramtypes.v, paramtypes.len, isVarArg);

  set_interned_type(b, as_Type(tn), ft);
  array_free(&paramtypes);
  return ft;
}


static Typ make_cslice_type(B* b, Typ elem) {
  // struct cslice<T> { const T* p; uint len; }
  Typ types[] = { LLVMPointerType(elem, 0), b->t_int };
  return LLVMStructTypeInContext(b->ctx, types, countof(types), /*packed*/false);
}


static Typ make_slice_type(B* b, Typ elem) {
  // struct slice<T> { T* p; uint len; uint cap; }
  Typ types[] = { LLVMPointerType(elem, 0), b->t_int, b->t_int };
  return LLVMStructTypeInContext(b->ctx, types, countof(types), /*packed*/false);
}


static Typ make_dynarray_type(B* b, Typ elem) {
  // struct dynarray<T> { T* p; uint len; uint cap; }
  return make_slice_type(b, elem);
}


static Typ make_array_type(B* b, ArrayTypeNode* tn) {
  Typ elemty = get_type(b, tn->elem);

  // [T] => dynarray<T>
  if (tn->size == 0)
    return make_dynarray_type(b, elemty);

  // [T N] => T[N]
  return LLVMArrayType(elemty, tn->size);
}


static Typ make_ref_type(B* b, RefTypeNode* t) {
  // &[T] => cslice<T>
  // mut&[T] => slice<T>
  ArrayTypeNode* arrayt = (ArrayTypeNode*)t->elem;
  if (t->elem->kind == NArrayType && arrayt->size == 0) {
    Typ elemty = get_type(b, arrayt->elem);
    if (NodeIsConst(t))
      return make_cslice_type(b, elemty);
    return make_slice_type(b, elemty);
  }

  // &T => T*
  if (b->build->safe)
    dlog("TODO safe deref wrapper type");
  Typ elemty = get_type(b, t->elem);
  return LLVMPointerType(elemty, 0);
}


static Typ _get_type(B* b, Type* np) {
  if (np->kind == NBasicType)
    return get_basic_type(b, (BasicTypeNode*)np);

  if (np->irval)
    return np->irval;

  Typ t = get_interned_type(b, np);
  if (t)
    return t;

  switch ((enum NodeKind)np->kind) { case NBad: {
    NCASE(FunType)   t = make_fun_type(b, n); break;
    NCASE(RefType)   t = make_ref_type(b, n); break;
    NCASE(ArrayType) t = make_array_type(b, n); break;
    NCASE(IdType)    return _get_type(b, n->target);

    // not implemented
    NCASE(TypeType)   panic("TODO %s", nodename(n));
    NCASE(TupleType)  panic("TODO %s", nodename(n));
    NCASE(StructType) panic("TODO %s", nodename(n));

    NDEFAULTCASE
      assertf(0,"invalid node kind: %s n@%p->kind = %u",
        NodeKindName(np->kind), np, np->kind);
  }}

  set_interned_type(b, np, t);
  np->irval = t;
  return t;
}


// end type build functions
//———————————————————————————————————————————————————————————————————————————————————————
// begin value build functions


static Val _build_expr(B* b, Expr* n, const char* vname);
#ifdef DEBUG_BUILD_EXPR
  static Val _build_expr_debug(B* b, Expr* n, const char* vname);
  #define build_expr(b, n, vname) _build_expr_debug((b),as_Expr(n),(vname))
#else
  #define build_expr(b, n, vname) _build_expr((b),as_Expr(n),(vname))
#endif

#define build_rval(b, n, vname) _build_rval((b),as_Expr(n),(vname))
static Val _build_rval(B* b, Expr* n, const char* vname) {
  if (b->flags & BFL_RVAL)
    MUSTTAIL return build_expr(b, n, vname);
  BFlags flags = b->flags;
  b->flags |= BFL_RVAL;
  Val val = build_expr(b, n, vname);
  b->flags = flags;
  return val;
}

#define build_lval(b, n, vname) _build_lval((b),as_Expr(n),(vname))
static Val _build_lval(B* b, Expr* n, const char* vname) {
  if ((b->flags & BFL_RVAL) == 0)
    MUSTTAIL return build_expr(b, n, vname);
  BFlags flags = b->flags;
  b->flags &= ~BFL_RVAL;
  Val val = build_expr(b, n, vname);
  b->flags = flags;
  return val;
}


// inline static Val build_expr_noload(B* b, Expr* n, const char* vname) {
//   bool noload = b->noload; b->noload = true;
//   Val v = build_expr(b, n, vname);
//   b->noload = noload;
//   return v;
// }

// inline static Val build_expr_doload(B* b, Expr* n, const char* vname) {
//   bool noload = b->noload; b->noload = false;
//   Val v = build_expr(b, n, vname);
//   b->noload = noload;
//   return v;
// }


static Val build_default_value(B* b, Type* t) {
  Typ ty = get_type(b, t);
  return LLVMConstNull(ty);
}


static Val build_store(B* b, Val dst, Val val) {
  #if DEBUG
  assertnotnull(dst);
  assertnotnull(val);
  Typ dst_type = LLVMTypeOf(dst);
  assertf(LLVMGetTypeKind(dst_type) == LLVMPointerTypeKind,
    "dst_type %s is not a pointer type",
    fmttyp(LLVMGetElementType(dst_type)) );
  if (LLVMTypeOf(val) != LLVMGetElementType(dst_type)) {
    panic("store destination type %s != source type %s",
      fmttyp(LLVMGetElementType(dst_type)), fmttyp(LLVMTypeOf(val)));
  }
  #endif
  return LLVMBuildStore(b->builder, val, dst);
}


static Val build_load(B* b, Typ elem_ty, Val src, const char* vname) {
  #if DEBUG
  assertnotnull(elem_ty);
  assertnotnull(src);
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
    for (u32 i = 0; i < n->params.len; i++) {
      ParamNode* param = as_ParamNode(n->params.v[i]);
      Val p = LLVMGetParam(fn, i);
      LLVMSetValueName2(p, param->name, symlen(param->name));
    }
  }
  return fn;
}


static void scrub_shared_irvals_visitor(ASTVisitor* v, const ASTParent* parent, Node* n) {
  // dlog("visit %s", nodename(n));
  if (n->irval)
    n->irval = NULL;
  ASTVisitChildrenAndType(v, parent, n);
}


static void scrub_shared_irvals(B* b, Node* parentn, Node* n) {
  if (b->astVisitor.ctx == NULL) {
    static const ASTVisitorFuns vf = {
      .Node = &scrub_shared_irvals_visitor,
    };
    ASTVisitorInit(&b->astVisitor, &vf, b);
  }
  ASTVisitRoot(&b->astVisitor, parentn, n, /*visit_type*/true);
}


static Val build_fun(B* b, FunNode* n, const char* vname) {
  if (n->irval)
    return n->irval;

  vname = n->name ? n->name : vname;
  bool is_main = strcmp(vname, "main") == 0;

  // add type id to function name
  char fname_buf[64];
  Str fname = str_make(fname_buf, sizeof(fname_buf));
  str_appendcstr(&fname, vname);
  if (!is_main) {
    // TODO: use type ID for parameters only?
    Sym tid = b_typeid(b->build, n->type);
    str_append(&fname, tid, symlen(tid));
  }

  // build function prototype
  Val fn = build_funproto(b, n, str_cstr(&fname));
  n->irval = fn;

  if (!n->body) { // external
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
  }

  if (/*vname[0] == '_'*/ !is_main) {
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

  // If the function is an instance of a template, we need to scrub any previously
  // stored irval values from its shared nodes (nodes with NF_Shared).
  // i.e. template is instanced at least twice.
  if (n->instance_of && !pset_add(&b->templateInstances, n->instance_of))
    scrub_shared_irvals(b, (Node*)n, (Node*)n->body);

  // create local storage for mutable parameters
  for (u32 i = 0; i < n->params.len; i++) {
    ParamNode* pn = as_ParamNode(n->params.v[i]);
    Val pv = LLVMGetParam(fn, i);
    Typ pt = LLVMTypeOf(pv);
    if (NodeIsConst(pn) /*&& !arg_type_needs_alloca(pt)*/) {
      // immutable primitive value does not need a local alloca
      pn->irval = pv;
      continue;
    }
    // give the local a helpful name
    pn->irval = LLVMBuildAlloca(b->builder, pt, vnamef(b, "arg_%s", pn->name));
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
  dlog("TODO build_global_var");
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
      NCASE(Template)
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
      safenotnull( path_dir(&dir, file->name, strlen(file->name)) );
      LLVMSetSourceFileName(b->mod, dir.v, dir.len);
      str_free(&dir);
    }
    build_file(b, file);
  }
}


static Val build_nil(B* b, NilNode* n, const char* vname) {
  if (b->flags & BFL_RVAL)
    dlog("TODO build rval nil (depends on outer type)");
  return b->v_intptr_0;
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


static Val build_cslice(B* b, Val array_ptr) {
  // (cslice<T>){ GEP(array_ptr), len(array_ptr) }
  Typ array_ty = LLVMTypeOf(array_ptr); // i.e. [N x T]*
  assert_llvm_type_isptrkind(array_ty, LLVMArrayTypeKind);
  assert(LLVMIsConstant(array_ptr));
  Typ elem_ty = LLVMGetElementType(array_ty); // i.e. [N x T]
  Val indices[] = { b->v_i32_0, b->v_i32_0 };
  Val constfields[] = {
    LLVMConstGEP2(elem_ty, array_ptr, indices, countof(indices)),
    LLVMConstInt(b->t_int, CoLLVMArrayTypeLength(elem_ty), /*signext*/false),
  };
  bool packed = false;
  return LLVMConstStructInContext(b->ctx, constfields, countof(constfields), packed);
}


static Val build_strlit(B* b, StrLitNode* n, const char* vname) {
  if (n->irval)
    return n->irval;
  assert(NodeIsConst(n));
  if (!vname[0])
    vname = "str";
  Val gv = CoLLVMBuildGlobalString(b->builder, n->p, n->len, vname); // [N x i8]*
  n->irval = build_cslice(b, gv); // [N x i8]* => { u8* p=GEP, len=N }
  return n->irval;
}


static Val build_id(B* b, IdNode* n, const char* vname) {
  assertnotnull(n->target); // must be resolved
  if (!n->irval)
    n->irval = build_expr(b, as_Expr(n->target), n->name);
  return n->irval;
}


static Val build_binop(B* b, BinOpNode* n, const char* vname) {
  BasicTypeNode* tn = as_BasicTypeNode(n->type);
  assert(tn->typecode < TC_NUM_END);

  Val left = build_rval(b, n->left, "");
  Val right = build_rval(b, n->right, "");
  assertf(LLVMTypeOf(left) == LLVMTypeOf(right),
    "binop args: %s != %s", fmttyp(LLVMTypeOf(left)), fmttyp(LLVMTypeOf(right)));

  u32 op = 0;
  bool isfloat = false;
  assert(n->op < T_PRIM_OPS_END);
  switch (tn->typecode) {
    case TC_bool: switch (n->op) {
      case TEq:  op = LLVMIntEQ; break; // ==
      case TNEq: op = LLVMIntNE; break; // !=
    } break;
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
    default: break;
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
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_postfixop(B* b, PostfixOpNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}


static Val build_return(B* b, ReturnNode* n, const char* vname) {
  Val retval = build_rval(b, n->expr, "");
  return LLVMBuildRet(b->builder, retval);
}


static Val build_const(B* b, ConstNode* n, const char* vname) {
  if (n->irval)
    return n->irval;

  // don't build unused constants
  if (NodeIsUnused(n) && b->build->opt > OptNone)
    return b->v_int_0;

  // TODO: if anything takes the address of a const, it needs to be stored in memory
  // somewhere. As a global if the referring reference survives the scope or on stack.

  n->irval = build_rval(b, n->value, n->name);
  return n->irval;
}


static Val build_templateparam(B* b, TemplateParamNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}


static Val build_var(B* b, VarNode* n, const char* vname) {
  if (n->irval) {
    if (!NodeIsConst(n) && (b->flags & BFL_RVAL)) {
      Typ ty = get_type(b, n->type);
      return build_load(b, ty, n->irval, n->name);
    }
    return n->irval;
  }

  // don't build unused variables
  if (NodeIsUnused(n) && b->build->opt > OptNone)
    return b->v_int_0;

  // build initializer
  Val init;
  if (n->init) {
    init = build_rval(b, n->init, n->name);
  } else {
    init = build_default_value(b, n->type);
  }

  // immutable var is represented by its initializer (no stack space)
  if (NodeIsConst(n)) {
    n->irval = init;
    return n->irval;
  }

  // allocate stack space
  Typ ty = get_type(b, n->type);
  n->irval = LLVMBuildAlloca(b->builder, ty, vnamef(b, "var_%s", n->name));

  // store initial value
  build_store(b, n->irval, init);

  return init;
}


static Val build_param(B* b, ParamNode* n, const char* vname) {
  Val paramval = assertnotnull(n->irval); // note: irval set by build_fun
  if (NodeIsConst(n) || (b->flags & BFL_RVAL) == 0)
    return paramval;
  Typ ty = get_type(b, n->type);
  return build_load(b, ty, paramval, vname);
}


static Val build_assign_local(B* b, AssignNode* n, const char* vname) {
  LocalNode* dstn = as_LocalNode(n->dst);
  Val dst = build_lval(b, n->dst, dstn->name);
  Val val = build_rval(b, n->val, "");
  build_store(b, dst, val);
  return val;
}


static Val build_assign_tuple(B* b, AssignNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_assign_index(B* b, AssignNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_assign_selector(B* b, AssignNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
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
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_array(B* b, ArrayNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}


static Val build_block(B* b, BlockNode* n, const char* vname) {
  assertf(n->a.len > 0, "empty block");
  u32 i = 0;
  for (; i < n->a.len - 1; i++)
     build_expr(b, n->a.v[i], "");
  // last expr of block is its value
  return build_expr(b, n->a.v[i], "");
}


static Val build_type_call(B* b, CallNode* n, const char* vname) {
  //Type* tn = (Type*)n->receiver;
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}


static Val build_call_builtin(B* b, CallNode* n, Expr* recv, const char* vname) {
  // to_rawptr is currently the only builtin
  assert(recv == (Expr*)kBuiltin_to_rawptr);

  // to_rawptr prototype:
  //   unsafe fun to_rawptr<T>(_ &T) rawptr
  assert(n->args.len == 1);
  Val ref = build_expr(b, n->args.v[0], "");

  // note: LLVMBuildExtractValue uses LLVMConstExtractValue if ref is constant
  return LLVMBuildExtractValue(b->builder, ref, 0, vname);
}


static Val build_fun_call(B* b, CallNode* n, const char* vname) {
  // remove any constant-expression name indirection.
  // e.g.  "fun foo(); bar = foo; bar()"  =>  "fun foo(); ...; foo()"
  Expr* recv = NodeEval(b->build, as_Expr(n->receiver), NULL, 0);

  // is this a builtin?
  if (recv == (Expr*)kBuiltin_to_rawptr)
    return build_call_builtin(b, n, recv, vname);

  // build callee
  Val fn = build_expr(b, recv, "callee");

  // build arguments
  Val argsv[16];
  auto args = array_make(Array(Val), argsv, sizeof(argsv));
  UNUSED bool ok = true;
  for (u32 i = 0; i < n->args.len; i++) {
    const char* arg_vname = vnamef(b, "callarg_%u", i);
    Val arg = build_rval(b, n->args.v[i], arg_vname);
    ok &= array_push(&args, arg);
  }
  assert(ok);

  // build call
  assert_is_FunTypeNode(recv->type);
  Typ fntype = get_type(b, recv->type);
  Val v = LLVMBuildCall2(b->builder, fntype, fn, args.v, args.len, "");
  array_free(&args);
  return v;
}


static Val build_call(B* b, CallNode* n, const char* vname) {
  Expr* recv = (Expr*)n->receiver;
  if (is_Expr(n->receiver) && is_FunTypeNode(recv->type))
    return build_fun_call(b, n, vname);

  assertf(
    (is_Expr(n->receiver) && is_TypeTypeNode(recv->type)) ||
    is_TypeTypeNode(n->receiver),
    "unexpected n->receiver: %s", nodename(n->receiver));
  return build_type_call(b, n, vname);
}


static Val build_template(B* b, TemplateNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}


static Val build_typecast(B* b, TypeCastNode* n, const char* vname) {
  if (!is_BasicTypeNode(n->type)) {
    dlog("TODO non-basic type cast %s  %s:%d", __FUNCTION__, __FILE__, __LINE__);
    return b->v_int_0;
  }
  BasicTypeNode* t = (BasicTypeNode*)n->type;
  LLVMBool isSigned = (t->tflags & TF_Signed);
  Typ dst_type = get_basic_type(b, t);
  Val src_val = build_expr(b, n->expr, "");
  return LLVMBuildIntCast2(b->builder, src_val, dst_type, isSigned, vname);
}


static Val build_ref(B* b, RefNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_namedarg(B* b, NamedArgNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_selector(B* b, SelectorNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_index(B* b, IndexNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_slice(B* b, SliceNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}

static Val build_if(B* b, IfNode* n, const char* vname) {
  dlog("TODO %s  %s:%d", __FUNCTION__, __FILE__, __LINE__); return b->v_int_0;
}


static Val _build_expr(B* b, Expr* np, const char* vname) {
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
  NCASE(Template)      return build_template(b, n, vname);
  NCASE(Call)       return build_call(b, n, vname);
  NCASE(TypeCast)   return build_typecast(b, n, vname);
  NCASE(Const)      return build_const(b, n, vname);
  NCASE(Var)        return build_var(b, n, vname);
  NCASE(Param)      return build_param(b, n, vname);
  NCASE(TemplateParam) return build_templateparam(b, n, vname);
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


#ifdef DEBUG_BUILD_EXPR
static Val _build_expr_debug(B* b, Expr* np, const char* vname) {
  dlog2("○ %s <%s> %s ...", nodename(np), FMTNODE(np->type,0), FMTNODE(np,1));

  b->debug_depth++;
  Val v = _build_expr(b, np, vname);
  b->debug_depth--;

  const char* nname = nodename(np);
  const char* in_typstr = FMTNODE(np->type,0);
  const char* in_valstr = FMTNODE(np,1);
  const char* out_typstr = v ? fmttyp(LLVMTypeOf(v)) : "(NULL)";
  const char* out_valstr = v ? fmtval(v) : "\e[31m(NULL)";

  const char* typcolor = "\e[36m";
  const char* valcolor = "\e[1m";

  if (strlen(out_typstr) < 25 && strlen(out_valstr) < 25) {
    dlog2("● %s <%s> %s ⟶ %s%s\e[0m : %s%s\e[0m",
      nname, in_typstr, in_valstr, typcolor, out_typstr, valcolor, out_valstr);
  } else {
    if (strlen(out_typstr) < 35) {
      dlog2("● %s <%s> %s ⟶ %s%s\e[0m :", nname, in_typstr, in_valstr, typcolor, out_typstr);
    } else {
      dlog2("● %s <%s> %s ⟶", nname, in_typstr, in_valstr);
      dlog2("  %s%s\e[0m :", typcolor, out_typstr);
    }
    dlog2("  %s%s\e[0m", valcolor, out_valstr);
  }

  return v;
}
#endif


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
      log("\e[33;1m——————————————————————— LLVMVerifyModule ———————————————————————\e[0m");
      int len = strlen(errmsg);
      log("%.*s", (int)(len && errmsg[len-1] == '\n' ? len - 1 : len), errmsg);
      LLVMDisposeMessage(errmsg);
      log("————————————————————————————————————————————————————————————————");
      LLVMDumpModule(b->mod);
      log("————————————————————————————————————————————————————————————————");
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
