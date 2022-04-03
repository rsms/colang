#include "llvmimpl.h"
#include "../parse/parse.h"


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
    const char* asmx = LLVMTargetHasAsmBackend(target) ? " asm" : "";
    dlog("selected target: %s (%s) [abilities:%s%s%s]", name, description, jit, mc, asmx);
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


// defined in build.c
error llvm_build_module(BuildCtx* build, LLVMModuleRef mod);


error llvm_build_and_emit(BuildCtx* build, const char* triple) {
  dlog("llvm_build_and_emit");
  error err;

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(build->pkg.name, ctx);

  err = llvm_build_module(build, mod);
  if (err)
    goto done;

  // TODO:
  // - select target and emit machine code
  // - verify, optimize and target-fit module
  // - emit machine code (object)
  // - emit machine assembly (for debugging)
  // - emit LLVM IR bitcode (for debugging)
  // - emit LLVM IR text code (for debugging)
  // - link executable (objects -> elf/mach-o/coff)

done:
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  return err;
}
