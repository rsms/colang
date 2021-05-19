#!/bin/bash
set -e
cd "$(dirname "$0")/.."

LLVM_PREFIX=$PWD/deps/llvm
BUILDDIR=$PWD/deps/llvm-src/build-compiler-rt

rm -rf "$BUILDDIR"
mkdir -p "$BUILDDIR"
cd "$BUILDDIR"

# -DLLVM_CONFIG_PATH="$LLVM_PREFIX"

cmake \
  -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCOMPILER_RT_BUILD_XRAY=OFF \
  -DCOMPILER_RT_USE_LIBCXX=OFF \
  -DCOMPILER_RT_CAN_EXECUTE_TESTS=OFF \
  -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
  -DCOMPILER_RT_BUILD_CRT=OFF \
  -DCOMPILER_RT_BUILD_PROFILE=OFF \
  -DCOMPILER_RT_BUILD_MEMPROF=OFF \
  -DSANITIZER_USE_STATIC_CXX_ABI=ON \
  ../compiler-rt

ninja

# deps/llvm-src/compiler-rt/CMakeLists.txt
#  option(COMPILER_RT_BUILD_BUILTINS "Build builtins" ON)
#  option(COMPILER_RT_BUILD_CRT "Build crtbegin.o/crtend.o" ON)
#  option(COMPILER_RT_CRT_USE_EH_FRAME_REGISTRY "Use eh_frame in crtbegin.o/crtend.o" ON)
#  option(COMPILER_RT_BUILD_SANITIZERS "Build sanitizers" ON)
#  option(COMPILER_RT_BUILD_XRAY "Build xray" ON)
#  option(COMPILER_RT_BUILD_LIBFUZZER "Build libFuzzer" ON)
#  option(COMPILER_RT_BUILD_PROFILE "Build profile runtime" ON)
#  option(COMPILER_RT_BUILD_MEMPROF "Build memory profiling runtime" ON)
#  option(COMPILER_RT_BUILD_XRAY_NO_PREINIT "Build xray with no preinit patching" OFF)
#  option(COMPILER_RT_CAN_EXECUTE_TESTS "Can we execute instrumented tests" ON)
#  option(COMPILER_RT_CAN_EXECUTE_TESTS "Can we execute instrumented tests" OFF)
#  option(COMPILER_RT_DEBUG "Build runtimes with full debug info" OFF)
#  option(COMPILER_RT_EXTERNALIZE_DEBUGINFO
#  option(COMPILER_RT_INTERCEPT_LIBDISPATCH
#  option(COMPILER_RT_LIBDISPATCH_INSTALL_PATH
#  option(SANITIZER_ALLOW_CXXABI "Allow use of C++ ABI details in ubsan" ON)
#  option(SANITIZER_USE_STATIC_LLVM_UNWINDER
#  option(SANITIZER_USE_STATIC_CXX_ABI
#  option(COMPILER_RT_USE_BUILTINS_LIBRARY
#  option(COMPILER_RT_USE_LIBCXX "Enable compiler-rt to use libc++ from the source tree" ON)
