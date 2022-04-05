// Interfaces internal to source files in this directory, not included by outside sources.
// The llvm.h file contains interfaces "exported" to outside users.
#pragma once

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Initialization.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/OrcEE.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>

#include "llvm.h"

ASSUME_NONNULL_BEGIN

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

// llvm_module_optimize1 is the part of llvm_module_optimize implemented in C++
EXTERN_C error llvm_module_optimize1(CoLLVMModule* m, const CoLLVMBuild*, int olevel);


ASSUME_NONNULL_END