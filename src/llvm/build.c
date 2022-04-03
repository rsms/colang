#include "llvmimpl.h"
#include "../parse/parse.h"

// DEBUG_BUILD_EXPR: define to dlog trace build_expr
#define DEBUG_BUILD_EXPR


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


// make the code more readable by using short name aliases
typedef LLVMValueRef  Value;


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
  Value      varalloc;      // memory preallocated for a var's init
  SymMap     internedTypes; // AST types, keyed by typeid
  PMap       defaultInits;  // constant initializers (LLVMTypeRef => Value)

  // memory generation check (specific to current function)
  LLVMBasicBlockRef mgen_failb;
  Value             mgen_alloca; // alloca for failed ref (type "REF" { i32*, i32 })

  // type constants
  LLVMTypeRef t_void;
  LLVMTypeRef t_bool;
  LLVMTypeRef t_i8;
  LLVMTypeRef t_i16;
  LLVMTypeRef t_i32;
  LLVMTypeRef t_i64;
  // LLVMTypeRef t_i128;
  LLVMTypeRef t_f32;
  LLVMTypeRef t_f64;
  // LLVMTypeRef t_f128;
  LLVMTypeRef t_int;

  LLVMTypeRef t_i8ptr;  // i8*
  LLVMTypeRef t_i32ptr; // i32*

  // ref struct types
  LLVMTypeRef t_ref; // "mut&T", "&T"

  // value constants
  Value v_i32_0; // i32 0

  // metadata values
  Value md_br_likely;
  Value md_br_unlikely;

  // metadata "kind" identifiers
  u32 md_kind_prof; // "prof"

} B;


// development debugging support
#ifdef DEBUG_BUILD_EXPR
static char kSpaces[256];
#endif


static error builder_init(B* b, BuildCtx* build, LLVMModuleRef mod) {
  #ifdef DEBUG_BUILD_EXPR
  memset(kSpaces, ' ', sizeof(kSpaces));
  #endif

  LLVMContextRef ctx = LLVMGetModuleContext(mod);

  *b = (B){
    .build = build,
    .ctx = ctx,
    .mod = mod,
    .builder = LLVMCreateBuilderInContext(ctx),
    .prettyIR = true,

    // FPM: Apply per-function optimizations. Set to NULL to disable.
    // Really only useful for JIT; for assembly to asm, obj or bc we apply module-wide opt.
    // .FPM = LLVMCreateFunctionPassManagerForModule(mod),
    .FPM = NULL,

    // constants
    // note: no disposal needed of built-in types
    .t_void = LLVMVoidTypeInContext(ctx),
    .t_bool = LLVMInt1TypeInContext(ctx),
    .t_i8   = LLVMInt8TypeInContext(ctx),
    .t_i16  = LLVMInt16TypeInContext(ctx),
    .t_i32  = LLVMInt32TypeInContext(ctx),
    .t_i64  = LLVMInt64TypeInContext(ctx),
    // .t_i128 = LLVMInt128TypeInContext(ctx),
    .t_f32  = LLVMFloatTypeInContext(ctx),
    .t_f64  = LLVMDoubleTypeInContext(ctx),
    // .t_f128 = LLVMFP128TypeInContext(ctx),

    // metadata "kind" identifiers
    .md_kind_prof = LLVMGetMDKindIDInContext(ctx, "prof", 4),
  };

  // initialize common types
  b->t_int = (build->sint_type == TC_i32) ? b->t_i32 : b->t_i64;
  b->t_i8ptr = LLVMPointerType(b->t_i8, 0);
  b->t_i32ptr = LLVMPointerType(b->t_i32, 0);
  b->v_i32_0 = LLVMConstInt(b->t_i32, 0, /*signext*/false);

  // initialize containers
  if (symmap_init(&b->internedTypes, build->mem, 16) == NULL) {
    LLVMDisposeBuilder(b->builder);
    return err_nomem;
  }
  if (pmap_init(&b->defaultInits, build->mem, 16, MAPLF_2) == NULL) {
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


error llvm_build_module(BuildCtx* build, LLVMModuleRef mod) {
  // initialize builder
  B b_; B* b = &b_;
  error err = builder_init(b, build, mod);
  if (err)
    return err;

  // // build package parts
  // for (u32 i = 0; i < pkgnode->cunit.a.len; i++) {
  //   auto cn = (Node*)pkgnode->cunit.a.v[i];
  //   build_file(b, cn);
  // }

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
