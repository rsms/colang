#include "llvmimpl.h"
#include "../parse/parse.h"


static error select_target(const char* triple, LLVMTargetRef* targetp) {
  char* errmsg;
  if (LLVMGetTargetFromTriple(triple, targetp, &errmsg) != 0) {
    dlog("LLVMGetTargetFromTriple: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    return err_invalid;
  }

  #if DEBUG
    LLVMTargetRef target = *targetp;
    const char* name = LLVMGetTargetName(target);
    const char* description = LLVMGetTargetDescription(target);
    const char* jit = LLVMTargetHasJIT(target) ? " jit" : "";
    const char* mc = LLVMTargetHasTargetMachine(target) ? " mc" : "";
    const char* asmx = LLVMTargetHasAsmBackend(target) ? " asm" : "";
    dlog("selected target: %s (%s) [abilities:%s%s%s]", name, description, jit, mc, asmx);
  #endif

  return 0;
}


// select_target_machine is like -mtune
static error select_target_machine(
  LLVMTargetRef         target,
  const char*           triple,
  LLVMCodeGenOptLevel   optLevel,
  LLVMCodeModel         codeModel,
  LLVMTargetMachineRef* resultp)
{
  // select host CPU and features (NOT PORTABLE!) when optimizing
  const char* CPU = "";      // "" for generic
  const char* features = ""; // "" for none
  char* hostCPUName = NULL; // needs LLVMDisposeMessage
  char* hostFeatures = NULL; // needs LLVMDisposeMessage
  if (optLevel != LLVMCodeGenLevelNone && strcmp(triple, llvm_host_triple())) {
    hostCPUName = LLVMGetHostCPUName();
    hostFeatures = LLVMGetHostCPUFeatures();
    CPU = hostCPUName;
    features = hostFeatures;
  }

  LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
    target, triple, CPU, features, optLevel, LLVMRelocStatic, codeModel);
  if (!tm) {
    dlog("LLVMCreateTargetMachine failed");
    return err_not_supported;
  }

  if (hostCPUName) {
    LLVMDisposeMessage(hostCPUName);
    LLVMDisposeMessage(hostFeatures);
  }

  *resultp = tm;
  return 0;
}


CoLLVMModule nullable llvm_module_create(BuildCtx* build, const char* name) {
  LLVMContextRef ctx = LLVMContextCreate();
  return (CoLLVMModule)LLVMModuleCreateWithNameInContext(name, ctx);
}


void llvm_module_free(CoLLVMModule m) {
  LLVMModuleRef mod = (LLVMModuleRef)m;
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
}


error llvm_module_set_target(BuildCtx* build, CoLLVMModule m, const char* triple) {
  LLVMTargetRef target;
  error err = select_target(triple, &target);
  if (err)
    return err;

  LLVMCodeGenOptLevel optLevel;
  LLVMCodeModel codeModel = LLVMCodeModelDefault;
  switch ((enum OptLevel)build->opt) {
    case OptNone:
      optLevel = LLVMCodeGenLevelNone;
      break;
    case OptSpeed:
      optLevel = LLVMCodeGenLevelAggressive;
      break;
    case OptSize:
      optLevel = LLVMCodeGenLevelDefault;
      codeModel = LLVMCodeModelSmall;
      break;
  }
  LLVMTargetMachineRef targetm;
  err = select_target_machine(target, triple, optLevel, codeModel, &targetm);
  if (err)
    return err;

  LLVMModuleRef mod = (LLVMModuleRef)m;
  LLVMSetTarget(mod, triple);
  LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetm);
  LLVMSetModuleDataLayout(mod, assertnotnull(dataLayout));

  return 0;
}


error llvm_build(BuildCtx* build, const char* triple) {
  dlog("llvm_build");
  error err;

  CoLLVMModule m = llvm_module_create(build, build->pkg.name);

  err = llvm_module_set_target(build, m, triple);
  if (err)
    goto done;

  err = llvm_module_build(build, m);
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
  llvm_module_free(m);
  return err;
}
