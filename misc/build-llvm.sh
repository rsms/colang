#!/bin/bash
set -e
USERWD=$PWD
cd "$(dirname "$0")/.."
. misc/_common.sh

# where to install the result
DESTDIR="$DEPS_DIR"
mkdir -p "$DESTDIR"

# Host compiler location
# HOST_LLVM_PREFIX=/usr/local/Cellar/llvm/11.0.0
if [ -z "$HOST_LLVM_PREFIX" ]; then
  HOST_LLVM_PREFIX="$(command -v clang)"
  [ -n "$HOST_LLVM_PREFIX" ] ||
    _err "clang not found in PATH. Set HOST_LLVM_PREFIX or add clang to PATH"
  HOST_LLVM_PREFIX="$(dirname "$(dirname "$HOST_LLVM_PREFIX")")"
fi

export CC="$HOST_LLVM_PREFIX"/bin/clang
if [ ! -x "$CC" ]; then
  echo "clang not found at HOST_LLVM_PREFIX/bin/clang (HOST_LLVM_PREFIX=$HOST_LLVM_PREFIX)" >&2
  exit 1
fi

# Note: If you are getting errors (like for example "redefinition of module 'libxml2'") and
# are building on macOS, try this to make sure you don't have two different clangs installed:
#   sudo rm -rf /Library/Developer/CommandLineTools
#   sudo xcode-select --install
#

# Requirements for building clang.
# https://llvm.org/docs/GettingStarted.html#software
#   CMake     >=3.13.4  Makefile/workspace generator
#   GCC       >=5.1.0 C/C++ compiler1
#   python    >=3.6 Automated test suite2
#   zlib      >=1.2.3.4 Compression library3
#   GNU Make  3.79, 3.79.1
#
# We use ninja, so we need that too

# -------------------------------------------------------------------------
# zlib

if [ ! -f "$DESTDIR"/zlib/lib/libz.a ]; then
  _download_pushsrc https://zlib.net/zlib-1.2.11.tar.xz e1cb0d5c92da8e9a8c2635dfa249c341dfd00322
  ./configure --static --prefix=
  make -j$(nproc)
  make check
  rm -rf "$DESTDIR/zlib"
  mkdir -p "$DESTDIR/zlib"
  make DESTDIR="$DESTDIR/zlib" install
  _popsrc
fi

# -------------------------------------------------------------------------
# llvm & clang

# LLVM_GIT_BRANCH=llvmorg-11.0.1
LLVM_GIT_BRANCH=llvmorg-12.0.0

# fetch or update llvm sources
SOURCE_CHANGED=true
_pushd "$DEPS_DIR"
if [ -d llvm-src ]; then
  if [ "$(git -C llvm-src describe --tags)" != "$LLVM_GIT_BRANCH" ]; then
    _pushd llvm-src
    git fetch origin
    echo git checkout "$LLVM_GIT_BRANCH"
         git checkout "$LLVM_GIT_BRANCH"
    _popd
  else
    SOURCE_CHANGED=false
  fi
else
  echo git clone --branch $LLVM_GIT_BRANCH https://github.com/llvm/llvm-project.git llvm-src
       git clone --branch $LLVM_GIT_BRANCH https://github.com/llvm/llvm-project.git llvm-src
fi
_popd

# _llvm_build <build-type> [args to cmake ...]
_llvm_build() {
  local build_type=$1 ;shift  # Debug | Release | RelWithDebInfo | MinSizeRel
  local build_dir=build-$build_type
  _pushd "$DEPS_DIR/llvm-src"
  ! $SOURCE_CHANGED || rm -rf $build_dir
  mkdir -p $build_dir
  _pushd $build_dir

  # cmake -G Ninja \
  #   -DCMAKE_BUILD_TYPE=$build_type \
  #   -DCMAKE_INSTALL_PREFIX= \
  #   -DCMAKE_C_COMPILER="$HOST_LLVM_PREFIX"/bin/clang \
  #   -DCMAKE_CXX_COMPILER="$HOST_LLVM_PREFIX"/bin/clang++ \
  #   -DCMAKE_ASM_COMPILER="$HOST_LLVM_PREFIX"/bin/clang \
  #   -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;BPF;Mips;RISCV;WebAssembly;X86" \
  #   -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" \
  #   -DLLVM_ENABLE_RUNTIMES="compiler-rt;libc;libcxx;libcxxabi;libunwind" \
  #   -DLLVM_ENABLE_MODULES=On \
  #   -DLLVM_ENABLE_BINDINGS=Off \
  #   -DLLVM_ENABLE_LIBXML2=OFF \
  #   -DLLVM_LIBC_ENABLE_LINTING=OFF \
  #   -DLLDB_ENABLE_CURSES=OFF \
  #   ../llvm

  local EXTRA_CMAKE_ARGS=()
  if command -v xcrun >/dev/null; then
    EXTRA_CMAKE_ARGS+=( -DDEFAULT_SYSROOT="$(xcrun --show-sdk-path)" )
  fi
  EXTRA_CMAKE_ARGS+=( "$@" )

  local LLVM_CFLAGS="-I$DESTDIR/zlib/include"
  local LLVM_LDFLAGS="-L$DESTDIR/zlib/lib"

  for _retry in 1 2; do
    if cmake -G Ninja \
      -DCMAKE_BUILD_TYPE=$build_type \
      -DCMAKE_INSTALL_PREFIX="$DESTDIR/llvm" \
      -DCMAKE_PREFIX_PATH="$DESTDIR/llvm" \
      -DCMAKE_C_COMPILER="$HOST_LLVM_PREFIX"/bin/clang \
      -DCMAKE_CXX_COMPILER="$HOST_LLVM_PREFIX"/bin/clang++ \
      -DCMAKE_ASM_COMPILER="$HOST_LLVM_PREFIX"/bin/clang \
      -DCMAKE_C_FLAGS="$LLVM_CFLAGS" \
      -DCMAKE_CXX_FLAGS="$LLVM_CFLAGS" \
      -DCMAKE_EXE_LINKER_FLAGS="$LLVM_LDFLAGS" \
      -DCMAKE_SHARED_LINKER_FLAGS="$LLVM_LDFLAGS" \
      -DCMAKE_MODULE_LINKER_FLAGS="$LLVM_LDFLAGS" \
      -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;AVR;BPF;Mips;RISCV;WebAssembly;X86" \
      -DLLVM_ENABLE_PROJECTS="clang;lld" \
      -DLLVM_ENABLE_MODULES=ON \
      -DLLVM_ENABLE_BINDINGS=OFF \
      -DLLVM_ENABLE_LIBXML2=OFF \
      -DLLVM_ENABLE_TERMINFO=OFF \
      -DCLANG_ENABLE_OBJC_REWRITER=OFF \
      "${EXTRA_CMAKE_ARGS[@]}" \
      ../llvm
    then
      break # ok; break retry loop
    fi
    [ $_retry = "1" ] || return 1
    # failure; retry
    echo "deleting CMakeCache.txt and retrying..."
    rm -f CMakeCache.txt
  done

  # See https://llvm.org/docs/CMake.html#llvm-specific-variables for documentation on
  # llvm cmake configuration.

  ninja

  # install
  echo "installing llvm at $DESTDIR/llvm"
  rm -rf "$DESTDIR/llvm"
  mkdir -p "$DESTDIR/llvm"
  # cmake -DCMAKE_INSTALL_PREFIX="$DESTDIR/llvm" -P cmake_install.cmake
  cmake --build . --target install
}

# _llvm_build Debug -DLLVM_ENABLE_ASSERTIONS=On
# _llvm_build Release -DLLVM_ENABLE_ASSERTIONS=On
# _llvm_build RelWithDebInfo -DLLVM_ENABLE_ASSERTIONS=On
# _llvm_build MinSizeRel -DLLVM_ENABLE_ASSERTIONS=Off
_llvm_build MinSizeRel -DLLVM_ENABLE_ASSERTIONS=On

# copy "driver" code (main program code) and patch it
_pushd "$PROJECT"
cp -v "$DEPS_DIR"/llvm-src/clang/tools/driver/driver.cpp     src/clang/driver.cc
cp -v "$DEPS_DIR"/llvm-src/clang/tools/driver/cc1_main.cpp   src/clang/driver_cc1_main.cc
cp -v "$DEPS_DIR"/llvm-src/clang/tools/driver/cc1as_main.cpp src/clang/driver_cc1as_main.cc
patch -p0 < misc/clang_driver.diff
#
# to make a new patch:
#   cp deps/llvm-src/clang/tools/driver/driver.cpp src/clang/driver.cc
#   cp src/clang/driver.cc src/clang/driver.cc.orig
#   edit src/clang/driver.cc
#   diff -u src/clang/driver.cc.orig src/clang/driver.cc > misc/clang_driver.diff
#

# copy clang C headers to lib/clang
echo "copy lib sources: llvm/lib/clang/*/include -> lib/clang"
rm -rf "$PROJECT"/lib/clang
mkdir -p "$PROJECT"/lib
cp -a "$DESTDIR"/llvm/lib/clang/*/include "$PROJECT"/lib/clang

# Copy headers & sources for lib/libcxx, lib/libcxxabi, lib/libunwind
for dir in libcxx libcxxabi libunwind; do
  echo "copy lib sources: llvm-src/$dir -> lib/$dir"
  dstdir="$PROJECT"/lib/$dir
  rm -rf "$dstdir"
  mkdir -p "$dstdir"
  cd "$DEPS_DIR"/llvm-src/$dir
  cp LICENSE.txt "$dstdir"/LICENSE.txt
  for f in \
    $(find include -not -name CMakeLists.txt) \
    $(find src -not -name CMakeLists.txt -and -not -path 'src/support/win32*')
  do
    if [ -d "$f" ]; then
      mkdir "$dstdir"/$f
    else
      cp -a $f "$dstdir"/$f
    fi
  done
done




# LLVM_ENABLE_PROJECTS full list:
#   clang;clang-tools-extra;compiler-rt;debuginfo-tests;libc;libclc;libcxx;
#   libcxxabi;libunwind;lld;lldb;openmp;parallel-libs;polly;pstl
#
# LLVM_TARGETS_TO_BUILD values:
# Generated from:
#   (cd deps/llvm-src/llvm/lib/Target && for f in *; do [ -d $f ] && echo $f; done)
# Note:
#   To list targets of an llvm installation, run `llc --version`
# -----
# LLVM_TARGETS_TO_BUILD="AArch64;AMDGPU" ...
#   AArch64
#   AMDGPU
#   ARC
#   ARM
#   AVR
#   BPF
#   Hexagon
#   Lanai
#   MSP430
#   Mips
#   NVPTX
#   PowerPC
#   RISCV
#   Sparc
#   SystemZ
#   VE
#   WebAssembly
#   X86
#   XCore
#

# Note: https://llvm.org/docs/CMake.html#llvm-specific-variables mentions
#   -static-libstdc++
# for statically linking with libstdc++


# It is possible to set a different install prefix at installation time
# by invoking the cmake_install.cmake script generated in the build directory:
# cmake -DCMAKE_INSTALL_PREFIX=/tmp/llvm -P cmake_install.cmake

# TODO patch above;
# -DCMAKE_INSTALL_PREFIX="$DEPS_DIR"
# -DCMAKE_C_FLAGS="$CFLAGS"
#
# -DLLVM_ENABLE_LIBXML2=OFF
# -DLLDB_ENABLE_CURSES=OFF
#
# Add comment with this url about cmake vars:
# https://llvm.org/docs/CMake.html#llvm-specific-variables
