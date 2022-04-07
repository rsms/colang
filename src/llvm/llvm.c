#include "llvmimpl.h"
#include "../parse/parse.h"


#define HANDLE_LLVM_ERRMSG(err, errmsg) ({ \
  dlog("llvm error: %s", (errmsg)); \
  LLVMDisposeMessage((errmsg)); \
  err; \
})


static error select_target(const char* triple, LLVMTargetRef* targetp) {
  char* errmsg;
  if (LLVMGetTargetFromTriple(triple, targetp, &errmsg) != 0)
    return HANDLE_LLVM_ERRMSG(err_invalid, errmsg);

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


void llvm_module_init(CoLLVMModule* m, BuildCtx* build, const char* name) {
  m->build = build;
  m->M = LLVMModuleCreateWithNameInContext(name, LLVMContextCreate());
  m->TM = NULL;
}


void llvm_module_dispose(CoLLVMModule* m) {
  if (m->M == NULL)
    return;
  LLVMModuleRef mod = m->M;
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  memset(m, 0, sizeof(*m));
}


error llvm_module_set_target(CoLLVMModule* m, const char* triple) {
  m->TM = NULL;
  LLVMTargetRef target;
  error err = select_target(triple, &target);
  if (err)
    return err;

  LLVMCodeGenOptLevel optLevel;
  LLVMCodeModel codeModel = LLVMCodeModelDefault;
  switch ((enum OptLevel)m->build->opt) {
    case OptNone:
      optLevel = LLVMCodeGenLevelNone;
      break;
    case OptSpeed:
      optLevel = LLVMCodeGenLevelLess;
      break;
    case OptPerf:
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

  LLVMModuleRef mod = m->M;
  LLVMSetTarget(mod, triple);
  LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetm);
  LLVMSetModuleDataLayout(mod, assertnotnull(dataLayout));

  m->TM = targetm;
  return 0;
}


error llvm_module_optimize(CoLLVMModule* m, const CoLLVMBuild* opt) {
  // optimize and target-fit module (also verifies the IR)
  int optlevel = 0;
  switch ((enum OptLevel)m->build->opt) {
    case OptNone:  optlevel = 0; break;
    case OptSpeed: optlevel = 1; break;
    case OptPerf:  optlevel = 3; break;
    case OptSize:  optlevel = 4; break;
  }
  return llvm_module_optimize1(m, opt, optlevel);
}


void llvm_module_dump(CoLLVMModule* m) {
  LLVMDumpModule(m->M);
}


error llvm_build(BuildCtx* build, CoLLVMModule* m, const CoLLVMBuild* opt) {
  dlog("llvm_build");
  error err;

  llvm_module_init(m, build, build->pkg.name);

  err = llvm_module_set_target(m, opt->target_triple);
  if (err)
    goto onerror;

  err = llvm_module_build(m, opt);
  if (err)
    goto onerror;

  err = llvm_module_optimize(m, opt);
  if (err)
    goto onerror;

  return 0;
onerror:
  llvm_module_dispose(m);
  return err;
}
