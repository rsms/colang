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
#define DEBUG_BUILD_EXPR

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


// make the code more readable by using short name aliases
typedef LLVMValueRef  Value;

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
  #endif

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // build state
  bool   noload;        // for NVar
  bool   mutable;       // true if inside mutable data context
  u32    fnest;         // function nest depth
  Value  varalloc;      // memory preallocated for a var's init
  SymMap internedTypes; // AST types, keyed by typeid
  PtrMap defaultInits;  // constant initializers (LLVMTypeRef => Value)

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
  LLVMTypeRef t_size;

} B;

typedef enum {
  Immutable,
  Mutable,
} Mutability;


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


static Value store(B* b, Value v, Value ptr) {
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


static void store_or_copy(B* b, Value v, Value ptr) {
  if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind) {
    assert(LLVMTypeOf(ptr) == LLVMTypeOf(v));
    u32 dst_align = 4; // TODO
    u32 src_align = 4; // TODO
    LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(ptr));
    LLVMBuildMemCpy(b->builder, ptr, dst_align, v, src_align, LLVMSizeOf(ty));
  } else {
    store(b, v, ptr);
  }
}


static LLVMTypeRef get_struct_type(B* b, Type* tn);
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
      return get_struct_type(b, n);
    case NArrayType:
      return get_array_type(b, n);
    default:
      panic("TODO node kind %s", NodeKindName(n->kind));
      break;
  }
  panic("invalid node kind %s", NodeKindName(n->kind));
  return NULL;
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


static Value build_expr(B* b, Node* n, const char* debugname);


// build_expr_noload calls build_expr with b->noload set to true, ignoring result value
inline static Value nullable build_expr_noload(B* b, Node* n, const char* debugname) {
  bool noload = b->noload; // save
  b->noload = true;
  build_expr(b, n, debugname);
  b->noload = noload; // restore
  return n->irval;
}

inline static Value build_expr_mustload(B* b, Node* n, const char* debugname) {
  bool noload = b->noload; // save
  b->noload = false;
  Value v = build_expr(b, n, debugname);
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


static Value build_fun(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NFun);
  assertnotnull_debug(n->type);
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
      if (NodeIsConst(pn)) { // immutable
        pn->irval = pv;
      } else { // mutable
        LLVMTypeRef ty = get_type(b, pn->type);
        pn->irval = LLVMBuildAlloca(b->builder, ty, pn->var.name);
        store(b, pv, pn->irval);
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

  // restore any current builder position
  if (prevb)
    LLVMPositionBuilderAtEnd(b->builder, prevb);

  b->fnest--;

  return fn;
}


static Value build_block(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NBlock);
  assertnotnull_debug(n->type);

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


static Value build_struct_cons(B* b, Node* n, const char* debugname);


static Value build_type_call(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NCall);
  assertnotnull_debug(n->call.receiver->type);
  asserteq_debug(n->call.receiver->type->kind, NTypeType);

  // type call, e.g. str(1), MyStruct(x, y), etc.
  Type* tn = assertnotnull_debug(n->call.receiver->type->t.type);

  switch (tn->kind) {
    case NStructType:
      return build_struct_cons(b, n, debugname);
    default:
      panic("TODO: type call %s", fmtnode(tn));
      return NULL;
  }
}


static Value build_fun_call(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NCall);
  assertnotnull_debug(n->call.receiver->type);
  asserteq_debug(n->call.receiver->type->kind, NFunType);

  // n->call.receiver->kind==NFun
  Value callee = build_expr(b, n->call.receiver, "callee");
  if (!callee) {
    errlog("unknown function");
    return NULL;
  }

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

  asserteq(LLVMCountParams(callee), argc);
  Value v = LLVMBuildCall(b->builder, callee, argv, argc, "");

  // LLVMSetTailCall(v, true); // set tail call when we know it for sure

  STK_ARRAY_DISPOSE(argv);
  return v;
}


static Value build_call(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NCall);
  assertnotnull_debug(n->type);

  Type* recvt = assertnotnull_debug(n->call.receiver->type);
  switch (recvt->kind) {
    case NTypeType: // type constructor call
      return build_type_call(b, n, debugname);
    case NFunType: // function call
      return build_fun_call(b, n, debugname);
    default:
      panic("invalid call kind=%s n=%s", NodeKindName(recvt->kind), fmtnode(n));
      return NULL;
  }
}


static Value build_init_store(B* b, Node* n, Value init, const char* debugname) {
  if (!LLVMGetInsertBlock(b->builder) /*|| !b->mutable*/) {
    // global
    Value ptr = LLVMAddGlobal(b->mod, LLVMTypeOf(init), debugname);
    LLVMSetLinkage(ptr, LLVMPrivateLinkage);
    LLVMSetInitializer(ptr, init);
    LLVMSetGlobalConstant(ptr, LLVMIsConstant(init));
    // LLVMSetUnnamedAddr(ptr, true);
    return ptr;
  }
  // mutable, on stack
  Value ptr = LLVMBuildAlloca(b->builder, LLVMTypeOf(init), debugname);
  store(b, init, ptr);
  return ptr;
}


static Value nullable set_varalloca(B* b, Value v) {
  Value outer = b->varalloc;
  b->varalloc = v;
  dlog_mod(b, "set varalloc %s", fmtvalue(b->varalloc));
  return outer;
}


static Value take_varalloca(B* b, LLVMTypeRef ty) {
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


static Value build_array(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NArray);
  Type* arrayt = assertnotnull_debug(n->type);

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
      store(b, init, ptr);
    } else {
      // store as global
      ptr = LLVMAddGlobal(b->mod, arrayty, debugname);
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
      LLVMConstInt(b->t_i32, 0, /*signext*/false),
      LLVMConstInt(b->t_i32, arrayt->t.array.size, /*signext*/false),
    };
    for (u32 i = initv ? 1 : 0; i < valuec; i++) {
      if (valuev[i] != initv) {
        Value eptr = LLVMBuildInBoundsGEP2(b->builder, arrayty, ptr, gepindexv, 2, "");
        store(b, valuev[i], eptr);
      }
    }
    n->irval = ptr;
  }

  STK_ARRAY_DISPOSE(valuev);

  // n->irval = build_init_store(b, n, init, debugname);
  return n->irval;
}


static Value build_typecast(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NTypeCast);
  assertnotnull_debug(n->type);
  assertnotnull_debug(n->call.args);

  panic("TODO");

  LLVMBool isSigned = false;
  LLVMTypeRef dsttype = b->t_i32;
  LLVMValueRef srcval = build_expr(b, n->call.args, "");
  return LLVMBuildIntCast2(b->builder, srcval, dsttype, isSigned, debugname);
}


static Value build_return(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NReturn);
  assertnotnull_debug(n->type);
  // TODO: check current function and if type is nil, use LLVMBuildRetVoid
  LLVMValueRef v = build_expr(b, n->op.left, debugname);
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


static Value build_struct_type_expr(B* b, Type* n, const char* debugname) {
  LLVMTypeRef ty = get_struct_type(b, n);
  dlog_mod(b, "build_struct_type_expr %s", fmttype(ty));

  if ((n->flags & NodeFlagRValue) && !b->noload) {
    // struct type used as value
    panic("TODO: build type value %s", fmttype(ty));
  }

  return NULL;
}


static Value build_anon_struct(
  B* b, Value* values, u32 numvalues, const char* debugname, Mutability mut)
{
  u32 nconst = 0;
  for (u32 i = 0; i < numvalues; i++)
    nconst += LLVMIsConstant(values[i]);

  if (nconst == numvalues) {
    // all values are constant
    Value init = LLVMConstStructInContext(b->ctx, values, numvalues, /*packed*/false);
    if (mut == Mutable) {
      // struct will be modified; allocate on stack
      Value ptr = LLVMBuildAlloca(b->builder, LLVMTypeOf(init), debugname);
      store(b, init, ptr);
      return ptr;
    } else {
      // struct is read-only; allocate as global
      LLVMValueRef ptr = LLVMAddGlobal(b->mod, LLVMTypeOf(init), debugname);
      LLVMSetLinkage(ptr, LLVMPrivateLinkage);
      LLVMSetInitializer(ptr, init);
      LLVMSetGlobalConstant(ptr, true);
      LLVMSetUnnamedAddr(ptr, true);

      // LLVMValueRef args[1];
      // args[0] = LLVMConstInt(b->t_i32, 0, false);
      // return LLVMConstInBoundsGEP(ptr, args, 1);
      return ptr;
    }
  }

  STK_ARRAY_DEFINE(typesv, LLVMTypeRef, 16);
  STK_ARRAY_INIT(typesv, b->build->mem, numvalues);
  for (u32 i = 0; i < numvalues; i++) {
    typesv[i] = LLVMTypeOf(values[i]);
  }

  LLVMTypeRef ty = LLVMStructTypeInContext(b->ctx, typesv, numvalues, /*packed*/false);
  Value ptr = LLVMBuildAlloca(b->builder, ty, debugname);

  for (u32 i = 0; i < numvalues; i++) {
    LLVMValueRef fieldptr = LLVMBuildStructGEP2(b->builder, ty, ptr, i, "");
    store(b, values[i], fieldptr);
  }

  STK_ARRAY_DISPOSE(typesv);
  return ptr;
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


static Value build_field(B* b, Node* n, const char* debugname) {
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


static Value build_struct_cons(B* b, Node* n, const char* debugname) {
  // called by build_type_call
  asserteq_debug(n->kind, NCall);

  Type* recvt = assertnotnull_debug(n->call.receiver->type);
  asserteq_debug(recvt->kind, NTypeType);

  Type* structType = assertnotnull_debug(recvt->t.type);
  asserteq_debug(structType->kind, NStructType);

  LLVMTypeRef ty = get_struct_type(b, structType);
  Value init = build_struct_init(b, structType, n->call.args, ty);
  n->irval = build_init_store(b, n, init, debugname);
  return n->irval;
}


static Value build_selector(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NSelector);
  assertnotnull_debug(n->type);
  assert_debug(n->sel.indices.len > 0); // GEP path should be resolved by type resolver

  // #ifdef DEBUG
  // fprintf(stderr, "[%s] GEP index path:", __func__);
  // for (u32 i = 0; i < n->sel.indices.len; i++) {
  //   fprintf(stderr, " %u", n->sel.indices.v[i]);
  // }
  // fprintf(stderr, "\n");
  // #endif

  Value ptr = assertnotnull_debug(build_expr_noload(b, n->sel.operand, debugname));
  LLVMTypeRef st_ty = LLVMGetElementType(LLVMTypeOf(ptr));

  u32 nindices = n->sel.indices.len + 1; // first index is for pointer array/offset
  STK_ARRAY_MAKE(indices, b->build->mem, LLVMValueRef, 16, nindices);
  indices[0] = LLVMConstInt(b->t_i32, 0, /*signext*/false);
  for (u32 i = 0; i < n->sel.indices.len; i++) {
    indices[i + 1] = LLVMConstInt(b->t_i32, n->sel.indices.v[i], /*signext*/false);
  }

  // note: llvm kindly coalesces consecutive GEPs, meaning that nested NSelector nodes
  // become a single GEP. e.g. "x.foo.bar" into nested struct "bar".

  n->irval = LLVMBuildInBoundsGEP2(
    b->builder, st_ty, ptr, indices, nindices, debugname);

  STK_ARRAY_DISPOSE(indices);

  if (b->noload)
    return n->irval;

  LLVMTypeRef elem_ty = LLVMGetElementType(LLVMTypeOf(n->irval));
  return LLVMBuildLoad2(b->builder, elem_ty, n->irval, debugname);
}


// gep_load loads the value of field at index from sequence at memory location ptr
static Value gep_load(B* b, Value v, u32 index, const char* debugname) {
  LLVMTypeRef vty = LLVMTypeOf(assertnotnull_debug(v));
  LLVMTypeKind tykind = LLVMGetTypeKind(vty);

  switch (tykind) {
    case LLVMArrayTypeKind:
      return LLVMGetElementAsConstant(v, index);
    case LLVMPointerTypeKind:
      break;
    default:
      panic("unexpected value type %s", fmttype(vty));
  }

  // v is a pointer: GEP
  LLVMTypeRef seqty = LLVMGetElementType(vty);
  LLVMTypeKind seqty_kind = LLVMGetTypeKind(seqty);

  assert_debug(seqty_kind == LLVMStructTypeKind || seqty_kind == LLVMArrayTypeKind);
  assert_debug(index <
    (seqty_kind == LLVMStructTypeKind ? LLVMCountStructElementTypes(seqty) :
     LLVMGetArrayLength(seqty)) );

  LLVMValueRef indexv[2] = {
    LLVMConstInt(b->t_i32, 0, /*signext*/false),
    LLVMConstInt(b->t_i32, index, /*signext*/false),
  };

  // "inbounds" — the result value of the GEP is undefined if the address is outside
  // the actual underlying allocated object and not the address one-past-the-end.
  Value elemptr = LLVMBuildInBoundsGEP2(b->builder, seqty, v, indexv, 2, debugname);

  LLVMTypeRef elem_ty = (
    seqty_kind == LLVMStructTypeKind ? LLVMStructGetTypeAtIndex(seqty, index) :
    LLVMGetElementType(seqty));

  return LLVMBuildLoad2(b->builder, elem_ty, elemptr, debugname);
}


static Value build_index(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIndex);
  assertnotnull_debug(n->type);
  assertnotnull_debug(n->index.index);

  Node* operand = assertnotnull_debug(n->index.operand);
  assert_debug(n->index.index->val.i <= 0xFFFFFFFF);
  u32 index = (u32)n->index.index->val.i;

  // debugname "operand.index"
  #ifdef DEBUG
  char debugname2[256];
  if (debugname[0] == 0 && operand->kind == NVar) {
    int len = snprintf(
      debugname2, sizeof(debugname2), "%s.%u", operand->var.name, index);
    if ((size_t)len >= sizeof(debugname2))
      debugname2[sizeof(debugname2) - 1] = '\0'; // truncate
    debugname = debugname2;
  }
  #endif

  Type* operandt = assertnotnull_debug(operand->type);

optype_switch:
  switch (operandt->kind) {
    case NRefType:
      operandt = operandt->t.ref;
      goto optype_switch;

    case NTupleType: {
      asserteq_debug(n->index.index->kind, NIntLit); // must be resolved const
      Value v = assertnotnull_debug(build_expr_noload(b, operand, debugname));
      return gep_load(b, v, index, debugname);
    }

    case NArrayType: {
      asserteq_debug(n->index.index->kind, NIntLit); // must be resolved const
      Value v = assertnotnull_debug(build_expr_noload(b, operand, debugname));
      return gep_load(b, v, index, debugname);
    }

    // case NStructType:

    // case NArrayType:
    // TODO: LLVMBuildGEP2
    // LLVMValueRef LLVMBuildGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
    //                          LLVMValueRef Pointer, LLVMValueRef *Indices,
    //                          unsigned NumIndices, const char *Name);

    default:
      panic("TODO: %s", NodeKindName(operand->type->kind));
  }

  return LLVMConstInt(b->t_int, 0, /*signext*/false); // placeholder
}


static Value load_var(B* b, Node* n, const char* debugname) {
  assert_debug(n->kind == NVar);
  Value v = (Value)assertnotnull_debug(n->irval);

  if (NodeIsConst(n) || b->noload)
    return v;

  assert_llvm_type_isptr(LLVMTypeOf(v));

  if (R_UNLIKELY(b->fnest == 0)) {
    // var load in global scope is the same as using its initializer
    return LLVMGetInitializer(v);
  }

  if (debugname[0] == 0)
    debugname = assertnotnull_debug(n->var.name);

  LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(v));
  dlog_mod(b, "load_var ptr (type %s => %s): %s",
    fmttype(LLVMTypeOf(v)), fmttype(ty), fmtvalue(v));
  return LLVMBuildLoad2(b->builder, ty, v, debugname);
}


static Value build_var_def(B* b, Node* n, const char* debugname, Value nullable init) {
  asserteq_debug(n->kind, NVar);
  assertnull_debug(n->irval);
  assert_debug( ! NodeIsParam(n)); // params are eagerly built by build_fun
  assertnotnull_debug(LLVMGetInsertBlock(b->builder)); // local, not global

  if (n->var.nrefs == 0 && !n->type) // skip unused var
    return NULL;

  assertnotnull_debug(n->type);

  if (debugname[0] == 0)
    debugname = n->var.name;

  bool noload = b->noload; // save
  b->noload = false;

  if (NodeIsConst(n)) {
    // immutable variable
    if (init) {
      n->irval = init;
    } else if (n->var.init) {
      n->irval = build_expr(b, n->var.init, debugname);
    } else {
      n->irval = build_default_value(b, n->type);
    }
  } else {
    // mutable variable
    // See https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html
    LLVMTypeRef ty = get_type(b, n->type);
    n->irval = LLVMBuildAlloca(b->builder, ty, debugname);

    bool store = true;

    if (init || n->var.init) {
      if (!init) {
        Value outer_varalloca = set_varalloca(b, n->irval);
        init = build_expr(b, n->var.init, debugname);
        if (b->varalloc == NULL) {
          // varalloc used -- skip store of init
          store = false;
        }
        b->varalloc = outer_varalloca; // restore
      }
    } else {
      // zero initialize
      init = LLVMConstNull(ty);
    }

    if (store)
      store_or_copy(b, init, n->irval);
  }

  b->noload = noload; // restore

  return (Value)n->irval;
}


static Value build_var(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NVar);

  // build var if needed
  if (!n->irval) {
    bool mutable = b->mutable; b->mutable = !NodeIsConst(n); // push "mutable"
    build_var_def(b, n, debugname, NULL);
    b->mutable = mutable; // pop "mutable"
  }

  return load_var(b, n, debugname);
}


static Value build_global_var(B* b, Node* n) {
  assert(n->kind == NVar);
  assert(n->type);

  if (NodeIsConst(n) && n->var.init && NodeIsType(n->var.init))
    return NULL;

  Value init;
  if (n->var.init) {
    init = build_expr(b, n->var.init, n->var.name);
    assertnotnull_debug(init);
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


static Value build_id_read(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NId);
  assertnotnull_debug(n->id.target); // should be resolved

  Value v = build_expr(b, n->id.target, n->id.name);
  n->irval = n->id.target->irval;
  return v;
}


static Value build_assign_var(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->op.left->kind, NVar);

  const char* name = n->op.left->var.name;
  Value ptr = build_expr_noload(b, n->op.left, name);

  Value right = build_expr_noload(b, n->op.right, "rvalue");
  if (LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind) {
    u32 dst_align = LLVMGetAlignment(ptr);
    u32 src_align = dst_align;
    LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(ptr));
    LLVMBuildMemCpy(b->builder, ptr, dst_align, right, src_align, LLVMSizeOf(ty));
  } else {
    store(b, right, ptr);
  }

  // value of assignment is its new value
  if ((n->flags & NodeFlagRValue) && !b->noload) {
    LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(ptr));
    return LLVMBuildLoad2(b->builder, ty, ptr, name);
  }

  return NULL;
}


static Value build_assign_tuple(B* b, Node* n, const char* debugname) {
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
      build_var_def(b, dstn, dstn->var.name, NULL);
      srcvalsv[i] = load_var(b, dstn, dstn->var.name);
    }
    assertnotnull_debug(srcvalsv[i]);
  }

  // now store
  for (u32 i = 0; i < srcvalsc; i++) {
    Node* srcn = sources->array.a.v[i];
    Node* dstn = targets->array.a.v[i];

    if (srcn) {
      // assignment to existing memory location
      if (dstn->kind != NVar)
        panic("TODO: dstn %s", NodeKindName(dstn->kind));
      Value ptr = assertnotnull_debug(build_expr_noload(b, dstn, dstn->var.name));
      store(b, srcvalsv[i], ptr);
    }
  }

  Value result = NULL;

  // if the assignment is used as a value, make tuple val
  if ((n->flags & NodeFlagRValue) && !b->noload) {
    for (u32 i = 0; i < srcvalsc; i++) {
      Node* dstn = targets->array.a.v[i];
      if (dstn->kind != NVar)
        panic("TODO: dstn %s", NodeKindName(dstn->kind));
      srcvalsv[i] = load_var(b, dstn, dstn->var.name);
    }
    result = build_anon_struct(b, srcvalsv, srcvalsc, debugname, Immutable);
  }

  STK_ARRAY_DISPOSE(srcvalsv);
  return result;
}


static Value build_assign_index(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->op.left->kind, NIndex);

  Node* target = n->op.left->index.operand; // i.e. "x" in "x[3] = y"
  Node* index = n->op.left->index.index;    // i.e. "3" in "x[3] = y"
  Node* source = n->op.right;               // i.e. "y" in "x[3] = y"

  Value indexval = build_expr_mustload(b, index, "");
  Value srcval = build_expr_mustload(b, source, "");

  Type* targett = assertnotnull_debug(target->type);
  assert_debug(targett->kind == NArrayType || targett->kind == NTupleType);

  if (targett->kind == NArrayType) {
    assert_debug(TypeEquals(b->build, targett->t.array.subtype, source->type));
    Value arrayptr = assertnotnull_debug(build_expr_noload(b, target, debugname));
    assert_llvm_type_isptr(LLVMTypeOf(arrayptr));

    LLVMValueRef indexv[2] = {
      LLVMConstInt(b->t_i32, 0, /*signext*/false),
      indexval,
    };

    // "inbounds" — the result value of the GEP is undefined if the address is outside
    // the actual underlying allocated object and not the address one-past-the-end.
    LLVMTypeRef arrayty = LLVMGetElementType(LLVMTypeOf(arrayptr));
    Value elemptr = LLVMBuildInBoundsGEP2(
      b->builder, arrayty, arrayptr, indexv, 2, debugname);

    store(b, srcval, elemptr);
    n->irval = srcval;
    return srcval; // value of assignment expression is its new value
  }

  panic("TODO tuple");
  return NULL;
}


static Value build_assign(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NAssign);
  assertnotnull_debug(n->type);

  switch (n->op.left->kind) {
    case NVar:   R_MUSTTAIL return build_assign_var(b, n, debugname);
    case NTuple: R_MUSTTAIL return build_assign_tuple(b, n, debugname);
    case NIndex: R_MUSTTAIL return build_assign_index(b, n, debugname);
    default:
      panic("TODO assign to %s", NodeKindName(n->op.left->kind));
      return NULL;
  }
}


static Value build_binop(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NBinOp);
  assertnotnull_debug(n->type);

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
      return LLVMBuildFCmp(b->builder, (LLVMRealPredicate)op, left, right, debugname);
    return LLVMBuildICmp(b->builder, (LLVMIntPredicate)op, left, right, debugname);
  }
  return LLVMBuildBinOp(b->builder, (LLVMOpcode)op, left, right, debugname);
}


static Value build_if(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIf);
  assertnotnull_debug(n->type);

  bool isrvalue = (n->flags & NodeFlagRValue) && !b->noload;

  // condition
  assertnotnull_debug(n->cond.cond->type);
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


static Value build_namedval(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NNamedVal);
  Value v = build_expr(b, n->namedval.value, n->namedval.name);
  n->irval = n->namedval.value->irval;
  return v;
}


static Value build_ref(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NRef);
  n->irval = build_expr_noload(b, n->ref.target, debugname);
  return n->irval;
}


static Value build_intlit(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIntLit);
  assertnotnull_debug(n->type);
  return n->irval = LLVMConstInt(get_type(b, n->type), n->val.i, /*signext*/false);
}


static Value build_floatlit(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NFloatLit);
  assertnotnull_debug(n->type);
  return n->irval = LLVMConstReal(get_type(b, n->type), n->val.f);
}


static Value build_expr(B* b, Node* n, const char* debugname) {
  assertnotnull_debug(n);

  #ifdef DEBUG_BUILD_EXPR
    if (debugname && debugname[0]) {
      dlog_mod(b, "→ %s %s <%s> (\"%s\")",
        NodeKindName(n->kind), fmtnode(n), fmtnode(n->type), debugname);
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
    case NArray:      RET(build_array(b, n, debugname));
    case NAssign:     RET(build_assign(b, n, debugname));
    case NBinOp:      RET(build_binop(b, n, debugname));
    case NBlock:      RET(build_block(b, n, debugname));
    case NCall:       RET(build_call(b, n, debugname));
    case NField:      RET(build_field(b, n, debugname));
    case NFloatLit:   RET(build_floatlit(b, n, debugname));
    case NFun:        RET(build_fun(b, n, debugname));
    case NId:         RET(build_id_read(b, n, debugname));
    case NIf:         RET(build_if(b, n, debugname));
    case NIndex:      RET(build_index(b, n, debugname));
    case NIntLit:     RET(build_intlit(b, n, debugname));
    case NNamedVal:   RET(build_namedval(b, n, debugname));
    case NReturn:     RET(build_return(b, n, debugname));
    case NSelector:   RET(build_selector(b, n, debugname));
    case NStructType: RET(build_struct_type_expr(b, n, debugname));
    case NTypeCast:   RET(build_typecast(b, n, debugname));
    case NVar:        RET(build_var(b, n, debugname));
    case NRef:        RET(build_ref(b, n, debugname));
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
  };
  _b.t_int = _b.t_i32; // alias int = i32
  _b.t_size = _b.t_i64; // alias size = i32

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
  auto entry_fun = (int(*)(void))entry_addr;
  int result = entry_fun();
  RTIMER_LOG("llvm JIT execute module main fun");
  fprintf(stderr, "main => %i\n", result);


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
  const char* asm_file = "out1.asm";
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
