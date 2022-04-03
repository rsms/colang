#pragma once

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/Initialization.h>
#include <llvm-c/TargetMachine.h>

#include "../colib.h"
#include "llvm.h"

ASSUME_NONNULL_BEGIN

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif


// llvm_optmod applies module-wide optimizations.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
EXTERN_C bool llvm_optmod(
  LLVMModuleRef        mod,
  LLVMTargetMachineRef targetm,
  int                  optlevel,
  bool                 enable_tsan,
  bool                 enable_lto,
  char**               errmsg);

// llvm_emit_bc writes LLVM IR (text) code to filename.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
static bool llvm_emit_ir(LLVMModuleRef, const char* filename, char** errmsg);

// llvm_emit_bc writes LLVM bitcode to filename.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
EXTERN_C bool llvm_emit_bc(LLVMModuleRef, const char* filename, char** errmsg);

// llvm_emit_mc applies module-wide optimizations (unless CoBuildDebug) and emits machine-specific
// code to asm_outfile and/or bin_outfile.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
static bool llvm_emit_mc(
  LLVMModuleRef, LLVMTargetMachineRef, LLVMCodeGenFileType,
  const char* filename, char** errmsg);


// --------------------------------------------------------------------------------------
// implementations

inline static bool llvm_emit_ir(LLVMModuleRef M, const char* filename, char** errmsg) {
  return LLVMPrintModuleToFile(M, filename, errmsg) == 0;
}

inline static bool llvm_emit_mc(
  LLVMModuleRef        M,
  LLVMTargetMachineRef T,
  LLVMCodeGenFileType  FT,
  const char*          filename,
  char**               errmsg)
{
  // Note: Filename argument to LLVMTargetMachineEmitToFile is incorrectly typed as mutable
  // "char*" in llvm-c/TargetMachine.h. It's really "const char*" as is evident by looking at
  // the implementation in llvm/lib/Target/TargetMachineC.cpp.
  return LLVMTargetMachineEmitToFile(T, M, (char*)filename, FT, errmsg) == 0;
}

ASSUME_NONNULL_END
