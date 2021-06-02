#include "../common.h"
#include "../parse/parse.h"
#include "llvm.h"

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>

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
  SymMap typemap;

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


static LLVMTypeRef get_type(B* b, Type* nullable n) {
  if (!n)
    return b->t_void;
  switch (n->kind) {
    case NBasicType: {
      switch (n->t.basic.typeCode) {
        case TypeCode_bool:
          return b->t_bool;
        case TypeCode_int8:
        case TypeCode_uint8:
          return b->t_i8;
        case TypeCode_int16:
        case TypeCode_uint16:
          return b->t_i16;
        case TypeCode_int32:
        case TypeCode_uint32:
          return b->t_i32;
        case TypeCode_int64:
        case TypeCode_uint64:
          return b->t_i64;
        case TypeCode_float32:
          return b->t_f32;
        case TypeCode_float64:
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
    default:
      panic("TODO node kind %s", NodeKindName(n->kind));
      break;
  }
  panic("invalid node kind %s", NodeKindName(n->kind));
  return NULL;
}


static void print_duration(const char* message, u64 timestart) {
  auto timeend = nanotime();
  char abuf[40];
  auto buflen = fmtduration(abuf, countof(abuf), timeend - timestart);
  fprintf(stderr, "%s %.*s\n", message, buflen, abuf);
  fflush(stderr);
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


static LLVMTypeRef build_funtype(B* b, Node* nullable params, Node* nullable result) {
  LLVMTypeRef returnType = get_type(b, result);
  LLVMTypeRef* paramsv = NULL;
  u32 paramsc = 0;
  if (params != NULL) {
    assert(params->kind == NTupleType);
    paramsc = params->array.a.len;
    paramsv = memalloc(b->build->mem, sizeof(void*) * paramsc);
    for (u32 i = 0; i < paramsc; i++) {
      paramsv[i] = get_type(b, params->array.a.v[i]);
    }
  }
  auto ft = LLVMFunctionType(returnType, paramsv, paramsc, /*isVarArg*/false);
  if (paramsv)
    memfree(b->build->mem, paramsv);
  return ft;
}


static LLVMTypeRef get_funtype(B* b, Node* funTypeNode) {
  assert(funTypeNode->kind == NFunType);
  assert(funTypeNode->t.id);
  LLVMTypeRef ft = (LLVMTypeRef)SymMapGet(&b->typemap, funTypeNode->t.id);
  if (!ft) {
    ft = build_funtype(b, funTypeNode->t.fun.params, funTypeNode->t.fun.result);
    SymMapSet(&b->typemap, funTypeNode->t.id, ft);
  }
  return ft;
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


static Value get_fun(B* b, Node* n) { // n->kind==NFun
  asserteq(n->kind, NFun);
  LLVMValueRef fn = NULL;
  dlog("n %s", fmtnode(n));
  auto f = &n->fun;

  // name
  assertnotnull(f->name);
  const char* name = f->name;
  // add typeid (experiment with function overloading)
  if (strcmp(name, "main") != 0)
    name = str_fmt("%s%s", name, n->type->t.id); // LEAKS!

  // llvm maintains a map of all named functions in the module; query it
  fn = LLVMGetNamedFunction(b->mod, name);
  if (!fn) {
    fn = build_funproto(b, n, name);
    // // also build body if we have it
    // if (n->fun.body) {
    //   // Create a new basic block to start insertion into.
    //   // Note: entry BB is required, but its name can be empty.
    //   LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
    //   LLVMPositionBuilderAtEnd(b->builder, bb);
    //
    //   Value retval = build_expr(b, n->fun.body, "");
    //   if (!retval || n->type->t.fun.result == Type_nil) {
    //     LLVMBuildRetVoid(b->builder);
    //     // retval = LLVMConstInt(b->t_int, 0, /*signext*/false); // XXX TMP
    //   } else {
    //     LLVMBuildRet(b->builder, retval);
    //   }
    // }
  }
  return fn;
}


static Value build_fun(B* b, Node* n) {
  assert(n->kind == NFun);
  assert(n->type);
  assert(n->type->kind == NFunType);
  auto f = &n->fun;

  if (f->name) { // Sym
    dlog("named fun: %p %s", n, f->name);
  } else {
    dlog("anonymous fun: %p", n);
  }

  Value fn = get_fun(b, n);

  assertnotnull(n->fun.body);
  // Create a new basic block to start insertion into.
  // Note: entry BB is required, but its name can be empty.
  LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, bb);
  Value bodyval = build_expr(b, n->fun.body, "");

  // LLVMOpcode LLVMRet
  LLVMOpcode LLVMGetInstructionOpcode(LLVMValueRef Inst);

  if (!bodyval || !value_is_ret(bodyval)) {
    // implicit return at end of body
    if (!bodyval || n->type->t.fun.result == Type_nil) {
      LLVMBuildRetVoid(b->builder);
    } else {
      if (value_is_call(bodyval))
        LLVMSetTailCall(bodyval, true);
      LLVMBuildRet(b->builder, bodyval);
    }
  }

  return fn;
}


static Value build_block(B* b, Node* n) { // n->kind==NBlock
  Value v = NULL; // return null to signal "empty block"
  for (u32 i = 0; i < n->array.a.len; i++) {
    v = build_expr(b, n->array.a.v[i], "");
  }
  // last expr of block is its value (TODO: is this true? is that Co's semantic?)
  return v;
}


static Value build_call(B* b, Node* n) { // n->kind==NCall
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


static Value build_typecast(B* b, Node* n, const char* debugname) { // n->kind==NTypeCast
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


static Value build_return(B* b, Node* n, const char* debugname) { // n->kind==NReturn
  // TODO: check current function and if type is nil, use LLVMBuildRetVoid
  LLVMValueRef v = build_expr(b, n->op.left, debugname);
  if (value_is_call(v))
    LLVMSetTailCall(v, true);
  return LLVMBuildRet(b->builder, v);
}


static Value build_expr(B* b, Node* n, const char* debugname) {
retry:
  if (debugname && debugname[0]) {
    dlog("build_expr %s %s <%s> (\"%s\")",
      NodeKindName(n->kind), fmtnode(n), fmtnode(n->type), debugname);
  } else {
    dlog("build_expr %s %s <%s>", NodeKindName(n->kind), fmtnode(n), fmtnode(n->type));
  }
  switch (n->kind) {
    case NBinOp: {
      Value x = build_expr(b, n->op.left, "");
      Value y = build_expr(b, n->op.right, "");
      // TODO FIXME: op (currently hard coded to "add")
      if (n->type == Type_float64 || n->type == Type_float32) {
        return LLVMBuildFAdd(b->builder, x, y, debugname);
      } else {
        return LLVMBuildAdd(b->builder, x, y, debugname);
      }
    }
    case NId: {
      assert(n->ref.target); // should be resolved
      debugname = n->ref.name;
      n = n->ref.target;
      goto retry;
    }
    case NLet: {
      // FIXME don't use name lookup here; pointers POINTERS! (or Sym or PtrMap or something.)
      Value v = LLVMGetNamedGlobal(b->mod, n->field.name);
      if (v) {
        dlog("found named global");
        return v;
      }
      // TODO FIXME generate a location instead of just assuming init is the value
      assert(n->field.init); // should be resolved
      debugname = n->field.name;
      n = n->field.init;
      goto retry;
    }
    case NIntLit:
      return LLVMConstInt(get_type(b, n->type), n->val.i, /*signext*/false);
    case NFloatLit:
      return LLVMConstReal(get_type(b, n->type), n->val.f);
    case NArg:
      return LLVMGetParam(get_current_fun(b), n->field.index);
    case NBlock:
      return build_block(b, n);
    case NCall:
      return build_call(b, n);
    case NTypeCast:
      return build_typecast(b, n, debugname);
    case NReturn:
      return build_return(b, n, debugname);
    case NFun:
      return get_fun(b, n);
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
  if (n->field.init) {
    Value v = build_expr(b, n->field.init, n->field.name);
    if (!LLVMIsConstant(v)) {
      panic("not a constant expression %s", fmtnode(n));
    }
    gv = LLVMAddGlobal(b->mod, LLVMTypeOf(v), n->field.name);
    LLVMSetInitializer(gv, v);
  } else {
    gv = LLVMAddGlobal(b->mod, get_type(b, n->type), n->field.name);
  }
  // TODO: conditionally make linkage private
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  return gv;
}


static void build_pkgpart(B* b, Node* n) {
  assert(n->kind == NFile);
  for (u32 i = 0; i < n->array.a.len; i++) {
    auto cn = (Node*)n->array.a.v[i];
    switch (cn->kind) {
      case NFun:
        build_fun(b, cn);
        break;
      case NLet:
        // package-level constant
        build_global_let(b, cn);
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
    // Really only useful for JIT as for assembly to asm, obj or bc we apply module-wide opt.
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
  SymMapInit(&b->typemap, 8, build->mem);

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
  for (u32 i = 0; i < pkgnode->array.a.len; i++) {
    auto cn = (Node*)pkgnode->array.a.v[i];
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
      errlog("LLVMVerifyModule: %s", errmsg);
      LLVMDisposeMessage(errmsg);
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

finish:
  SymMapDispose(&b->typemap);
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
    const char* name = LLVMGetTargetName(target);
    const char* description = LLVMGetTargetDescription(target);
    const char* jit = LLVMTargetHasJIT(target) ? " jit" : "";
    const char* mc = LLVMTargetHasTargetMachine(target) ? " mc" : "";
    const char* _asm = LLVMTargetHasAsmBackend(target) ? " asm" : "";
    dlog("selected target: %s (%s) [abilities:%s%s%s]", name, description, jit, mc, _asm);
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

  LLVMTargetMachineRef targetMachine =
    LLVMCreateTargetMachine(target, triple, CPU, features, optLevel, LLVMRelocStatic, codeModel);
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


bool llvm_build_and_emit(Build* build, Node* pkgnode, const char* triple) {
  dlog("llvm_build_and_emit");
  bool ok = false;
  auto timestart = nanotime();

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(build->pkg->id, ctx);

  // build module; Co AST -> LLVM IR
  // TODO: move the IR building code to C++
  build_module(build, pkgnode, mod);

  // select target and emit machine code
  const char* hostTriple = llvm_init_targets();
  if (!triple)
    triple = hostTriple; // default to host
  LLVMTargetRef target = select_target(triple);
  LLVMCodeGenOptLevel optLevel =
    (build->opt == CoOptNone ? LLVMCodeGenLevelNone : LLVMCodeGenLevelDefault);
  LLVMCodeModel codeModel =
    (build->opt == CoOptSmall ? LLVMCodeModelSmall : LLVMCodeModelDefault);

  // optLevel = LLVMCodeGenLevelAggressive;
  LLVMTargetMachineRef targetm = select_target_machine(target, triple, optLevel, codeModel);
  if (!targetm)
    goto end;

  // set target
  LLVMSetTarget(mod, triple);
  LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetm);
  LLVMSetModuleDataLayout(mod, dataLayout);

  char* errmsg;

  // optimize module
  bool enable_tsan = false;
  bool enable_lto = false;
  auto opt_timestart = nanotime();
  if (!llvm_optmod(mod, targetm, build->opt, enable_tsan, enable_lto, &errmsg)) {
    errlog("llvm_optmod: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    goto end;
  }
  print_duration("llvm_optmod", opt_timestart);
  #ifdef DEBUG
  dlog("LLVM IR module after optimizations:");
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
    auto timestart = nanotime();
    if (!llvm_emit_mc(mod, targetm, LLVMObjectFile, obj_file, &errmsg)) {
      errlog("llvm_emit_mc (LLVMObjectFile): %s", errmsg);
      LLVMDisposeMessage(errmsg);
      // obj_file = NULL; // skip linking
      goto end;
    } else {
      dlog("wrote %s", obj_file);
    }
    print_duration("emit_mc obj", timestart);
  }

  // emit machine code (assembly)
  if (asm_file) {
    double timestart = nanotime();
    if (!llvm_emit_mc(mod, targetm, LLVMAssemblyFile, asm_file, &errmsg)) {
      errlog("llvm_emit_mc (LLVMAssemblyFile): %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    } else {
      dlog("wrote %s", asm_file);
    }
    print_duration("emit_mc asm", timestart);
  }

  // emit LLVM bitcode
  if (bc_file) {
    double timestart = nanotime();
    if (!llvm_emit_bc(mod, bc_file, &errmsg)) {
      errlog("llvm_emit_bc: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    } else {
      dlog("wrote %s", bc_file);
    }
    print_duration("llvm_emit_bc", timestart);
  }

  // emit LLVM IR
  if (ir_file) {
    double timestart = nanotime();
    if (!llvm_emit_ir(mod, ir_file, &errmsg)) {
      errlog("llvm_emit_ir: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    } else {
      dlog("wrote %s", ir_file);
    }
    print_duration("llvm_emit_ir", timestart);
  }

  // link executable
  if (exe_file && obj_file) {
    double timestart = nanotime();
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
    } else {
      if (strlen(errmsg) > 0)
        fwrite(errmsg, strlen(errmsg), 1, stderr); // print warnings
      dlog("wrote %s", exe_file);
    }
    LLVMDisposeMessage(errmsg);
    print_duration("link", timestart);
  }

  // if we get here, without "goto end", all succeeded
  ok = true;

end:
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  print_duration("llvm_build_and_emit", timestart);
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
