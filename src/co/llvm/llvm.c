#include "../common.h"
#include "../parse/parse.h"
#include "../util/rtimer.h"
#include "../util/ptrmap.h"
#include "../util/stk_array.h"
#include "llvm.h"

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/OrcEE.h>

// DEBUG_BUILD_EXPR: define to dlog trace build_expr
//#define DEBUG_BUILD_EXPR

// rtimer helpers
#define ENABLE_RTIMER_LOGGING
#ifdef ENABLE_RTIMER_LOGGING
  #define RTIMER_INIT          RTimer rtimer_ = {0}
  #define RTIMER_START()       rtimer_start(&rtimer_)
  #define RTIMER_LOG(fmt, ...) rtimer_log(&rtimer_, fmt, ##__VA_ARGS__)
#else
  #define RTIMER_INIT          do{}while(0)
  #define RTIMER_START()       do{}while(0)
  #define RTIMER_LOG(fmt, ...) do{}while(0)
#endif

#ifdef DEBUG_BUILD_EXPR
  #define dlog_mod(b, fmt, ...) \
    dlog("%.*s" fmt, (b)->log_indent * 2, kSpaces, ##__VA_ARGS__)
#else
  #define dlog_mod(b, fmt, ...) ((void)0)
#endif

#define assert_llvm_type_iskind(llvmtype, expect_typekind) \
  asserteq_debug(LLVMGetTypeKind(llvmtype), (expect_typekind))

#define assert_llvm_type_isptrkind(llvmtype, expect_typekind) do { \
  asserteq_debug(LLVMGetTypeKind(llvmtype), LLVMPointerTypeKind); \
  asserteq_debug(LLVMGetTypeKind(LLVMGetElementType(llvmtype)), (expect_typekind)); \
} while(0)

#define assert_llvm_type_isptr(llvmtype) \
  asserteq_debug(LLVMGetTypeKind(llvmtype), LLVMPointerTypeKind)

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
  Build*          build; // Co build (package, mem allocator, etc)
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
  PtrMap     defaultInits;  // constant initializers (LLVMTypeRef => Value)

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
static char kSpaces[256] = {0};
#endif


__attribute__((used))
static const char* fmtvalue(LLVMValueRef v) {
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
  LLVMTypeRef ty = LLVMTypeOf(v);
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


__attribute__((used))
static const char* fmttype(LLVMTypeRef ty) {
  if (!ty)
    return "(null)";
  static char* p[5] = {NULL};
  static u32 index = 0;
  u32 i = index++;
  if (index == countof(p))
    index = 0;
  if (p[i])
    LLVMDisposeMessage(p[i]);
  p[i] = LLVMPrintTypeToString(ty);
  return p[i];
}


// dnamef formats IR value names
#if defined(DEBUG) && defined(DEBUG_BUILD_EXPR)
  ATTR_FORMAT(printf, 2, 3)
  static const char* dnamef(B* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    b->dname_buf[0] = 0;
    vsnprintf(b->dname_buf, sizeof(b->dname_buf), fmt, ap);
    va_end(ap);
    return b->dname_buf;
  }
#else
  #define dnamef(b, fmt, ...) ""
#endif


static Mutability set_mut(B* b, Mutability next) {
  Mutability prev = b->mut;
  b->mut = next;
  return prev;
}


static Mutability set_mut_based_on_node_const(B* b, Node* n) {
  return set_mut(b, (Mutability)!NodeIsConst(n));
}


static LLVMTypeRef refstruct_type(B*);


// build_store is really just LLVMBuildStore with assertions enabled in DEBUG builds
static Value build_store(B* b, Value v, Value ptr) {

  #if DEBUG
  LLVMTypeRef ptrty = LLVMTypeOf(ptr);
  asserteq(LLVMGetTypeKind(ptrty), LLVMPointerTypeKind);
  if (LLVMTypeOf(v) != LLVMGetElementType(ptrty)) {
    panic("store destination type %s != source type %s",
      fmttype(LLVMGetElementType(ptrty)), fmttype(LLVMTypeOf(v)));
  }
  #endif

  return LLVMBuildStore(b->builder, v, ptr);
}


// build_load is really just LLVMBuildLoad2 with assertions enabled in DEBUG builds
static Value build_load(B* b, LLVMTypeRef elem_ty, Value ptr, const char* vname) {

  #if DEBUG
  LLVMTypeRef ptrty = LLVMTypeOf(ptr);
  asserteq(LLVMGetTypeKind(ptrty), LLVMPointerTypeKind);
  LLVMTypeRef ptr_elem_ty = LLVMGetElementType(ptrty);
  if (elem_ty != ptr_elem_ty) {
    panic("load destination type %s != source type %s",
      fmttype(elem_ty), fmttype(ptr_elem_ty));
  }
  #endif

  return LLVMBuildLoad2(b->builder, elem_ty, ptr, vname);
}


// static Value build_copy(B* b, Value dstptr, Value srcptr, Value sizeval) {
//   asserteq_debug(LLVMGetTypeKind(LLVMTypeOf(srcptr)), LLVMPointerTypeKind);
//   asserteq_debug(LLVMTypeOf(dstptr), LLVMTypeOf(srcptr));
//   u32 dst_align = LLVMGetAlignment(dstptr);
//   u32 src_align = LLVMGetAlignment(srcptr);
//   return LLVMBuildMemCpy(b->builder, dstptr, dst_align, srcptr, src_align, sizeval);
// }


static LLVMTypeRef seq_elem_type(LLVMTypeRef seqty, u32 index) {
  switch (LLVMGetTypeKind(seqty)) {
    case LLVMStructTypeKind:
      return LLVMStructGetTypeAtIndex(seqty, index);
    case LLVMArrayTypeKind:
    case LLVMVectorTypeKind:
    case LLVMScalableVectorTypeKind:
      return LLVMGetElementType(seqty);
    default:
      panic("invalid seq type %s", fmttype(seqty));
  }
}


//// build_gep_load loads the value of field at index from sequence at memory location ptr
//static Value build_gep_load(B* b, Value v, u32 index, const char* vname) {
//  LLVMTypeRef vty = LLVMTypeOf(notnull(v));
//  LLVMTypeKind tykind = LLVMGetTypeKind(vty);
//
//  switch (tykind) {
//    case LLVMArrayTypeKind:
//      return LLVMGetElementAsConstant(v, index);
//    case LLVMPointerTypeKind:
//      break;
//    default:
//      panic("unexpected value type %s", fmttype(vty));
//  }
//
//  // v is a pointer: GEP
//  LLVMTypeRef seqty = LLVMGetElementType(vty);
//
//  #if DEBUG
//  LLVMTypeKind seqty_kind = LLVMGetTypeKind(seqty);
//  assert_debug(seqty_kind == LLVMStructTypeKind || seqty_kind == LLVMArrayTypeKind);
//  assert_debug(index <
//    (seqty_kind == LLVMStructTypeKind ? LLVMCountStructElementTypes(seqty) :
//     LLVMGetArrayLength(seqty)) );
//  #endif
//
//  LLVMValueRef indexv[2] = {
//    b->v_i32_0,
//    LLVMConstInt(b->t_i32, index, /*signext*/false),
//  };
//
//  // "inbounds" — the result value of the GEP is undefined if the address is outside
//  // the actual underlying allocated object and not the address one-past-the-end.
//  Value elemptr = LLVMBuildInBoundsGEP2(b->builder, seqty, v, indexv, 2, vname);
//
//  LLVMTypeRef elem_ty = seq_elem_type(seqty, index);
//  return build_load(b, elem_ty, elemptr, vname);
//}


// // set_br_likely marks a branch instruction as being likely to take the true branch
// static void set_br_likely(B* b, Value brinstr) {
//   // see https://llvm.org/docs/BranchWeightMetadata.html
//   if (b->md_br_likely == NULL) {
//     LLVMMetadataRef branch_weights = LLVMMDStringInContext2(b->ctx, "branch_weights", 14);
//     LLVMMetadataRef weight1 = LLVMValueAsMetadata(LLVMConstInt(b->t_i32, 100, 0));
//     LLVMMetadataRef weight2 = LLVMValueAsMetadata(b->v_i32_0);
//     LLVMMetadataRef mds[] = {branch_weights, weight1, weight2};
//     LLVMMetadataRef metadata = LLVMMDNodeInContext2(b->ctx, mds, 3);
//     b->md_br_likely = LLVMMetadataAsValue(b->ctx, metadata);
//   }
//   LLVMSetMetadata(brinstr, b->md_kind_prof, b->md_br_likely);
// }


// set_br_unlikely marks a branch instruction as being unlikely to take the true branch
static void set_br_unlikely(B* b, Value brinstr) {
  // see https://llvm.org/docs/BranchWeightMetadata.html
  if (b->md_br_unlikely == NULL) {
    LLVMMetadataRef branch_weights = LLVMMDStringInContext2(b->ctx, "branch_weights", 14);
    LLVMMetadataRef weight1 = LLVMValueAsMetadata(b->v_i32_0);
    LLVMMetadataRef weight2 = LLVMValueAsMetadata(LLVMConstInt(b->t_i32, 100, 0));
    LLVMMetadataRef mds[] = {branch_weights, weight1, weight2};
    LLVMMetadataRef metadata = LLVMMDNodeInContext2(b->ctx, mds, 3);
    b->md_br_unlikely = LLVMMetadataAsValue(b->ctx, metadata);
  }
  LLVMSetMetadata(brinstr, b->md_kind_prof, b->md_br_unlikely);
}


static LLVMTypeRef get_struct_type(B* b, Type* tn);
static LLVMTypeRef get_tuple_type(B* b, Type* tn);
static LLVMTypeRef get_array_type(B* b, Type* tn);


static LLVMTypeRef get_type(B* b, Type* nullable n) {
  if (!n)
    return b->t_void;

  switch (n->kind) {

  case NBasicType: {
    switch (n->t.basic.typeCode) {
      case TypeCode_bool:
        return b->t_bool;
      case TypeCode_i8:
      case TypeCode_u8:
        return b->t_i8;
      case TypeCode_i16:
      case TypeCode_u16:
        return b->t_i16;
      case TypeCode_i32:
      case TypeCode_u32:
        return b->t_i32;
      case TypeCode_i64:
      case TypeCode_u64:
        return b->t_i64;
      case TypeCode_f32:
        return b->t_f32;
      case TypeCode_f64:
        return b->t_f64;
      case TypeCode_ideal:
      case TypeCode_int:
      case TypeCode_uint:
        return b->t_int;
      case TypeCode_nil:
        return b->t_void;
      default: {
        panic("TODO basic type %s", n->t.basic.name);
        break;
      }
    }
    break;
  }

  case NStructType:
    R_MUSTTAIL return get_struct_type(b, n);

  case NTupleType:
    R_MUSTTAIL return get_tuple_type(b, n);

  case NArrayType:
    R_MUSTTAIL return get_array_type(b, n);

  case NRefType:
    if (b->build->safe)
      return refstruct_type(b);
    return LLVMPointerType(get_type(b, n->t.ref), 0);

  default:
    panic("invalid node kind %s", NodeKindName(n->kind));
    return NULL;
  }
}


static bool value_is_ret(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMRet;
}

__attribute__((used))
static bool value_is_call(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMCall;
}

inline static LLVMBasicBlockRef get_current_block(B* b) {
  return LLVMGetInsertBlock(b->builder);
}

inline static Value get_current_fun(B* b) {
  return LLVMGetBasicBlockParent(get_current_block(b));
}


static Value build_expr(B* b, Node* n, const char* vname);


// build_expr_noload calls build_expr with b->noload set to true, ignoring result value
inline static Value nullable build_expr_noload(B* b, Node* n, const char* vname) {
  bool noload = b->noload; // save
  b->noload = true;
  build_expr(b, n, vname);
  b->noload = noload; // restore
  return n->irval;
}

inline static Value build_expr_mustload(B* b, Node* n, const char* vname) {
  bool noload = b->noload; // save
  b->noload = false;
  Value v = build_expr(b, n, vname);
  b->noload = noload; // restore
  return v;
}


static Value build_default_value(B* b, Type* tn) {
  LLVMTypeRef ty = get_type(b, tn);
  return LLVMConstNull(ty);
}


inline static Sym ntypeid(B* b, Type* tn) {
  return tn->t.id ? tn->t.id : (tn->t.id = GetTypeID(b->build, tn));
}


static LLVMTypeRef nullable get_intern_type(B* b, Type* tn) {
  assert_debug(NodeIsType(tn));
  Sym tid = ntypeid(b, tn);
  return (LLVMTypeRef)SymMapGet(&b->internedTypes, tid);
}

static void add_intern_type(B* b, Type* tn, LLVMTypeRef tr) {
  assert_debug(NodeIsType(tn));
  assertnull_debug(get_intern_type(b, tn)); // must not be defined
  Sym tid = ntypeid(b, tn);
  SymMapSet(&b->internedTypes, tid, tr);
}


inline static Value nullable get_default_init(B* b, LLVMTypeRef ty) {
  return (Value)PtrMapGet(&b->defaultInits, ty);
}

inline static void add_default_init(B* b, LLVMTypeRef ty, Value v) {
  assertnull_debug(get_default_init(b, ty));
  PtrMapSet(&b->defaultInits, ty, v);
}


static LLVMTypeRef build_funtype(B* b, Node* nullable params, Node* nullable result) {
  LLVMTypeRef returnType = get_type(b, result);

  u32 paramsc = 0;
  STK_ARRAY_DEFINE(paramsv, LLVMTypeRef, 16);

  if (params) {
    Type* paramst = params->type;
    asserteq(paramst->kind, NTupleType);
    paramsc = paramst->t.tuple.a.len;
    STK_ARRAY_INIT(paramsv, b->build->mem, paramsc);
    for (u32 i = 0; i < paramsc; i++) {
      paramsv[i] = get_type(b, paramst->t.tuple.a.v[i]);
    }
  }

  auto ft = LLVMFunctionType(returnType, paramsv, paramsc, /*isVarArg*/false);
  STK_ARRAY_DISPOSE(paramsv);
  return ft;
}


static LLVMTypeRef get_funtype(B* b, Type* tn) {
  LLVMTypeRef tr = get_intern_type(b, tn);
  if (!tr) {
    tr = build_funtype(b, tn->t.fun.params, tn->t.fun.result);
    add_intern_type(b, tn, tr);
  }
  return tr;
}


static Value build_funproto(B* b, Node* n, const char* name) {
  asserteq(n->kind, NFun);
  LLVMTypeRef ft = get_funtype(b, n->type);
  // auto f = &n->fun;
  Value fn = LLVMAddFunction(b->mod, name, ft);

  // set argument names (for debugging)
  if (b->prettyIR && n->fun.params) {
    auto a = n->fun.params->array.a;
    for (u32 i = 0; i < a.len; i++) {
      auto param = (Node*)a.v[i];
      // param->kind==NArg
      Value p = LLVMGetParam(fn, i);
      LLVMSetValueName2(p, param->var.name, symlen(param->var.name));
    }
  }

  // linkage & visibility
  if (n->fun.name && strcmp(name, "main") != 0) {
    // TODO: only set for globals
    // Note on LLVMSetVisibility: visibility is different.
    // See https://llvm.org/docs/LangRef.html#visibility-styles
    // LLVMPrivateLinkage is like "static" in C but omit from symbol table
    LLVMSetLinkage(fn, LLVMPrivateLinkage);
    // LLVMSetLinkage(fn, LLVMInternalLinkage); // like "static" in C
  }

  return fn;
}


// static bool arg_type_needs_alloca(LLVMTypeRef ty) {
//   switch (LLVMGetTypeKind(ty)) {
//     // types of arguments which must always be placed in a local alloca
//     case LLVMFunctionTypeKind:
//     case LLVMStructTypeKind:
//     case LLVMArrayTypeKind:
//     case LLVMVectorTypeKind:
//     case LLVMScalableVectorTypeKind:
//       return true;

//     case LLVMVoidTypeKind:
//     case LLVMHalfTypeKind:
//     case LLVMFloatTypeKind:
//     case LLVMDoubleTypeKind:
//     case LLVMX86_FP80TypeKind:
//     case LLVMFP128TypeKind:
//     case LLVMPPC_FP128TypeKind:
//     case LLVMLabelTypeKind:
//     case LLVMIntegerTypeKind:
//     case LLVMPointerTypeKind:
//     case LLVMMetadataTypeKind:
//     case LLVMX86_MMXTypeKind:
//     case LLVMTokenTypeKind:
//     case LLVMBFloatTypeKind:
//     case LLVMX86_AMXTypeKind:
//       return false;
//   }
// }


static Value build_fun(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NFun);
  notnull(n->type);
  asserteq_debug(n->type->kind, NFunType);

  if (n->irval)
    return (Value)n->irval;

  auto f = &n->fun;

  LLVMValueRef fn; {
    const char* name = f->name;
    if (name == NULL || strcmp(name, "main") != 0)
      name = str_fmt("%s%s", name, n->type->t.id);
    fn = build_funproto(b, n, name);
    if (name != f->name)
      str_free((Str)name);
  }

  n->irval = fn;

  if (!n->fun.body) { // external
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
  }

  b->fnest++;

  // save any current builder position
  LLVMBasicBlockRef prevb = get_current_block(b);
  LLVMBasicBlockRef mgen_failb = b->mgen_failb;
  Value mgen_alloca = b->mgen_alloca;
  b->mgen_failb = NULL;
  b->mgen_alloca = NULL;

  // create a new basic block to start insertion into
  LLVMBasicBlockRef entryb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, entryb);

  // process params eagerly
  if (n->fun.params) {
    auto a = n->fun.params->array.a;
    for (u32 i = 0; i < a.len; i++) {
      auto pn = (Node*)a.v[i];
      asserteq_debug(pn->kind, NVar);
      assert_debug(NodeIsParam(pn));
      Value pv = LLVMGetParam(fn, i);
      LLVMTypeRef ty = LLVMTypeOf(pv);
      if (NodeIsConst(pn) /*&& !arg_type_needs_alloca(ty)*/) {
        // immutable pimitive value does not need a local alloca
        pn->irval = pv;
      } else { // mutable
        const char* name = pn->var.name;
        #if DEBUG
        char namebuf[128];
        snprintf(namebuf, sizeof(namebuf), "arg_%s", name);
        name = namebuf;
        #endif

        pn->irval = LLVMBuildAlloca(b->builder, ty, name);
        build_store(b, pv, pn->irval);
      }
    }
  }

  // build body
  Value bodyval = build_expr(b, n->fun.body, "");

  // handle implicit return at end of body
  if (!bodyval || !value_is_ret(bodyval)) {
    if (!bodyval || n->type->t.fun.result == Type_nil) {
      LLVMBuildRetVoid(b->builder);
    } else {
      // if (value_is_call(bodyval)) {
      //   // TODO: might need to add a condition for matching parameters & return type
      //   LLVMSetTailCall(bodyval, true);
      // }
      LLVMBuildRet(b->builder, bodyval);
    }
  }

  // make sure failure blocks are at the end of the function
  if (b->mgen_failb) {
    LLVMBasicBlockRef lastb = LLVMGetLastBasicBlock(fn);
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

  return fn;
}


static Value build_block(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NBlock);
  notnull(n->type);

  Value v = NULL; // return null to signal "empty block"
  bool noload = b->noload; // save
  b->noload = true;

  for (u32 i = 0; i < n->array.a.len; i++) {
    if (i == n->array.a.len - 1) {
      // load last expression of a block that is in turn being loaded
      b->noload = noload;
    }
    v = build_expr(b, n->array.a.v[i], "");
  }

  b->noload = noload; // restore
  // last expr of block is its value (TODO: is this true? is that Co's semantic?)
  return v;
}


static Value build_struct_cons(B* b, Node* n, const char* vname);


static Value build_type_call(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NCall);
  notnull(n->call.receiver->type);
  asserteq_debug(n->call.receiver->type->kind, NTypeType);

  // type call, e.g. str(1), MyStruct(x, y), etc.
  Type* tn = notnull(n->call.receiver->type->t.type);

  switch (tn->kind) {
    case NStructType:
      return build_struct_cons(b, n, vname);
    default:
      panic("TODO: type call %s", fmtnode(tn));
      return NULL;
  }
}


static Value build_fun_call(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NCall);
  notnull(n->call.receiver->type);
  asserteq_debug(n->call.receiver->type->kind, NFunType);

  // n->call.receiver->kind==NFun
  Value callee = build_expr(b, n->call.receiver, "callee");
  if (!callee) {
    errlog("unknown function");
    return NULL;
  }

  bool noload = b->noload; // save
  b->noload = false;

  // arguments
  u32 argc = 0;
  STK_ARRAY_DEFINE(argv, Value, 16);
  if (n->call.args) {
    asserteq(n->call.args->kind, NTuple);
    argc = n->call.args->array.a.len;
    STK_ARRAY_INIT(argv, b->build->mem, argc);
    for (u32 i = 0; i < argc; i++) {
      argv[i] = build_expr(b, n->call.args->array.a.v[i], "arg");
    }
  }

  b->noload = noload; // restore

  asserteq(LLVMCountParams(callee), argc);
  Value v = LLVMBuildCall(b->builder, callee, argv, argc, "");

  // LLVMSetTailCall(v, true); // set tail call when we know it for sure

  STK_ARRAY_DISPOSE(argv);
  return v;
}


static Value build_call(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NCall);
  notnull(n->type);

  Type* recvt = notnull(n->call.receiver->type);
  switch (recvt->kind) {
    case NTypeType: // type constructor call
      return build_type_call(b, n, vname);
    case NFunType: // function call
      return build_fun_call(b, n, vname);
    default:
      panic("invalid call kind=%s n=%s", NodeKindName(recvt->kind), fmtnode(n));
      return NULL;
  }
}


static Value build_init_store(B* b, Node* n, Value init, const char* vname) {
  if (!LLVMGetInsertBlock(b->builder) /*|| !b->mutable*/) {
    // global
    Value ptr = LLVMAddGlobal(b->mod, LLVMTypeOf(init), vname);
    LLVMSetLinkage(ptr, LLVMPrivateLinkage);
    LLVMSetInitializer(ptr, init);
    LLVMSetGlobalConstant(ptr, LLVMIsConstant(init));
    // LLVMSetUnnamedAddr(ptr, true);
    return ptr;
  }
  // mutable, on stack
  Value ptr = LLVMBuildAlloca(b->builder, LLVMTypeOf(init), vname);
  build_store(b, init, ptr);
  return ptr;
}


static Value nullable set_varalloca(B* b, Value v) {
  Value outer = b->varalloc;
  b->varalloc = v;
  dlog_mod(b, "set varalloc %s", fmtvalue(b->varalloc));
  return outer;
}

#if DEBUG
static Value take_varalloca(B* b, LLVMTypeRef ty)
#else
#define take_varalloca(b, ign) _take_varalloca(b)
static Value _take_varalloca(B* b)
#endif
{
  if (!b->varalloc)
    return NULL;

  #if DEBUG
  LLVMTypeRef ptrty = LLVMTypeOf(b->varalloc);
  if (ty != LLVMGetElementType(ptrty)) {
    panic("varalloca type %s != source type %s",
      fmttype(LLVMGetElementType(ptrty)), fmttype(ty));
  }
  #endif

  dlog_mod(b, "take varalloc %s", fmtvalue(b->varalloc));

  Value v = b->varalloc;
  b->varalloc = NULL;
  return v;
}


static Value build_array(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NArray);
  Type* arrayt = notnull(n->type);

  u32 valuec = n->array.a.len;
  STK_ARRAY_MAKE(valuev, b->build->mem, Value, 16, valuec);

  if (arrayt->t.array.size > 0)
    assert(valuec <= arrayt->t.array.size);

  if (valuec < arrayt->t.array.size)
    panic("TODO zero rest");

  // build values
  for (u32 i = 0; i < valuec; i++) {
    Node* cn = n->array.a.v[i];
    Value v = build_expr(b, cn, "");
    valuev[i] = v;
  }

  // if all initializers are constant, use constant initializer
  u32 nconst = 0;
  for (u32 i = 0; i < valuec; i++)
    nconst += (u32)LLVMIsConstant(valuev[i]);

  // element type
  LLVMTypeRef elemty = (
    valuec > 0 ? LLVMTypeOf(valuev[0]) :
    get_type(b, arrayt->t.array.subtype) );

  LLVMTypeRef arrayty = LLVMArrayType(elemty, arrayt->t.array.size);
  Value ptr = take_varalloca(b, arrayty);

  if (nconst == valuec) {
    // all initializers are constant
    Value init = LLVMConstArray(elemty, valuev, valuec);
    if (ptr) {
      // store to preallocated var address
      build_store(b, init, ptr);
    } else {
      // store as global
      ptr = LLVMAddGlobal(b->mod, arrayty, vname);
      LLVMSetLinkage(ptr, LLVMPrivateLinkage);
      LLVMSetInitializer(ptr, init);
      LLVMSetGlobalConstant(ptr, true);
      LLVMSetUnnamedAddr(ptr, true);
    }
    n->irval = ptr;

  } else {
    // at least one initializer varies at runtime
    Value initv = NULL;
    if (!ptr) {
      assert_debug(valuec > 0);
      initv = valuev[0]; // TODO: should it be the last value?
      ptr = LLVMBuildArrayAlloca(b->builder, arrayty, initv, "");
    }
    Value gepindexv[2] = {
      b->v_i32_0,
      LLVMConstInt(b->t_i32, arrayt->t.array.size, /*signext*/false),
    };
    for (u32 i = initv ? 1 : 0; i < valuec; i++) {
      if (valuev[i] != initv) {
        Value eptr = LLVMBuildInBoundsGEP2(b->builder, arrayty, ptr, gepindexv, 2, "");
        build_store(b, valuev[i], eptr);
      }
    }
    n->irval = ptr;
  }

  STK_ARRAY_DISPOSE(valuev);

  // n->irval = build_init_store(b, n, init, vname);
  return n->irval;
}


static Value build_typecast(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NTypeCast);
  notnull(n->type);
  notnull(n->call.args);

  panic("TODO");

  LLVMBool isSigned = false;
  LLVMTypeRef dsttype = b->t_i32;
  LLVMValueRef srcval = build_expr(b, n->call.args, "");
  return LLVMBuildIntCast2(b->builder, srcval, dsttype, isSigned, vname);
}


static Value build_return(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NReturn);
  notnull(n->type);
  // TODO: check current function and if type is nil, use LLVMBuildRetVoid
  LLVMValueRef v = build_expr(b, n->op.left, vname);
  // if (value_is_call(v))
  //   LLVMSetTailCall(v, true);
  return LLVMBuildRet(b->builder, v);
}


static LLVMTypeRef build_struct_type(B* b, Type* n) {
  asserteq_debug(n->kind, NStructType);

  u32 elemc = n->t.struc.a.len;
  STK_ARRAY_MAKE(elemv, b->build->mem, LLVMTypeRef, 16, n->t.struc.a.len);

  for (u32 i = 0; i < n->t.struc.a.len; i++) {
    Node* field = n->t.struc.a.v[i];
    asserteq_debug(field->kind, NField);
    elemv[i] = get_type(b, field->type);
  }

  LLVMTypeRef ty;
  if (n->t.struc.name) {
    ty = LLVMStructCreateNamed(b->ctx, n->t.struc.name);
    LLVMStructSetBody(ty, elemv, elemc, /*packed*/false);
  } else {
    ty = LLVMStructTypeInContext(b->ctx, elemv, elemc, /*packed*/false);
  }

  STK_ARRAY_DISPOSE(elemv);

  return ty;
}


static LLVMTypeRef build_array_type(B* b, Type* n) {
  asserteq_debug(n->kind, NArrayType);
  assert(n->t.array.size > 0); // TODO: slice types e.g. "[int]"
  LLVMTypeRef elemty = get_type(b, n->t.array.subtype);
  return LLVMArrayType(elemty, n->t.array.size);
}


static LLVMTypeRef build_tuple_type(B* b, Type* n) {
  asserteq_debug(n->kind, NTupleType);

  u32 typesc = n->t.tuple.a.len;
  STK_ARRAY_MAKE(typesv, b->build->mem, LLVMTypeRef, 16, typesc);

  bool noload = b->noload;
  b->noload = false;
  for (u32 i = 0; i < typesc; i++) {
    typesv[i] = get_type(b, n->t.tuple.a.v[i]);
  }
  b->noload = noload; // restore

  STK_ARRAY_DISPOSE(typesv);

  return LLVMStructTypeInContext(b->ctx, typesv, typesc, /*packed*/false);
}


static LLVMTypeRef get_struct_type(B* b, Type* tn) {
  asserteq_debug(tn->kind, NStructType);
  if (!tn->irval) {
    if (tn->t.struc.name) { // not interning named struct types
      tn->irval = build_struct_type(b, tn);
    } else {
      tn->irval = get_intern_type(b, tn);
      if (!tn->irval) {
        tn->irval = build_struct_type(b, tn);
        add_intern_type(b, tn, tn->irval);
      }
    }
  }
  return tn->irval;
}


static LLVMTypeRef get_tuple_type(B* b, Type* tn) {
  asserteq_debug(tn->kind, NTupleType);
  if (!tn->irval) {
    tn->irval = get_intern_type(b, tn);
    if (!tn->irval) {
      tn->irval = build_tuple_type(b, tn);
      add_intern_type(b, tn, tn->irval);
    }
  }
  return tn->irval;
}


static LLVMTypeRef get_array_type(B* b, Type* tn) {
  asserteq_debug(tn->kind, NArrayType);
  if (!tn->irval) {
    tn->irval = get_intern_type(b, tn);
    if (!tn->irval) {
      tn->irval = build_array_type(b, tn);
      add_intern_type(b, tn, tn->irval);
    }
  }
  return tn->irval;
}


static Value build_struct_type_expr(B* b, Type* n, const char* vname) {
  LLVMTypeRef ty = get_struct_type(b, n);
  dlog_mod(b, "build_struct_type_expr %s", fmttype(ty));

  if ((n->flags & NodeFlagRValue) && !b->noload) {
    // struct type used as value
    panic("TODO: build type value %s", fmttype(ty));
  }

  return NULL;
}


static Value build_anon_struct(
  B* b, Value* values, u32 numvalues, Value* ptr_out, const char* vname)
{
  u32 nconst = 0;
  for (u32 i = 0; i < numvalues; i++)
    nconst += LLVMIsConstant(values[i]);

  if (nconst == numvalues) {
    // all values are constant
    Value init = LLVMConstStructInContext(b->ctx, values, numvalues, /*packed*/false);
    if (b->mut == Mutable) {
      // struct will be modified; allocate on stack
      LLVMTypeRef ty = LLVMTypeOf(init);
      Value ptr = take_varalloca(b, ty);
      if (!ptr)
        ptr = LLVMBuildAlloca(b->builder, ty, vname);
      build_store(b, init, ptr);
      *ptr_out = ptr;
    } else {
      // struct is read-only; allocate as global
      LLVMValueRef ptr = LLVMAddGlobal(b->mod, LLVMTypeOf(init), vname);
      LLVMSetLinkage(ptr, LLVMPrivateLinkage);
      LLVMSetInitializer(ptr, init);
      LLVMSetGlobalConstant(ptr, true);
      LLVMSetUnnamedAddr(ptr, true);
      *ptr_out = ptr;
    }
    return init;
  }

  // some values vary at runtime

  STK_ARRAY_DEFINE(typesv, LLVMTypeRef, 16);
  STK_ARRAY_INIT(typesv, b->build->mem, numvalues);
  for (u32 i = 0; i < numvalues; i++) {
    typesv[i] = LLVMTypeOf(values[i]);
  }

  LLVMTypeRef ty = LLVMStructTypeInContext(b->ctx, typesv, numvalues, /*packed*/false);
  Value ptr = take_varalloca(b, ty);
  if (!ptr)
    ptr = LLVMBuildAlloca(b->builder, ty, vname);

  for (u32 i = 0; i < numvalues; i++) {
    LLVMValueRef fieldptr = LLVMBuildStructGEP2(b->builder, ty, ptr, i, "");
    build_store(b, values[i], fieldptr);
  }

  STK_ARRAY_DISPOSE(typesv);
  *ptr_out = ptr;
  if (b->noload)
    return NULL;

  return build_load(b, ty, ptr, vname);
}


static Value build_struct_init(B* b, Type* tn, Node* nullable init, LLVMTypeRef ty);


// build_initializer builds initialization value for type tn
static Value build_initializer(B* b, Type* tn, Node* nullable init, const char* name) {
  if (init)
    return build_expr(b, init, name);

  LLVMTypeRef ty = get_type(b, tn);

  if (tn->kind == NStructType)
    return build_struct_init(b, tn, init, ty);

  return LLVMConstNull(ty);
}


static Value build_field(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NField);
  // TODO: use constructor arguments if present
  return build_initializer(b, n->type, n->field.init, n->field.name);
}


static Value build_struct_init(B* b, Type* tn, Node* nullable args, LLVMTypeRef ty) {
  asserteq_debug(tn->kind, NStructType);
  assert_debug(args == NULL || args->kind == NTuple);

  Value v = NULL;

  if (args == NULL && (v = get_default_init(b, ty))) {
    // use precomputed default constant initializer
    return v;
  }

  if (args)
    dlog_mod(b, "TODO: use args as initializers %s", fmtnode(args));

  u32 numvalues = tn->t.struc.a.len;
  u32 numerrors = 0;
  STK_ARRAY_MAKE(values, b->build->mem, Value, 16, numvalues);

  for (u32 i = 0; i < numvalues; i++) {
    Node* field = tn->t.struc.a.v[i];
    Node* initexpr = field->field.init; // TODO: use constructor value if present
    values[i] = build_initializer(b, field->type, initexpr, field->field.name);
    // dlog_mod(b, "values[%u] %s", i, fmtvalue(values[i]));

    if (R_UNLIKELY(initexpr == field->field.init && !LLVMIsConstant(values[i]))) {
      // field's default initializer is not a constant (e.g. may be var load instead)
      build_errf(b->build, NodePosSpan(initexpr),
        "non-constant field initializer %s", fmtnode(initexpr));
      node_diag_trailn(b->build, DiagNote, field->field.init, 1);
    }
  }

  // if all inits are zero, use LLVMConstNull
  u32 nzero = 0;
  u32 nconst = 0;
  for (u32 i = 0; i < numvalues; i++)
    nzero += (u32)LLVMIsNull(values[i]);
  if (numerrors != 0 || nzero == numvalues) {
    v = LLVMConstNull(ty);
    nconst = numvalues;
    goto end;
  }

  // if all initializers are constant, use constant initializer
  for (u32 i = 0; i < numvalues; i++)
    nconst += (u32)LLVMIsConstant(values[i]);
  if (nconst == numvalues) {
    const char* structName = LLVMGetStructName(ty);
    if (structName) {
      // LLVM treats named struct types as unique and so we can't use
      // LLVMConstStructInContext to create initializer for a struct of named type.
      v = LLVMConstNamedStruct(ty, values, numvalues);
    } else {
      v = LLVMConstStructInContext(b->ctx, values, numvalues, /*packed*/false);
    }
    goto end;
  }

  panic("TODO: non-const struct initializer");


end:
  STK_ARRAY_DISPOSE(values);
  if (args == NULL && nconst == numvalues)
    add_default_init(b, ty, v); // save as default constant initializer
  return v;
}


static Value build_struct_cons(B* b, Node* n, const char* vname) {
  // called by build_type_call
  asserteq_debug(n->kind, NCall);

  Type* recvt = notnull(n->call.receiver->type);
  asserteq_debug(recvt->kind, NTypeType);

  Type* structType = notnull(recvt->t.type);
  asserteq_debug(structType->kind, NStructType);

  LLVMTypeRef ty = get_struct_type(b, structType);
  Value init = build_struct_init(b, structType, n->call.args, ty);
  n->irval = build_init_store(b, n, init, vname);
  return n->irval;
}


static Value load_var(B* b, Node* n, const char* vname) {
  assert_debug(n->kind == NVar);
  Value v = (Value)n->irval;

  if (NodeIsConst(n) || b->noload)
    return v;

  notnull(v);
  assert_llvm_type_isptr(LLVMTypeOf(v));

  if (R_UNLIKELY(b->fnest == 0)) {
    // var load in global scope is the same as using its initializer
    return LLVMGetInitializer(v);
  }

  if (vname[0] == 0)
    vname = notnull(n->var.name);

  LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(v));
  dlog_mod(b, "load_var ptr (type %s => %s): %s",
    fmttype(LLVMTypeOf(v)), fmttype(ty), fmtvalue(v));
  return build_load(b, ty, v, vname);
}


static Value build_var_def(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NVar);
  assertnull_debug(n->irval);
  assert_debug( ! NodeIsParam(n)); // params are eagerly built by build_fun
  notnull(LLVMGetInsertBlock(b->builder)); // local, not global

  if (n->flags & NodeFlagUnused) // skip unused var
    return NULL;

  notnull(n->type);

  if (vname[0] == 0)
    vname = n->var.name;

  bool noload = b->noload; // save
  b->noload = false;

  if (NodeIsConst(n)) {
    // immutable variable
    if (n->var.init) {
      n->irval = build_expr(b, n->var.init, vname);
    } else {
      n->irval = build_default_value(b, n->type);
    }
    b->noload = noload; // restore
    return (Value)n->irval;
  }

  // mutable variable
  LLVMTypeRef ty = get_type(b, n->type);

  #if DEBUG
  char namebuf[128];
  snprintf(namebuf, sizeof(namebuf), "var_%s", vname);
  vname = namebuf;
  #endif

  // dlog("var type: %s", fmttype(ty));
  n->irval = LLVMBuildAlloca(b->builder, ty, vname);

  bool store = true;

  Value init;
  if (n->var.init) {
    Value outer_varalloca = set_varalloca(b, n->irval);
    init = build_expr(b, n->var.init, vname);
    // dlog("var init: %s = %s", fmttype(LLVMTypeOf(init)), fmtvalue(init));
    store = b->varalloc != NULL; // varalloc used -- skip store of init
    b->varalloc = outer_varalloca; // restore
  } else {
    // zero initialize
    init = LLVMConstNull(ty);
  }

  if (store)
    build_store(b, init, n->irval);

  b->noload = noload; // restore
  return (Value)n->irval;
}


static Value build_var(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NVar);

  // build var if needed
  if (!n->irval) {
    Mutability mut = set_mut_based_on_node_const(b, n);
    build_var_def(b, n, vname);
    b->mut = mut; // restore
  }

  return load_var(b, n, vname);
}


static Value build_global_var(B* b, Node* n) {
  assert(n->kind == NVar);
  assert(n->type);

  if (NodeIsConst(n) && n->var.init && NodeIsType(n->var.init))
    return NULL;

  Value init;
  if (n->var.init) {
    init = build_expr(b, n->var.init, n->var.name);
    notnull(init);
    if (!LLVMIsConstant(init))
      panic("not a constant expression %s", fmtnode(n));
  } else {
    init = LLVMConstNull(get_type(b, n->type));
  }

  if (NodeIsConst(n)) {
    n->irval = init;
  } else {
    Value ptr = LLVMAddGlobal(b->mod, LLVMTypeOf(init), n->var.name);
    LLVMSetLinkage(ptr, LLVMPrivateLinkage);
    LLVMSetInitializer(ptr, init);
    LLVMSetGlobalConstant(ptr, false);
    // LLVMSetUnnamedAddr(ptr, true);
    n->irval = ptr; // save pointer for later lookups
  }

  return n->irval;
}


static Value build_id_read(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NId);
  notnull(n->id.target); // should be resolved

  Value v = build_expr(b, n->id.target, n->id.name);
  n->irval = n->id.target->irval;
  return v;
}


static Value build_tuple(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NTuple);
  notnull(n->type);

  u32 valuesc = n->array.a.len;
  STK_ARRAY_MAKE(valuesv, b->build->mem, Value, 16, valuesc);

  bool noload = b->noload;
  b->noload = false;
  for (u32 i = 0; i < valuesc; i++) {
    valuesv[i] = build_expr(b, n->array.a.v[i], "");
  }
  b->noload = noload; // restore

  Value v = build_anon_struct(b, valuesv, valuesc, (Value*)&n->irval, vname);

  STK_ARRAY_DISPOSE(valuesv);
  return v;
}


static Value build_assign_var(B* b, Node* n, const char* vname) {
  asserteq_debug(n->op.left->kind, NVar);

  const char* name = n->op.left->var.name;
  Value ptr = build_expr_noload(b, n->op.left, name);


  // build rvalue into the var's memory (varalloc)
  Mutability outer_mut = set_mut(b, Mutable);
  Value outer_varalloca = set_varalloca(b, ptr);
  Value right = build_expr_noload(b, n->op.right, "rvalue");
  bool store = b->varalloc != NULL; // varalloc not used?
  b->varalloc = outer_varalloca; // restore
  b->mut = outer_mut; // restore

  if (store && right != ptr /* not e.g. "x = x" */)
    build_store(b, right, ptr);

  // value of assignment is its new value
  if ((n->flags & NodeFlagRValue) && !b->noload) {
    // TODO: can we just return "right" here instead..?
    LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(ptr));
    return build_load(b, ty, ptr, name);
  }

  return NULL;
}


static Value build_assign_tuple(B* b, Node* n, const char* vname) {
  Node* targets = n->op.left;
  Node* sources = n->op.right;
  asserteq_debug(targets->kind, NTuple);
  asserteq_debug(sources->kind, NTuple);
  asserteq_debug(targets->array.a.len, sources->array.a.len);

  u32 srcvalsc = sources->array.a.len;
  STK_ARRAY_DEFINE(srcvalsv, Value, 16);
  STK_ARRAY_INIT(srcvalsv, b->build->mem, srcvalsc);

  // first load all sources in case a source var is in targets
  for (u32 i = 0; i < srcvalsc; i++) {
    Node* srcn = sources->array.a.v[i];
    Node* dstn = targets->array.a.v[i];
    if (srcn) {
      srcvalsv[i] = build_expr_mustload(b, srcn, "");
    } else {
      // variable definition
      build_var_def(b, dstn, dstn->var.name);
      srcvalsv[i] = load_var(b, dstn, dstn->var.name);
    }
    notnull(srcvalsv[i]);
  }

  // now store
  Mutability outer_mut = set_mut(b, Mutable);
  for (u32 i = 0; i < srcvalsc; i++) {
    Node* srcn = sources->array.a.v[i];
    Node* dstn = targets->array.a.v[i];

    if (srcn) {
      // assignment to existing memory location
      if (dstn->kind != NVar)
        panic("TODO: dstn %s", NodeKindName(dstn->kind));
      Value ptr = notnull(build_expr_noload(b, dstn, dstn->var.name));
      build_store(b, srcvalsv[i], ptr);
    }
  }
  b->mut = outer_mut; // restore

  Value v = NULL;

  // if the assignment is used as a value, make tuple val
  if ((n->flags & NodeFlagRValue) && !b->noload) {
    for (u32 i = 0; i < srcvalsc; i++) {
      Node* dstn = targets->array.a.v[i];
      if (dstn->kind != NVar)
        panic("TODO: dstn %s", NodeKindName(dstn->kind));
      srcvalsv[i] = load_var(b, dstn, dstn->var.name);
    }
    v = build_anon_struct(b, srcvalsv, srcvalsc, (Value*)&n->irval, vname);
  }

  STK_ARRAY_DISPOSE(srcvalsv);
  return v;
}


static Value build_assign_index(B* b, Node* n, const char* vname) {
  asserteq_debug(n->op.left->kind, NIndex);

  Node* target = n->op.left->index.operand;      // i.e. "x" in "x[3] = y"
  Node* indexexpr = n->op.left->index.indexexpr; // i.e. "3" in "x[3] = y"
  Node* source = n->op.right;                    // i.e. "y" in "x[3] = y"

  Value indexval = build_expr_mustload(b, indexexpr, "");
  Value srcval = build_expr_mustload(b, source, "");

  Type* targett = notnull(target->type);
  assert_debug(targett->kind == NArrayType || targett->kind == NTupleType);

  if (targett->kind == NArrayType) {
    assert_debug(TypeEquals(b->build, targett->t.array.subtype, source->type));
    Mutability outer_mut = set_mut(b, Mutable);
    Value arrayptr = notnull(build_expr_noload(b, target, vname));
    b->mut = outer_mut; // restore
    assert_llvm_type_isptr(LLVMTypeOf(arrayptr));

    LLVMValueRef indexv[2] = {
      b->v_i32_0,
      indexval,
    };

    // "inbounds" — the result value of the GEP is undefined if the address is outside
    // the actual underlying allocated object and not the address one-past-the-end.
    LLVMTypeRef arrayty = LLVMGetElementType(LLVMTypeOf(arrayptr));
    Value elemptr = LLVMBuildInBoundsGEP2(
      b->builder, arrayty, arrayptr, indexv, 2, vname);

    build_store(b, srcval, elemptr);
    n->irval = srcval;
    return srcval; // value of assignment expression is its new value
  }

  panic("TODO tuple");
  return NULL;
}


static Value build_assign(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NAssign);
  notnull(n->type);

  switch (n->op.left->kind) {
    case NVar:   R_MUSTTAIL return build_assign_var(b, n, vname);
    case NTuple: R_MUSTTAIL return build_assign_tuple(b, n, vname);
    case NIndex: R_MUSTTAIL return build_assign_index(b, n, vname);
    default:
      panic("TODO assign to %s", NodeKindName(n->op.left->kind));
      return NULL;
  }
}


static Value build_bin_op(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NBinOp);
  notnull(n->type);

  Type* tn = n->op.left->type;
  asserteq_debug(tn->kind, NBasicType);
  assert_debug(tn->t.basic.typeCode < TypeCode_CONCRETE_END);
  assert_debug(n->op.op < T_PRIM_OPS_END);

  Value left = build_expr(b, n->op.left, "");
  Value right = build_expr(b, n->op.right, "");
  u32 op = 0;

  // signed integer binary operators
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

  // unsigned integer binary operators
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

  // floating-point number binary operators
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

  bool isfloat = false;

  switch (tn->t.basic.typeCode) {
  case TypeCode_bool:
    switch (n->op.op) {
    case TEq:  op = LLVMIntEQ; break; // ==
    case TNEq: op = LLVMIntNE; break; // !=
    default: break;
    }
    break;
  case TypeCode_i8:
  case TypeCode_i16:
  case TypeCode_i32:
  case TypeCode_i64:
  case TypeCode_int:
    op = kOpTableSInt[n->op.op];
    break;
  case TypeCode_u8:
  case TypeCode_u16:
  case TypeCode_u32:
  case TypeCode_u64:
  case TypeCode_uint:
    op = kOpTableUInt[n->op.op];
    break;
  case TypeCode_f32:
  case TypeCode_f64:
    isfloat = true;
    op = kOpTableFloat[n->op.op];
    break;
  default:
    break;
  }

  if (op == 0) {
    build_errf(b->build, NodePosSpan(n), "invalid operand type %s", fmtnode(tn));
    return NULL;
  }

  if (n->op.op >= TEq && n->op.op <= TGEq) {
    // See how Go compares values: https://golang.org/ref/spec#Comparison_operators
    if (isfloat)
      return LLVMBuildFCmp(b->builder, (LLVMRealPredicate)op, left, right, vname);
    return LLVMBuildICmp(b->builder, (LLVMIntPredicate)op, left, right, vname);
  }
  return LLVMBuildBinOp(b->builder, (LLVMOpcode)op, left, right, vname);
}


static Value build_if(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NIf);
  notnull(n->type);

  bool isrvalue = (n->flags & NodeFlagRValue) && !b->noload;

  // condition
  notnull(n->cond.cond->type);
  asserteq_debug(n->cond.cond->type->kind, NBasicType);
  asserteq_debug(get_type(b, n->cond.cond->type), b->t_bool);
  Value condExpr = build_expr(b, n->cond.cond, "if.cond");

  Value fn = get_current_fun(b);

  LLVMBasicBlockRef thenb = LLVMAppendBasicBlockInContext(b->ctx, fn, "if.then");
  LLVMBasicBlockRef elseb = NULL;
  if (n->cond.elseb || isrvalue)
    elseb = LLVMCreateBasicBlockInContext(b->ctx, "if.else");
  LLVMBasicBlockRef endb = LLVMCreateBasicBlockInContext(b->ctx, "if.end");

  LLVMBuildCondBr(b->builder, condExpr, thenb, elseb ? elseb : endb);

  // then
  LLVMPositionBuilderAtEnd(b->builder, thenb);
  Value thenVal = build_expr(b, n->cond.thenb, "");
  LLVMBuildBr(b->builder, endb);
  // Codegen of "then" can change the current block, update thenb for the PHI
  thenb = LLVMGetInsertBlock(b->builder);

  // else
  Value elseVal = NULL;
  if (elseb) {
    LLVMAppendExistingBasicBlock(fn, elseb);
    LLVMPositionBuilderAtEnd(b->builder, elseb);
    if (n->cond.elseb) {
      if (!TypeEquals(b->build, n->cond.thenb->type, n->cond.elseb->type))
        panic("TODO: mixed types");
      elseVal = build_expr(b, n->cond.elseb, "");
    } else {
      elseVal = build_default_value(b, n->cond.thenb->type);
    }
    LLVMBuildBr(b->builder, endb);
    // Codegen of "then" can change the current block, update thenb for the PHI
    elseb = LLVMGetInsertBlock(b->builder);
  }

  // end
  LLVMAppendExistingBasicBlock(fn, endb);
  LLVMPositionBuilderAtEnd(b->builder, endb);

  if (!isrvalue) // "if" is used as a statement
    return NULL;

  // result type of if expression
  LLVMTypeRef ty = LLVMTypeOf(thenVal);
  Value phi = LLVMBuildPhi(b->builder, ty, ty == b->t_void ? "" : "if");
  Value             incomingValues[2] = { thenVal, elseVal };
  LLVMBasicBlockRef incomingBlocks[2] = { thenb,   elseb };
  LLVMAddIncoming(phi, incomingValues, incomingBlocks, 2);

  return phi;
}


static Value build_namedval(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NNamedVal);
  Value v = build_expr(b, n->namedval.value, n->namedval.name);
  n->irval = n->namedval.value->irval;
  return v;
}


static LLVMTypeRef refstruct_type_build(B* b) {
  // struct const_slice_ref {
  //   ptr memaddr // pointer to data
  //   len uint    // number of valid entries at ptr
  // }
  // struct mut_slice_ref {
  //   ptr memaddr // pointer to data
  //   len uint    // number of valid entries at ptr
  //   cap uint    // number of entries that can be stored at ptr
  // }
  // mut_ref =
  //   IF SAFE
  //     struct {
  //       ptr i8* // pointer to data
  //       gen u32 // memory generation (0 for non-heap objects)
  //     }
  //   ELSE
  //     T*
  //
  LLVMTypeRef refty = LLVMStructCreateNamed(b->ctx, "REF");
  LLVMTypeRef fields_types[] = {
    b->t_i32ptr, // i32*
    b->t_i32,    // i32
  };
  LLVMStructSetBody(refty, fields_types, countof(fields_types), /*packed*/false);
  b->t_ref = refty;
  return b->t_ref;
}


static LLVMTypeRef refstruct_type(B* b) {
  if (R_LIKELY(b->t_ref))
    return b->t_ref;
  return refstruct_type_build(b);
}


static Value build_refstruct_store(B* b, Value refptr, Value valptr, Value genval) {
  assert_llvm_type_isptr(LLVMTypeOf(notnull(valptr)));
  assert_debug(LLVMTypeOf(notnull(genval)) == b->t_i32);

  LLVMTypeRef refty = refstruct_type(b);
  Value ptrep = LLVMBuildStructGEP2(b->builder, refty, refptr, 0, "ref.ptr");
  Value genep = LLVMBuildStructGEP2(b->builder, refty, refptr, 1, "ref.gen");

  if (LLVMTypeOf(valptr) != b->t_i32ptr)
    valptr = LLVMBuildPointerCast(b->builder, valptr, b->t_i32ptr, "ptr");
  build_store(b, valptr, ptrep);
  build_store(b, genval, genep);

  return refptr;
}


// build_refstruct_deref builds a "*x" operation on a "safe reference",
// checking heap memory
static Value build_refstruct_deref(
  B* b, Value ref, LLVMTypeRef elem_ty, Value* valptr_out, const char* vname)
{
  LLVMTypeRef refty = refstruct_type(b);
  asserteq_debug(LLVMTypeOf(ref), refty); // ref must be %REF

  Value valptr = LLVMBuildExtractValue(b->builder, ref, 0, dnamef(b, "%s_ptr", vname));
  Value rgen = LLVMBuildExtractValue(b->builder, ref, 1, dnamef(b, "%s_gen", vname));
  // Value ptrep = LLVMBuildStructGEP2(b->builder, refty, refptr, 0, "ref.ptr");
  // Value genep = LLVMBuildStructGEP2(b->builder, refty, refptr, 1, "ref.gen");
  // Value valptr = build_load(b, b->t_i32ptr, ptrep, "ptr");
  // Value rgen = build_load(b, b->t_i32, genep, "rgen");

  // build check of gen
  Value fn = get_current_fun(b);
  LLVMBasicBlockRef checkb = LLVMAppendBasicBlockInContext(
    b->ctx, fn, dnamef(b, "refcheck_%s", vname));
  LLVMBasicBlockRef contb = LLVMAppendBasicBlockInContext(
    b->ctx, fn, dnamef(b, "refcheck_%s.ok", vname));

  // if gen != 0 then compare to gen at ptr memory
  LLVMValueRef gen_ne_zero = LLVMBuildICmp(
    b->builder, LLVMIntNE, rgen, b->v_i32_0, dnamef(b, "%s_hasgen", vname));
  LLVMBuildCondBr(b->builder, gen_ne_zero, checkb, contb);

  // gencheck failure branch (one per function shared by all defrefs)
  if (!b->mgen_failb) {
    assertnull_debug(b->mgen_alloca); // should be in sync w/ mgen_failb

    // generate stack space for the "current failing ref" to be used by mgen_failb
    LLVMBasicBlockRef entryb = LLVMGetEntryBasicBlock(fn);
    LLVMPositionBuilderAtEnd(b->builder, entryb);
    Value instr0 = LLVMGetFirstInstruction(entryb);
    if (instr0) {
      // make sure we add the alloca before the block's terminator.
      // we do this by inserting the alloca to the head.
      LLVMPositionBuilderBefore(b->builder, instr0);
    }
    b->mgen_alloca = LLVMBuildAlloca(b->builder, refty, "failref");

    // build the "ref failed" branch
    b->mgen_failb = LLVMAppendBasicBlockInContext(b->ctx, fn, "refcheck.fail");
    LLVMPositionBuilderAtEnd(b->builder, b->mgen_failb);
    dlog("TODO: call panic in refcheck.fail branch");
    // TODO: pass b->mgen_alloca to panic/error call; it contains the bad ref
    LLVMBuildUnreachable(b->builder);
  }

  // gencheck branch (gen is non-zero)
  LLVMPositionBuilderAtEnd(b->builder, checkb);
  // build_refstruct_store(b, b->mgen_alloca, valptr, rgen); // copy ref to mgen_alloca
  Value mgen = build_load(b, b->t_i32, valptr, dnamef(b, "%s_mgen", vname));

  // if gen != *((i32*)ptr) then goto mgen_failb
  LLVMValueRef gen_ne = LLVMBuildICmp(
    b->builder, LLVMIntNE, rgen, mgen, dnamef(b, "%s_isbadgen", vname));
  Value failbr = LLVMBuildCondBr(b->builder, gen_ne, b->mgen_failb, contb);
  set_br_unlikely(b, failbr);
  // LLVMValueRef gen_eq = LLVMBuildICmp(b->builder, LLVMIntEQ, rgen, mgen, "");
  // Value okbr = LLVMBuildCondBr(b->builder, gen_eq, contb, b->mgen_failb);
  // set_br_likely(b, okbr);

  // gencheck.cont branch
  LLVMPositionBuilderAtEnd(b->builder, contb);

  // cast if needed
  if (elem_ty != LLVMGetElementType(b->t_i32ptr)) {
    LLVMTypeRef final_ptrty = LLVMPointerType(elem_ty, 0);
    valptr = LLVMBuildPointerCast(b->builder, valptr, final_ptrty, "ptr");
  }

  Value v = b->noload ? NULL : build_load(b, elem_ty, valptr, vname);
  // TODO if gen != 0, cmp gen at v

  *valptr_out = valptr;
  return v;
}


// build_refstruct_new builds a "&x" operation, creating a "safe reference"
// If ptr comes from the heap, genval should contain the generation value, else be i32(0).
static Value build_refstruct_new(B* b, Value valptr, Value genval, const char* vname) {
  LLVMTypeRef refty = refstruct_type(b);

  #if DEBUG
  char namebuf[128];
  snprintf(namebuf, sizeof(namebuf), "ref_%s", vname);
  vname = namebuf;
  #endif

  Value refptr = take_varalloca(b, refty); // use memory preallocated for var
  if (!refptr) {
    // TODO: can we use @llvm.lifetime.start to mark the lifetime range of this alloca?
    refptr = LLVMBuildAlloca(b->builder, refty, vname);
  }
  return build_refstruct_store(b, refptr, valptr, genval); // => refptr
}


// build_ref creates a reference (pointer) to value n. i.e. "T => T*".
// e.g. "&x" => "take the address of x"
static Value build_ref(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NRef);

  // take address of target
  n->irval = build_expr_noload(b, n->ref.target, vname);

  // can only reference stuff that has a memory location
  asserteq_debug(LLVMGetTypeKind(LLVMTypeOf(notnull(n->irval))), LLVMPointerTypeKind);

  // if building in safe mode, refs are "wide pointers", else just a pointer
  if (b->build->safe) {
    n->irval = build_refstruct_new(b, n->irval, b->v_i32_0, vname);
    if (b->noload)
      return NULL;
    // replace the pointer address "T*" with "%REF{T*,gen}"
    LLVMTypeRef refty = refstruct_type(b);
    return build_load(b, refty, n->irval, vname);
  }

  return n->irval;
}


// build_deref loads the value at reference n. i.e. "T* => T".
// e.g. "*x" => "load the value at address x"
static Value build_deref(B* b, Node* n, Value* valptr_out, const char* vname) {
  asserteq_debug(notnull(n->type)->kind, NRefType);

  #if DEBUG
  if (vname[0] == 0 && n->kind == NId)
    vname = n->id.name;
  #endif

  Value ref = build_expr(b, n, vname); // "T*" or "%REF{i32*,gen}"

  // if building in safe mode, refs are "wide pointers", else just a pointer
  if (b->build->safe) {
    LLVMTypeRef elem_ty = get_type(b, notnull(n->type)->t.ref); // e.g. "T*"
    return build_refstruct_deref(b, ref, elem_ty, valptr_out, vname);
  }

  if (b->noload)
    return NULL;

  LLVMTypeRef elem_ty = LLVMGetElementType(LLVMTypeOf(notnull(ref)));
  return build_load(b, elem_ty, ref, vname);
}


static Value build_selector(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NSelector);
  notnull(n->type);
  assert_debug(n->sel.indices.len > 0); // GEP path should be resolved by type resolver

  // #ifdef DEBUG
  // fprintf(stderr, "[%s] GEP index path:", __func__);
  // for (u32 i = 0; i < n->sel.indices.len; i++) {
  //   fprintf(stderr, " %u", n->sel.indices.v[i]);
  // }
  // fprintf(stderr, "\n");
  // #endif

  Value ptr = notnull(build_expr_noload(b, n->sel.operand, vname));
  LLVMTypeRef st_ty = LLVMGetElementType(LLVMTypeOf(ptr));

  u32 nindices = n->sel.indices.len + 1; // first index is for pointer array/offset
  STK_ARRAY_MAKE(indices, b->build->mem, LLVMValueRef, 16, nindices);
  indices[0] = b->v_i32_0;
  for (u32 i = 0; i < n->sel.indices.len; i++) {
    indices[i + 1] = LLVMConstInt(b->t_i32, n->sel.indices.v[i], /*signext*/false);
  }

  // note: llvm kindly coalesces consecutive GEPs, meaning that nested NSelector nodes
  // become a single GEP. e.g. "x.foo.bar" into nested struct "bar".

  n->irval = LLVMBuildInBoundsGEP2(
    b->builder, st_ty, ptr, indices, nindices, vname);

  STK_ARRAY_DISPOSE(indices);

  if (b->noload)
    return n->irval;

  LLVMTypeRef elem_ty = LLVMGetElementType(LLVMTypeOf(n->irval));
  return build_load(b, elem_ty, n->irval, vname);
}


static Value build_index(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NIndex);
  notnull(n->type);

  Node* operand = notnull(n->index.operand);
  u32 index = n->index.index;

  // vname "operand.index"
  #ifdef DEBUG
  char vname2[256];
  if (vname[0] == 0 && operand->kind == NVar && index != 0xFFFFFFFF) {
    int len = snprintf(
      vname2, sizeof(vname2), "%s.%u", operand->var.name, index);
    if ((size_t)len >= sizeof(vname2))
      vname2[sizeof(vname2) - 1] = '\0'; // truncate
    vname = vname2;
  }
  #endif

  Value v = notnull(build_expr_mustload(b, operand, vname));
  LLVMTypeRef ty = LLVMTypeOf(v);
  // dlog("v %s = %s", fmttype(ty), fmtvalue(v));

  // automatic deref
  if (ty == b->t_ref) {
    // dereference safe ref, e.g. "%REF{i32*,gen}" => "[3 x i64]"
    // get actual type of ref pointer
    Type* operandt = notnull(operand->type);
    asserteq_debug(operandt->kind, NRefType);
    LLVMTypeRef aty = get_type(b, operandt->t.ref); // e.g. "[3 x i64]" (not ptr)

    bool noload = b->noload; // save
    b->noload = true;
    build_refstruct_deref(b, v, aty, (Value*)&n->irval, dnamef(b, "%s.ptr", vname));
    b->noload = noload; // restore

    v = n->irval;
    ty = LLVMTypeOf(v); // == LLVMPointerType(aty, 0)
  }

  switch (LLVMGetTypeKind(ty)) {

    case LLVMArrayTypeKind: { // "[3 x i64]" (not a pointer)
      if (index != 0xFFFFFFFF) {
        // constant index can use extractvalue instruction (basically offset)
        if (LLVMIsConstant(v))
          return LLVMConstExtractValue(v, &index, 1);
        return LLVMBuildExtractValue(b->builder, v, index, dnamef(b, "%s.val", vname));
      }

      // unbox operand to get to the storage owner (a function arg)
      Node* vn = NodeUnbox(operand, /*unrefVars*/false);

      // else: index is not constant; we need an intermediate alloca
      Value irval = (Value)vn->irval;
      LLVMTypeRef ty2 = irval ? LLVMTypeOf(irval) : NULL;
      if (irval && LLVMGetTypeKind(ty2) == LLVMPointerTypeKind) {
        // use existing memory
        v = irval;
        ty = ty2;
      } else {
        // no existing alloca; create one
        #if DEBUG
        if (vn->kind == NVar)
          vname = vn->var.name;
        #endif
        Value ptr = LLVMBuildAlloca(b->builder, ty, dnamef(b, "%s.tmp", vname));
        build_store(b, v, ptr);
        v = ptr;
        ty = LLVMTypeOf(ptr);
        vn->irval = ptr;
      }

      // continue with GEP load below
      break;
    }

    case LLVMStructTypeKind: {
      // tuple
      if (index != 0xFFFFFFFF && LLVMIsConstant(v))
        return LLVMConstExtractValue(v, &index, 1);
      // unbox operand to get to the storage owner
      Node* vn = NodeUnbox(operand, /*unrefVars*/false);
      // Note: at this point v might be a load and we will ignore it, leave it unused.
      // We could clean it up with LLVMInstructionRemoveFromParent(v) but LLVM already
      // does this during codegen (verified for x86_64), so we just leave it be.
      v = (Value)vn->irval;
      ty = LLVMTypeOf(v);
      break;
    }

    case LLVMPointerTypeKind:
      break;

    case LLVMVectorTypeKind:
      dlog("TODO: use LLVMBuildExtractElement");
      FALLTHROUGH;
    default:
      panic("unexpected operand type %s in index op", fmttype(ty));
  }

  // at this point v & ty must be a pointer to an array "[N x T]*" or struct (tuple)
  // dlog("v [%d] %s = %s", LLVMGetTypeKind(ty), fmttype(ty), fmtvalue(v));

  assert_llvm_type_isptr(ty);
  LLVMTypeRef seqty = LLVMGetElementType(ty); // e.g. "[3 x i32]" or "{i32, i32}"
  assert_debug(LLVMGetTypeKind(seqty) == LLVMArrayTypeKind ||
               LLVMGetTypeKind(seqty) == LLVMStructTypeKind);
  asserteq_debug(LLVMTypeOf(v), ty);

  // index value is either a compile-time constant or something we need to compute
  Value indexval;
  if (index != 0xFFFFFFFF) {
    if (LLVMIsConstant(v) && LLVMGetTypeKind(seqty) == LLVMArrayTypeKind) {
      // use constant value
      assert_debug(index < LLVMGetArrayLength(seqty)); // or: bug in resolve_index
      LLVMValueRef op = LLVMGetOperand(v, 0); // e.g. [3 x i64] [i64 1, i64 2, i64 3]
      asserteq_debug(LLVMTypeOf(op), seqty);
      return LLVMConstExtractValue(op, &index, 1);
    }
    // index is constant and LLVMBuildInBoundsGEP2 will build a constant GEP on v.
    indexval = LLVMConstInt(b->t_i32, index, /*signext*/false);
    // note: LLVMBuildExtractValue is NOT able to produce better code than GEP
    // in this case because it needs an intermediate load which in LLVM 13 produces
    // superfluous code.
  } else {
    // index was not resolved; it likely varies at runtime.
    // Note: indexing a tuple (impl as stuct) always has compile-time index.
    asserteq_debug(LLVMGetTypeKind(LLVMGetElementType(ty)), LLVMArrayTypeKind);
    Node* indexexpr = notnull(n->index.indexexpr);
    indexval = build_expr(b, indexexpr, dnamef(b, "%s.idx", vname));

    if (LLVMIsConstant(v) &&
        LLVMGetTypeKind(seqty) == LLVMArrayTypeKind &&
        LLVMIsConstant(indexval) &&
        LLVMIsAConstantInt(indexval) )
    {
      // use constant value
      // TODO: does this actually happen? The resolver pass should set index
      // if it is constant.
      u64 index2 = LLVMConstIntGetZExtValue(indexval);
      if (index2 <= 0xffffffff) {
        index = (u32)index2;
        u32 len = LLVMGetArrayLength(seqty);
        if (index < len) {
          LLVMValueRef op = LLVMGetOperand(v, 0); // e.g. [3 x i64] [i64 1, i64 2, i64 3]
          return LLVMConstExtractValue(op, &index, 1);
        } else {
          build_errf(b->build, NodePosSpan(indexexpr),
            "index %u out of bounds, accessing array of length %u", index, len);
        }
      }
    }
  }

  // GEP load: v[indexval]
  LLVMTypeRef elem_ty = seq_elem_type(seqty, index);
  LLVMValueRef indexv[2] = { b->v_i32_0, indexval };
  Value ep = LLVMBuildInBoundsGEP2(b->builder, seqty, v, indexv, 2, vname);
  return build_load(b, elem_ty, ep, vname);
}


static Value build_slice(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NSlice);
  notnull(n->type);

  panic("TODO");
  return NULL;
}


static Value build_prefix_op(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NPrefixOp);
  notnull(n->type);

  if (n->op.op == TStar)
    return build_deref(b, n->op.left, (Value*)&n->irval, vname);

  panic("TODO NPrefixOp %s", fmtnode(n));
  return NULL;
}


static Value build_intlit(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NIntLit);
  notnull(n->type);
  return n->irval = LLVMConstInt(get_type(b, n->type), n->val.i, /*signext*/false);
}


static Value build_floatlit(B* b, Node* n, const char* vname) {
  asserteq_debug(n->kind, NFloatLit);
  notnull(n->type);
  return n->irval = LLVMConstReal(get_type(b, n->type), n->val.f);
}


static Value build_expr(B* b, Node* n, const char* vname) {
  notnull(n);

  #ifdef DEBUG_BUILD_EXPR
    if (vname && vname[0]) {
      dlog_mod(b, "→ %s %s <%s> (\"%s\")",
        NodeKindName(n->kind), fmtnode(n), fmtnode(n->type), vname);
    } else {
      dlog_mod(b, "→ %s %s <%s>",
        NodeKindName(n->kind), fmtnode(n), fmtnode(n->type));
    }
    b->log_indent += 2;
    #define RET(x) {                                                  \
      Value v = x;                                                    \
      b->log_indent -= 2;                                             \
      dlog_mod(b, "← %s %s => %s",                                    \
        NodeKindName(n->kind), fmtnode(n), v ? fmtvalue(v) : "void"); \
      return v; }
  #else
    #define RET(x) R_MUSTTAIL return x
  #endif

  switch (n->kind) {
    case NArray:      RET(build_array(b, n, vname));
    case NAssign:     RET(build_assign(b, n, vname));
    case NBinOp:      RET(build_bin_op(b, n, vname));
    case NBlock:      RET(build_block(b, n, vname));
    case NCall:       RET(build_call(b, n, vname));
    case NField:      RET(build_field(b, n, vname));
    case NFloatLit:   RET(build_floatlit(b, n, vname));
    case NFun:        RET(build_fun(b, n, vname));
    case NId:         RET(build_id_read(b, n, vname));
    case NIf:         RET(build_if(b, n, vname));
    case NIndex:      RET(build_index(b, n, vname));
    case NIntLit:     RET(build_intlit(b, n, vname));
    case NNamedVal:   RET(build_namedval(b, n, vname));
    case NPrefixOp:   RET(build_prefix_op(b, n, vname));
    case NRef:        RET(build_ref(b, n, vname));
    case NReturn:     RET(build_return(b, n, vname));
    case NSelector:   RET(build_selector(b, n, vname));
    case NSlice:      RET(build_slice(b, n, vname));
    case NStructType: RET(build_struct_type_expr(b, n, vname));
    case NTuple:      RET(build_tuple(b, n, vname));
    case NTypeCast:   RET(build_typecast(b, n, vname));
    case NVar:        RET(build_var(b, n, vname));
    default:
      panic("TODO node kind %s", NodeKindName(n->kind));
      break;
  }

  panic("invalid node kind %s", NodeKindName(n->kind));

  #ifdef DEBUG_BUILD_EXPR
  b->log_indent--;
  #endif

  return NULL;
}


static void build_file(B* b, Node* n) {
  asserteq(n->kind, NFile);

  // TODO: set cat(filenames) instead of just the last file
  LLVMSetSourceFileName(b->mod, n->cunit.name, (size_t)str_len(n->cunit.name));

  // first build all globals
  for (u32 i = 0; i < n->cunit.a.len; i++) {
    auto cn = (Node*)n->cunit.a.v[i];
    if (cn->kind == NVar)
      build_global_var(b, cn);
  }

  // then functions
  for (u32 i = 0; i < n->cunit.a.len; i++) {
    auto cn = (Node*)n->cunit.a.v[i];
    switch (cn->kind) {
      case NFun:
        assertnotnull(cn->fun.name);
        build_fun(b, cn, cn->fun.name);
        break;
      case NVar:
        break;
      default:
        panic("TODO: %s", NodeKindName(cn->kind));
        break;
    }
  }
}


static void build_module(Build* build, Node* pkgnode, LLVMModuleRef mod) {
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  B _b = {
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
  if (build->sint_type == TypeCode_i32) {
    _b.t_int = _b.t_i32; // alias int = i32
  } else {
    _b.t_int = _b.t_i64; // alias int = i64
  }
  _b.t_i8ptr = LLVMPointerType(_b.t_i8, 0);
  _b.t_i32ptr = LLVMPointerType(_b.t_i32, 0);
  _b.v_i32_0 = LLVMConstInt(_b.t_i32, 0, /*signext*/false);

  #ifdef DEBUG_BUILD_EXPR
  if (kSpaces[0] == 0)
    memset(kSpaces, ' ', sizeof(kSpaces));
  #endif

  B* b = &_b;
  SymMapInit(&b->internedTypes, 16, build->mem);
  PtrMapInit(&b->defaultInits, 16, build->mem);

  // initialize function pass manager (optimize)
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

  // build package parts
  for (u32 i = 0; i < pkgnode->cunit.a.len; i++) {
    auto cn = (Node*)pkgnode->cunit.a.v[i];
    build_file(b, cn);
  }

  // // build demo functions
  // build_fun1(b, "foo");
  // build_fun1(b, "main");

  // verify IR
  #ifdef DEBUG
    char* errmsg;
    bool ok = LLVMVerifyModule(b->mod, LLVMPrintMessageAction, &errmsg) == 0;
    if (!ok) {
      //errlog("=========== LLVMVerifyModule ===========\n%s\n", errmsg);
      LLVMDisposeMessage(errmsg);
      dlog("\n=========== LLVMDumpModule ===========");
      LLVMDumpModule(b->mod);
      goto finish;
    }
  #endif

  // finalize all function passes scheduled in the function pass
  if (b->FPM)
    LLVMFinalizeFunctionPassManager(b->FPM);

  #ifdef DEBUG
  dlog("LLVM IR module as built:");
  LLVMDumpModule(b->mod);
  #endif

#ifdef DEBUG
finish:
#endif
  SymMapDispose(&b->internedTypes);
  PtrMapDispose(&b->defaultInits);
  if (b->FPM)
    LLVMDisposePassManager(b->FPM);
  LLVMDisposeBuilder(b->builder);
}


static LLVMTargetRef select_target(const char* triple) {
  // select target
  char* errmsg;
  LLVMTargetRef target;
  if (LLVMGetTargetFromTriple(triple, &target, &errmsg) != 0) {
    // error
    errlog("LLVMGetTargetFromTriple: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    target = NULL;
  } else {
    #if DEBUG
    const char* name = LLVMGetTargetName(target);
    const char* description = LLVMGetTargetDescription(target);
    const char* jit = LLVMTargetHasJIT(target) ? " jit" : "";
    const char* mc = LLVMTargetHasTargetMachine(target) ? " mc" : "";
    const char* _asm = LLVMTargetHasAsmBackend(target) ? " asm" : "";
    dlog("selected target: %s (%s) [abilities:%s%s%s]", name, description, jit, mc, _asm);
    #endif
  }
  return target;
}


static LLVMTargetMachineRef select_target_machine(
  LLVMTargetRef       target,
  const char*         triple,
  LLVMCodeGenOptLevel optLevel,
  LLVMCodeModel       codeModel)
{
  if (!target)
    return NULL;

  const char* CPU = "";      // "" for generic
  const char* features = ""; // "" for none

  // select host CPU and features (NOT PORTABLE!) when optimizing
  char* hostCPUName = NULL;
  char* hostFeatures = NULL;
  if (optLevel != LLVMCodeGenLevelNone) {
    hostCPUName = LLVMGetHostCPUName();
    hostFeatures = LLVMGetHostCPUFeatures();
    CPU = hostCPUName;
    features = hostFeatures;
  }

  LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
    target, triple, CPU, features, optLevel, LLVMRelocStatic, codeModel);
  if (!targetMachine) {
    errlog("LLVMCreateTargetMachine failed");
    return NULL;
  } else {
    char* triple1 = LLVMGetTargetMachineTriple(targetMachine);
    dlog("selected target machine: %s", triple1);
    LLVMDisposeMessage(triple1);
  }
  if (hostCPUName) {
    LLVMDisposeMessage(hostCPUName);
    LLVMDisposeMessage(hostFeatures);
  }
  return targetMachine;
}


static LLVMOrcThreadSafeModuleRef llvm_jit_buildmod(Build* build, Node* pkgnode) {
  RTIMER_INIT;

  LLVMOrcThreadSafeContextRef tsctx = LLVMOrcCreateNewThreadSafeContext();
  LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(tsctx);
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext(build->pkg->id, ctx);

  // build module; Co AST -> LLVM IR
  // TODO: consider moving the IR building code to C++
  RTIMER_START();
  build_module(build, pkgnode, M);
  RTIMER_LOG("build llvm IR");

  // Wrap the module and our ThreadSafeContext in a ThreadSafeModule.
  // Dispose of our local ThreadSafeContext value.
  // The underlying LLVMContext will be kept alive by our ThreadSafeModule, TSM.
  LLVMOrcThreadSafeModuleRef TSM = LLVMOrcCreateNewThreadSafeModule(M, tsctx);
  LLVMOrcDisposeThreadSafeContext(tsctx);
  return TSM;
}


static int llvm_jit_handle_err(LLVMErrorRef Err) {
  char* errmsg = LLVMGetErrorMessage(Err);
  fprintf(stderr, "LLVM JIT error: %s\n", errmsg);
  LLVMDisposeErrorMessage(errmsg);
  return 1;
}


int llvm_jit(Build* build, Node* pkgnode) {
  dlog("llvm_jit");
  RTIMER_INIT;
  // TODO: see llvm/examples/OrcV2Examples/LLJITWithObjectCache/LLJITWithObjectCache.cpp
  // for an example of caching compiled code objects, like LLVM IR modules.

  int main_result = 0;
  LLVMErrorRef err;

  RTIMER_START();

  // Initialize native target codegen and asm printer
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  // Create the JIT instance
  LLVMOrcLLJITRef J;
  if ((err = LLVMOrcCreateLLJIT(&J, 0))) {
    main_result = llvm_jit_handle_err(err);
    goto llvm_shutdown;
  }
  RTIMER_LOG("llvm JIT init");


  // build module
  LLVMOrcThreadSafeModuleRef M = llvm_jit_buildmod(build, pkgnode);
  LLVMOrcResourceTrackerRef RT;


  // // get execution session
  // LLVMOrcExecutionSessionRef ES = LLVMOrcLLJITGetExecutionSession(J);
  // LLVMOrcObjectLayerRef objlayer =
  //   LLVMOrcCreateRTDyldObjectLinkingLayerWithSectionMemoryManager(ES);


  // Add our module to the JIT
  LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    RT = LLVMOrcJITDylibCreateResourceTracker(MainJD);
  if ((err = LLVMOrcLLJITAddLLVMIRModuleWithRT(J, RT, M))) {
    // If adding the ThreadSafeModule fails then we need to clean it up
    // ourselves. If adding it succeeds the JIT will manage the memory.
    LLVMOrcDisposeThreadSafeModule(M);
    main_result = llvm_jit_handle_err(err);
    goto jit_cleanup;
  }

  // Look up the address of our entry point
  RTIMER_START();
  LLVMOrcJITTargetAddress entry_addr;
  if ((err = LLVMOrcLLJITLookup(J, &entry_addr, "main"))) {
    main_result = llvm_jit_handle_err(err);
    goto mod_cleanup;
  }
  RTIMER_LOG("llvm JIT lookup entry function \"main\"");


  // If we made it here then everything succeeded. Execute our JIT'd code.
  RTIMER_START();
  auto entry_fun = (long(*)(long))entry_addr;
  long result = entry_fun(123l);
  RTIMER_LOG("llvm JIT execute module main fun");
  fprintf(stderr, "main => %ld\n", result);


  RTIMER_START();

mod_cleanup:
  // Remove the code
  if ((err = LLVMOrcResourceTrackerRemove(RT))) {
    main_result = llvm_jit_handle_err(err);
    goto jit_cleanup;
  }

  // Attempt a second lookup — we expect an error as the code & symbols have been removed
  #if DEBUG
  LLVMOrcJITTargetAddress tmp;
  if ((err = LLVMOrcLLJITLookup(J, &tmp, "main")) != 0) {
    // expect error
    LLVMDisposeErrorMessage(LLVMGetErrorMessage(err)); // must release error message
  } else {
    assert(err != 0); // expected error
  }
  #endif

jit_cleanup:
  // Destroy our JIT instance. This will clean up any memory that the JIT has
  // taken ownership of. This operation is non-trivial (e.g. it may need to
  // JIT static destructors) and may also fail. In that case we want to render
  // the error to stderr, but not overwrite any existing return value.
  LLVMOrcReleaseResourceTracker(RT);
  if ((err = LLVMOrcDisposeLLJIT(J))) {
    int x = llvm_jit_handle_err(err);
    if (main_result == 0)
      main_result = x;
  }
  // LLVMOrcDisposeObjectLayer(objlayer);

llvm_shutdown:
  // Shut down LLVM.
  LLVMShutdown();
  RTIMER_LOG("llvm JIT cleanup");
  return main_result;
}


bool llvm_build_and_emit(Build* build, Node* pkgnode, const char* triple) {
  dlog("llvm_build_and_emit");
  bool ok = false;
  RTIMER_INIT;

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(build->pkg->id, ctx);


  // build module; Co AST -> LLVM IR
  // TODO: move the IR building code to C++
  RTIMER_START();
  build_module(build, pkgnode, mod);
  RTIMER_LOG("build llvm IR");


  // select target and emit machine code
  RTIMER_START();
  const char* hostTriple = llvm_init_targets();
  if (!triple)
    triple = hostTriple; // default to host
  LLVMTargetRef target = select_target(triple);
  LLVMCodeGenOptLevel optLevel =
    (build->opt == CoOptNone ? LLVMCodeGenLevelNone : LLVMCodeGenLevelDefault);
  LLVMCodeModel codeModel =
    (build->opt == CoOptSmall ? LLVMCodeModelSmall : LLVMCodeModelDefault);

  // optLevel = LLVMCodeGenLevelAggressive;
  LLVMTargetMachineRef targetm = select_target_machine(
    target, triple, optLevel, codeModel);
  if (!targetm)
    goto end;

  // set target
  LLVMSetTarget(mod, triple);
  LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetm);
  LLVMSetModuleDataLayout(mod, dataLayout);
  RTIMER_LOG("select llvm target");


  char* errmsg;

  // verify, optimize and target-fit module
  RTIMER_START();
  bool enable_tsan = false;
  bool enable_lto = false;
  if (!llvm_optmod(mod, targetm, build->opt, enable_tsan, enable_lto, &errmsg)) {
    errlog("llvm_optmod: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    goto end;
  }
  RTIMER_LOG("llvm optimize module");
  #ifdef DEBUG
  dlog("LLVM IR module after target-fit and optimizations:");
  LLVMDumpModule(mod);
  #endif


  // emit
  const char* obj_file = "out1.o";
  const char* asm_file = "out1.s";
  const char* bc_file  = "out1.bc";
  const char* ir_file  = "out1.ll";
  const char* exe_file = "out1.exe";

  // emit machine code (object)
  if (obj_file) {
    RTIMER_START();
    if (!llvm_emit_mc(mod, targetm, LLVMObjectFile, obj_file, &errmsg)) {
      errlog("llvm_emit_mc (LLVMObjectFile): %s", errmsg);
      LLVMDisposeMessage(errmsg);
      // obj_file = NULL; // skip linking
      goto end;
    }
    RTIMER_LOG("llvm codegen MC object %s", obj_file);
  }

  // emit machine code (assembly)
  if (asm_file) {
    RTIMER_START();
    if (!llvm_emit_mc(mod, targetm, LLVMAssemblyFile, asm_file, &errmsg)) {
      errlog("llvm_emit_mc (LLVMAssemblyFile): %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    }
    RTIMER_LOG("llvm codegen MC assembly %s", asm_file);
  }

  // emit LLVM bitcode
  if (bc_file) {
    RTIMER_START();
    if (!llvm_emit_bc(mod, bc_file, &errmsg)) {
      errlog("llvm_emit_bc: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    }
    RTIMER_LOG("llvm codegen LLVM bitcode %s", bc_file);
  }

  // emit LLVM IR
  if (ir_file) {
    RTIMER_START();
    if (!llvm_emit_ir(mod, ir_file, &errmsg)) {
      errlog("llvm_emit_ir: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    }
    RTIMER_LOG("llvm codegen LLVM IR text %s", ir_file);
  }

  // link executable
  if (exe_file && obj_file) {
    RTIMER_START();
    const char* inputv[] = { obj_file };
    CoLLDOptions lldopt = {
      .targetTriple = triple,
      .opt = build->opt,
      .outfile = exe_file,
      .infilec = countof(inputv),
      .infilev = inputv,
    };
    if (!lld_link(&lldopt, &errmsg)) {
      errlog("lld_link: %s", errmsg);
      goto end;
    }
    RTIMER_LOG("lld link executable %s", exe_file);

    // print warnings
    if (strlen(errmsg) > 0)
      fwrite(errmsg, strlen(errmsg), 1, stderr);
    LLVMDisposeMessage(errmsg);
  }


  // if we get here, without "goto end", all succeeded
  ok = true;

end:
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  return ok;
}

#if 0
__attribute__((constructor,used)) static void llvm_init() {
  Pkg pkg = {
    .dir  = ".",
    .id   = "foo/bar",
    .name = "bar",
  };
  Build build = {
    .pkg = &pkg,
    .opt = CoOptAggressive,
  };
  if (!llvm_build_and_emit(&build, /*target=host*/NULL)) {
    //
  }
  // exit(0);
}
#endif
