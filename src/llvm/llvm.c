#include "../colib.h"
#include "../parse/parse.h"
// #include "../util/rtimer.h"
// #include "../util/ptrmap.h"
// #include "../util/stk_array.h"
#include "llvmimpl.h"

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/OrcEE.h>


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


bool llvm_build_and_emit(BuildCtx* build, const char* triple) {
  dlog("llvm_build_and_emit");
  bool ok = false;

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(build->pkg.name, ctx);

  // ...

  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  return ok;
}
