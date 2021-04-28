#include <rbase/rbase.h>

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>

#include "llvm.h"
#include "../parse/parse.h"

// make the code more readable by using short name aliases
typedef LLVMValueRef  Value;
typedef LLVMTypeRef   Type;
typedef CoLLVMIRBuild Build;

static void print_duration(const char* message, u64 timestart) {
  auto timeend = nanotime();
  char abuf[40];
  auto buflen = fmtduration(abuf, countof(abuf), timeend - timestart);
  fprintf(stderr, "%s %.*s\n", message, buflen, abuf);
  fflush(stderr);
}

static Value build_call(Build* b, const char* calleeName) {
  // look up the name in the module table
  Value callee = LLVMGetNamedFunction(b->mod, calleeName);
  if (!callee) {
    errlog("unknown function %s", calleeName);
    return NULL;
  }
  // check argument count
  Value args[] = {
    LLVMConstInt(b->t_i32, 3, /*signext*/false),
    LLVMConstInt(b->t_i32, 4, /*signext*/false),
  };
  u32 nargs = (u32)countof(args);
  if (LLVMCountParams(callee) != nargs) {
    errlog("wrong number of arguments: %u (expected %u)", nargs, LLVMCountParams(callee));
    return NULL;
  }
  return LLVMBuildCall(b->builder, callee, args, nargs, "");
}

static Type build_funtype(Build* b) {
  Type returnType = b->t_i32;
  Type paramTypes[] = {b->t_i32, b->t_i32};
  LLVMBool isVarArg = false;
  return LLVMFunctionType(returnType, paramTypes, countof(paramTypes), isVarArg);
}

static Value build_funproto(Build* b, const char* name) {
  Type fnt = build_funtype(b);
  Value fn = LLVMAddFunction(b->mod, name, fnt);
  // set argument names (for debugging)
  Value arg1 = LLVMGetParam(fn, 0);
  Value arg2 = LLVMGetParam(fn, 1);
  LLVMSetValueName2(arg1, "x", 1);
  LLVMSetValueName2(arg2, "y", 1);
  return fn;
}

static Value build_fun(Build* b, const char* name) {
  // function prototype
  Value fn = build_funproto(b, name);

  // Create a new basic block to start insertion into.
  // Note: entry BB is required, but its name can be empty.
  LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, bb);
  // gs.Builder->SetInsertPoint(bb);

  // hard-coded bodies
  if (strcmp(name, "main") == 0) {
    Value v = build_call(b, "foo");
    LLVMBuildRet(b->builder, v);
  } else {
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    // (return (+ (+ arg1 arg2) (+ arg1 arg2)))
    Value add1 = LLVMBuildAdd(b->builder, LLVMGetParam(fn, 0), LLVMGetParam(fn, 1), "");
    Value add2 = LLVMBuildAdd(b->builder, LLVMGetParam(fn, 0), LLVMGetParam(fn, 1), "");
    Value v = LLVMBuildAdd(b->builder, add1, add2, "");
    LLVMBuildRet(b->builder, v);
  }

  // optimize
  if (b->FPM)
    LLVMRunFunctionPassManager(b->FPM, fn);

  // Note: On error, erase the function and return NULL:
  // LLVMEraseGlobalIFunc(fn);
  // return NULL;

  // LLVMDumpValue(fn);
  return fn;
}

static void build_module(LLVMModuleRef mod) {
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  CoLLVMIRBuild build = {
    .ctx = ctx,
    .mod = mod,
    .builder = LLVMCreateBuilderInContext(ctx),

    // FPM: Apply per-function optimizations. Set to NULL to disable.
    // Really only useful for JIT as for assembly to asm, obj or bc we apply module-wide opt.
    // .FPM = LLVMCreateFunctionPassManagerForModule(mod),
    .FPM = NULL,

    // constants
    .t_i32 = LLVMInt32TypeInContext(ctx), // note: no disposal needed of built-in types
  };
  Build* b = &build;

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

  // build a function
  build_fun(b, "foo");
  build_fun(b, "main");

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

  LLVMDumpModule(b->mod);

finish:
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
  LLVMTargetRef target,
  const char* triple,
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

  LLVMRelocMode        relocMode = LLVMRelocStatic;
  LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
    target, triple, CPU, features, optLevel, relocMode, codeModel);
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


// emit_mc emits machine code as binary object or assembly code
static bool emit_mc(
  LLVMModuleRef mod,
  LLVMTargetMachineRef targetMachine,
  LLVMCodeGenFileType fileType,
  const char* filename)
{
  // // optimize
  // if (!llvm_emit_mc(mod, targetMachine, CoBuildDebug, NULL, NULL))
  //   errlog("llvm_emit_mc failed");
  char* errmsg;
  char* filenametmp = strdup(filename);
  bool ok = LLVMTargetMachineEmitToFile(targetMachine, mod, filenametmp, fileType, &errmsg) == 0;
  free(filenametmp);
  if (!ok) {
    errlog("LLVMTargetMachineEmitToFile: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    return false;
  }
  dlog("wrote %s", filename);
  return true;
}


// emit_ir emits LLVM IR code
static bool emit_ir(
  LLVMModuleRef mod,
  const char* filename)
{
  char* errmsg;
  if (LLVMPrintModuleToFile(mod, filename, &errmsg) != 0) {
    errlog("LLVMPrintModuleToFile: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    return false;
  }
  dlog("wrote %s", filename);
  return true;
}


__attribute__((constructor,used)) static void llvm_init() {
  dlog("llvm_init");
  auto timestart = nanotime();

  // optimization level
  CoBuildType buildType = CoBuildOpt;

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("hello", ctx);

  build_module(mod);

  // select target and emit machine code
  const char* defaultTriple = llvm_init_targets();
  const char* triple = defaultTriple;
  LLVMTargetRef target = select_target(triple);
  LLVMCodeGenOptLevel optLevel =
    (buildType == CoBuildDebug ? LLVMCodeGenLevelNone : LLVMCodeGenLevelAggressive);
  LLVMCodeModel codeModel =
    (buildType == CoBuildOptSmall ? LLVMCodeModelSmall : LLVMCodeModelDefault);

  // LLVMCodeGenOptLevel optLevel = LLVMCodeGenLevelAggressive;
  LLVMTargetMachineRef targetMachine = select_target_machine(target, triple, optLevel, codeModel);
  if (targetMachine) {
    // emit
    //
    // Note: We have two options for emitting machine code:
    //   1. using the pre-packaged LLVMTargetMachineEmitToFile from llvm-c
    //   2. using our own llvm_emit_mc
    // LLVMTargetMachineEmitToFile produces less optimized code than llvm_emit_mc even with
    // LLVMCodeGenLevelAggressive. Additionally, LLVMTargetMachineEmitToFile is slower when
    // emitting to both obj/bc and asm at the same time since two separate calls are required,
    // whereas llvm_emit_mc can emit both by avoiding redundant work.
    // Another upside of llvm_emit_mc is that is applies optimizations to the module which
    // allows us to emit LLVM IR code with optimizations.
    const char* obj_file = "out1.o";
    const char* asm_file = "out1.asm";
    auto timestart = nanotime();
    #if 0
      if (obj_file)
        emit_mc(mod, targetMachine, LLVMObjectFile, obj_file);
      if (asm_file)
        emit_mc(mod, targetMachine, LLVMAssemblyFile, asm_file);
    #else
      bool enable_tsan = false;
      bool enable_lto = false; // if enabled, write LLVM bitcode to bin_outfile, else object MC
      llvm_emit_mc(mod, targetMachine, CoBuildOpt, enable_tsan, enable_lto, asm_file, obj_file);
    #endif
    print_duration("emit", timestart);
    emit_ir(mod, "out1.ll");
  }

  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  print_duration("llvm", timestart);
  exit(0);
}
