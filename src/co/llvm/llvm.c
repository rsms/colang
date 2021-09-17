#include "../common.h"
#include "../parse/parse.h"
#include "../util/rtimer.h"
#include "llvm.h"

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/OrcEE.h>

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

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // AST types, keyed by typeid
  SymMap internedTypes;

  // type constants
  LLVMTypeRef t_void;
  LLVMTypeRef t_bool;
  LLVMTypeRef t_i8;
  LLVMTypeRef t_i16;
  LLVMTypeRef t_i32;
  LLVMTypeRef t_i64;
  LLVMTypeRef t_f32;
  LLVMTypeRef t_f64;

  LLVMTypeRef t_int;

} B;


static LLVMTypeRef get_struct_type(B* b, Type* tn);


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

static bool value_is_call(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMCall;
}

static Value get_current_fun(B* b) {
  LLVMBasicBlockRef BB = LLVMGetInsertBlock(b->builder);
  return LLVMGetBasicBlockParent(BB);
}


static Value build_expr(B* b, Node* n, const char* debugname);


inline static Sym ntypeid(B* b, Type* tn) {
  return tn->t.id ? tn->t.id : GetTypeID(b->build, tn);
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


static LLVMTypeRef build_funtype(B* b, Node* nullable params, Node* nullable result) {
  LLVMTypeRef returnType = get_type(b, result);
  LLVMTypeRef* paramsv = NULL;
  u32 paramsc = 0;
  if (params) {
    asserteq(params->kind, NTupleType);
    paramsc = params->t.list.a.len;
    paramsv = memalloc(b->build->mem, sizeof(void*) * paramsc);
    for (u32 i = 0; i < paramsc; i++) {
      paramsv[i] = get_type(b, params->t.list.a.v[i]);
    }
  }
  auto ft = LLVMFunctionType(returnType, paramsv, paramsc, /*isVarArg*/false);
  if (paramsv)
    memfree(b->build->mem, paramsv);
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
      LLVMSetValueName2(p, param->field.name, symlen(param->field.name));
    }
  }

  // linkage & visibility
  if (n->fun.name && strcmp(name, "main") != 0) {
    // TODO: only set for globals
    // Note on LLVMSetVisibility: visibility is different.
    // See https://llvm.org/docs/LangRef.html#visibility-styles
    LLVMSetLinkage(fn, LLVMPrivateLinkage); // like "static" in C but omit from symbol table
    // LLVMSetLinkage(fn, LLVMInternalLinkage); // like "static" in C
  }

  return fn;
}


static Value build_fun(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NFun);
  assertnotnull_debug(n->type);
  asserteq_debug(n->type->kind, NFunType);

  LLVMValueRef fn = NULL;
  auto f = &n->fun;

  const char* name = f->name;
  // add typeid (experiment with function overloading)
  if (name == NULL || strcmp(name, "main") != 0)
    name = str_fmt("%s%s", name, n->type->t.id); // LEAKS! FIXME

  // llvm maintains a map of all named functions in the module; query it
  fn = LLVMGetNamedFunction(b->mod, name);
  if (fn)
    return fn;

  fn = build_funproto(b, n, name);

  if (n->fun.body) {
    // Create a new basic block to start insertion into.
    // Note: entry BB is required, but its name can be empty.
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
    LLVMPositionBuilderAtEnd(b->builder, bb);
    Value bodyval = build_expr(b, n->fun.body, "");

    if (!bodyval || !value_is_ret(bodyval)) {
      // implicit return at end of body
      if (!bodyval || n->type->t.fun.result == Type_nil) {
        LLVMBuildRetVoid(b->builder);
      } else {
        if (value_is_call(bodyval)) {
          // TODO: might need to add a condition for matching parameters & return type
          LLVMSetTailCall(bodyval, true);
        }
        LLVMBuildRet(b->builder, bodyval);
      }
    }
  }

  return fn;
}


static Value build_block(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NBlock);
  assertnotnull_debug(n->type);

  Value v = NULL; // return null to signal "empty block"
  for (u32 i = 0; i < n->array.a.len; i++) {
    v = build_expr(b, n->array.a.v[i], "");
  }
  // last expr of block is its value (TODO: is this true? is that Co's semantic?)
  return v;
}


static Value build_call(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NCall);
  assertnotnull_debug(n->type);

  if (NodeIsType(n->call.receiver)) {
    // type call, e.g. str(1)
    panic("TODO: type call");
    return NULL;
  }
  // n->call.receiver->kind==NFun
  Value callee = build_expr(b, n->call.receiver, "callee");
  if (!callee) {
    errlog("unknown function");
    return NULL;
  }

  // arguments
  Value* argv = NULL;
  u32 argc = 0;
  auto args = n->call.args;
  if (args) {
    asserteq(args->kind, NTuple);
    argc = args->array.a.len;
    argv = memalloc(b->build->mem, sizeof(void*) * argc);
    for (u32 i = 0; i < argc; i++) {
      argv[i] = build_expr(b, args->array.a.v[i], "arg");
    }
  }

  // check argument count
  #ifdef DEBUG
  if (LLVMCountParams(callee) != argc) {
    errlog("wrong number of arguments: %u (expected %u)", argc, LLVMCountParams(callee));
    return NULL;
  }
  #endif

  Value v = LLVMBuildCall(b->builder, callee, argv, argc, "");
  // LLVMSetTailCall(v, true); // set tail call when we know it for sure
  if (argv)
    memfree(b->build->mem, argv);
  return v;
}


static Value build_typecast(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NTypeCast);
  assertnotnull_debug(n->type);

  LLVMBool isSigned = false;
  LLVMTypeRef dsttype = b->t_i32;
  // switch (n->call.receiver->kind) {
  //   case NBasicType: {
  //     dlog(">>>>");
  //     break;
  //   }
  //   default:
  //     dlog("other");
  //     break;
  // }
  LLVMValueRef srcval = build_expr(b, n->call.args, "");
  return LLVMBuildIntCast2(b->builder, srcval, dsttype, isSigned, debugname);
}


static Value build_return(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NReturn);
  assertnotnull_debug(n->type);
  // TODO: check current function and if type is nil, use LLVMBuildRetVoid
  LLVMValueRef v = build_expr(b, n->op.left, debugname);
  if (value_is_call(v))
    LLVMSetTailCall(v, true);
  return LLVMBuildRet(b->builder, v);
}


static LLVMTypeRef build_struct_type(B* b, Type* n) {
  asserteq_debug(n->kind, NStructType);

  u32 elemc = n->t.struc.a.len; // get_type
  LLVMTypeRef elemv_st[32];
  LLVMTypeRef* elemv = elemv_st; // TODO: memalloc if needed

  for (u32 i = 0; i < n->t.struc.a.len; i++) {
    Node* field = n->t.struc.a.v[i];
    asserteq_debug(field->kind, NField);
    elemv[i] = get_type(b, field->type);
  }

  LLVMTypeRef ty = LLVMStructCreateNamed(b->ctx, ntypeid(b, n));
  LLVMStructSetBody(ty, elemv, elemc, /*packed*/false);

  //return LLVMStructTypeInContext(b->ctx, elemv, elemc, /*packed*/false);
  return ty;
}


static LLVMTypeRef get_struct_type(B* b, Type* tn) {
  asserteq_debug(tn->kind, NStructType);
  LLVMTypeRef ty = get_intern_type(b, tn);
  if (!ty) {
    ty = build_struct_type(b, tn);
    add_intern_type(b, tn, ty);
  }
  return ty;
}


static Value build_struct_type_expr(B* b, Type* tn, const char* debugname) {
  // struct type used as value
  LLVMTypeRef ty = get_struct_type(b, tn);

  if (LLVMGetInsertBlock(b->builder)) // inside function
    return LLVMBuildAlloca(b->builder, ty, debugname);

  // global scope
  LLVMValueRef* vals = NULL; // must be const
  u32 nvals = 0;
  return LLVMConstStructInContext(b->ctx, vals, nvals, /*packed*/false);
}


static Value build_struct(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NStructCons);
  assertnotnull_debug(n->type);

  LLVMTypeRef ty = get_struct_type(b, n->type);

  if (LLVMGetInsertBlock(b->builder)) { // inside function
    dlog("TODO: initialize fields");
    return LLVMBuildAlloca(b->builder, ty, debugname);
  }

  // global scope (FIXME)
  LLVMValueRef* vals = NULL; // must be const
  u32 nvals = 0;
  return LLVMConstStructInContext(b->ctx, vals, nvals, /*packed*/false);
}


static Value build_selector(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NSelector);
  assertnotnull_debug(n->type);

  dlog("TODO: GEP");

  Value pointer = build_expr(b, n->sel.operand, debugname);
  LLVMTypeRef ty = get_type(b, n->type);

  dlog("do GEP");

  u32 field_index = 0; // fixme

  return LLVMBuildStructGEP2(b->builder, ty, pointer, field_index, debugname);

  // TODO: if struct is a constant (materialized w/ LLVMConstStructInContext)
  // then use LLVMConstGEP2.

  // return LLVMConstInt(b->t_int, 0, /*signext*/false); // placeholder
}


static Value build_index(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIndex);
  assertnotnull_debug(n->type);
  dlog("TODO: GEP");
  // TODO: LLVMBuildGEP2
  // LLVMValueRef LLVMBuildGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
  //                          LLVMValueRef Pointer, LLVMValueRef *Indices,
  //                          unsigned NumIndices, const char *Name);
  return LLVMConstInt(b->t_int, 0, /*signext*/false); // placeholder
}


static Value build_default_value(B* b, Type* tn, const char* name) {
  LLVMTypeRef ty = get_type(b, tn);
  if (tn->kind == NBasicType && TypeCodeIsInt(tn->t.basic.typeCode)) {
    return LLVMConstInt(ty, 0, /*signext*/false);
  }
  // TODO: more constant types
  Value ptr = LLVMBuildAlloca(b->builder, ty, name);
  return LLVMBuildLoad2(b->builder, ty, ptr, name);
}


static Value build_let(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NLet);

  if (n->let.nrefs == 0 && !n->type) // skip unused let
    return NULL;

  assertnotnull_debug(n->type);

  if (n->let.irval) // already allocated; return pointer
    return (Value)n->let.irval;

  if (NodeIsConst(n)) {
    dlog("TODO: immutable local");
    assertnotnull_debug(n->let.init); // should be resolved
    // n->let.irval = build_expr(b, n->let.init, n->let.name);
    // return n->let.irval;
  }

  // mutable variables
  // See https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html
  LLVMTypeRef ty = get_type(b, n->type);
  n->let.irval = LLVMBuildAlloca(b->builder, ty, n->let.name);

  if (n->let.init) {
    auto init = build_expr(b, n->let.init, n->let.name);
    return LLVMBuildStore(b->builder, init, n->let.irval);
  }

  return n->let.irval;
}


static Value build_id_read(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NId);
  assertnotnull_debug(n->type);
  assertnotnull_debug(n->ref.target); // should be resolved

  Value target = build_expr(b, n->ref.target, n->ref.name);

  if (n->ref.target->kind == NLet) {
    LLVMTypeRef ty = get_type(b, n->type);
    return LLVMBuildLoad2(b->builder, ty, target, n->ref.name);
  }

  return target;
}


static Value build_param_read(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NParam);
  assertnotnull_debug(n->type);
  dlog("TODO load param %s", fmtnode(n));
  return LLVMGetParam(get_current_fun(b), n->field.index);
}


static Value build_assign(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NAssign);
  assertnotnull_debug(n->type);

  if (n->op.left->kind == NLet) {
    // store to local variable
    const char* name = n->op.left->ref.name;
    Value left = build_expr(b, n->op.left, name);
    Value right = build_expr(b, n->op.right, "rvalue");
    LLVMBuildStore(b->builder, right, left);
    // value of assignment is its new value
    return LLVMBuildLoad2(b->builder, LLVMGetElementType(LLVMTypeOf(left)), left, name);
  }

  panic("TODO assign to %s", fmtnode(n->op.left));
  return NULL;
}


static Value build_binop(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NBinOp);
  assertnotnull_debug(n->type);

  Type* tn = n->op.left->type;
  asserteq_debug(tn->kind, NBasicType);
  assert_debug(tn->t.basic.typeCode < TypeCode_CONCRETE_END);
  assert_debug(n->op.op < T_PRIM_OPS_END);

  Value left = build_expr(b, n->op.left, "lhs");
  Value right = build_expr(b, n->op.right, "rhs");
  u32 op = 0;

  dlog("build_binop type %s (%s), op %s",
    fmtnode(tn), TypeCodeName(tn->t.basic.typeCode), TokName(n->op.op));

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
  case TypeCode_isize:
    op = kOpTableSInt[n->op.op];
    break;
  case TypeCode_u8:
  case TypeCode_u16:
  case TypeCode_u32:
  case TypeCode_u64:
  case TypeCode_uint:
  case TypeCode_usize:
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
    if (isfloat)
      return LLVMBuildFCmp(b->builder, (LLVMRealPredicate)op, left, right, debugname);
    return LLVMBuildICmp(b->builder, (LLVMIntPredicate)op, left, right, debugname);
  }
  return LLVMBuildBinOp(b->builder, (LLVMOpcode)op, left, right, debugname);
}


static Value build_if(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIf);
  assertnotnull_debug(n->type);

  bool isrvalue = (n->flags & NodeFlagRValue) != 0; // n'value is required

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
  if (!thenVal)
    return NULL; // codegen failure
  LLVMBuildBr(b->builder, endb);
  // Codegen of "then" can change the current block, update thenb for the PHI
  thenb = LLVMGetInsertBlock(b->builder);

  // else
  Value elseVal = NULL;
  if (elseb) {
    //LLVMPositionBuilderAtEnd(b->builder, startb);
    LLVMAppendExistingBasicBlock(fn, elseb);
    LLVMPositionBuilderAtEnd(b->builder, elseb);
    if (n->cond.elseb) {
      if (!TypeEquals(b->build, n->cond.thenb->type, n->cond.elseb->type))
        panic("TODO: mixed types");
      elseVal = build_expr(b, n->cond.elseb, "");
      if (!elseVal)
        return NULL; // codegen failure
    } else {
      elseVal = build_default_value(b, n->cond.thenb->type, "else");
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


static Value build_intlit(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIntLit);
  assertnotnull_debug(n->type);
  return LLVMConstInt(get_type(b, n->type), n->val.i, /*signext*/false);
}


static Value build_floatlit(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NFloatLit);
  assertnotnull_debug(n->type);
  return LLVMConstReal(get_type(b, n->type), n->val.f);
}


static Value build_expr(B* b, Node* n, const char* debugname) {
  if (debugname && debugname[0]) {
    dlog("build_expr %s %s <%s> (\"%s\")",
      NodeKindName(n->kind), fmtnode(n), fmtnode(n->type), debugname);
  } else {
    dlog("build_expr %s %s <%s>", NodeKindName(n->kind), fmtnode(n), fmtnode(n->type));
  }
  switch (n->kind) {
    case NBinOp:      R_MUSTTAIL return build_binop(b, n, debugname);
    case NId:         R_MUSTTAIL return build_id_read(b, n, debugname);
    case NLet:        R_MUSTTAIL return build_let(b, n, debugname);
    case NIntLit:     R_MUSTTAIL return build_intlit(b, n, debugname);
    case NFloatLit:   R_MUSTTAIL return build_floatlit(b, n, debugname);
    case NParam:      R_MUSTTAIL return build_param_read(b, n, debugname);
    case NBlock:      R_MUSTTAIL return build_block(b, n, debugname);
    case NCall:       R_MUSTTAIL return build_call(b, n, debugname);
    case NTypeCast:   R_MUSTTAIL return build_typecast(b, n, debugname);
    case NReturn:     R_MUSTTAIL return build_return(b, n, debugname);
    case NStructType: R_MUSTTAIL return build_struct_type_expr(b, n, debugname);
    case NStructCons: R_MUSTTAIL return build_struct(b, n, debugname);
    case NSelector:   R_MUSTTAIL return build_selector(b, n, debugname);
    case NIndex:      R_MUSTTAIL return build_index(b, n, debugname);
    case NFun:        R_MUSTTAIL return build_fun(b, n, debugname);
    case NAssign:     R_MUSTTAIL return build_assign(b, n, debugname);
    case NIf:         R_MUSTTAIL return build_if(b, n, debugname);
    default:
      panic("TODO node kind %s", NodeKindName(n->kind));
      break;
  }
  panic("invalid node kind %s", NodeKindName(n->kind));
  return NULL;
}


static Value build_global_let(B* b, Node* n) {
  assert(n->kind == NLet);
  assert(n->type);
  Value gv;
  if (n->let.init) {
    Value v = build_expr(b, n->let.init, n->let.name);
    if (!LLVMIsConstant(v)) {
      panic("not a constant expression %s", fmtnode(n));
    }
    gv = LLVMAddGlobal(b->mod, LLVMTypeOf(v), n->let.name);
    LLVMSetInitializer(gv, v);
  } else {
    gv = LLVMAddGlobal(b->mod, get_type(b, n->type), n->let.name);
  }
  n->let.irval = gv; // save pointer for later lookups
  // Note: global vars are always stored to after they are defined as
  // "x = y" becomes a variable definition if "x" is not yet defined.
  // TODO: conditionally make linkage private
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  return gv;
}


static void build_pkgpart(B* b, Node* n) {
  assert(n->kind == NFile);

  // first build all globals
  for (u32 i = 0; i < n->cunit.a.len; i++) {
    auto cn = (Node*)n->cunit.a.v[i];
    if (cn->kind == NLet)
      build_global_let(b, cn);
  }

  // then functions
  for (u32 i = 0; i < n->cunit.a.len; i++) {
    auto cn = (Node*)n->cunit.a.v[i];
    switch (cn->kind) {
      case NFun:
        assertnotnull(cn->fun.name);
        build_fun(b, cn, cn->fun.name);
        break;
      case NLet:
        break;
      default:
        dlog("TODO: %s", NodeKindName(cn->kind));
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
    .t_i8 = LLVMInt8TypeInContext(ctx),
    .t_i16 = LLVMInt16TypeInContext(ctx),
    .t_i32 = LLVMInt32TypeInContext(ctx),
    .t_i64 = LLVMInt64TypeInContext(ctx),
    .t_f32 = LLVMFloatTypeInContext(ctx),
    .t_f64 = LLVMDoubleTypeInContext(ctx),
  };
  _b.t_int = _b.t_i32; // alias int = i32
  B* b = &_b;
  SymMapInit(&b->internedTypes, 16, build->mem);

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
    build_pkgpart(b, cn);
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
