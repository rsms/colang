#pragma once

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/Initialization.h>
#include <llvm-c/TargetMachine.h>

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

typedef enum CoBuildType {
  CoBuildDebug,    // -O0
  CoBuildOptSmall, // -Oz
  CoBuildOpt,      // -O3
} CoBuildType;

typedef struct CoLLVMIRBuild {
  LLVMContextRef  ctx;
  LLVMModuleRef   mod;
  LLVMBuilderRef  builder;

  //// debug info
  //std::unique_ptr<DIBuilder>   DBuilder;
  //DebugInfo                    debug;

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // constants
  LLVMTypeRef t_i32;

} CoLLVMIRBuild;

// llvm_init_targets initializes target info and returns the default target triplet.
// Safe to call multiple times. Just returns a cached value on subsequent calls.
EXTERN_C const char* llvm_init_targets();

EXTERN_C bool llvm_emit_mc(
  LLVMModuleRef,
  LLVMTargetMachineRef,
  CoBuildType,
  bool enable_tsan, // thread sanitizer
  bool enable_lto,  // LTO (if enabled, write LLVM bitcode to bin_outfile instead of object MC)
  const char* nullable asm_outfile,
  const char* nullable bin_outfile);
