#!/bin/bash
set -e
INITIAL_PWD=$PWD
cd "$(dirname "$0")"
ETC_LLVM_DIR=$PWD
. ../lib.sh
cd ../..  # to PROJECT


# DESTDIR: where to install stuff
# This is a prefix; each project is installed in a subdirectory, e.g. DESTDIR/zlib.
DESTDIR="$DEPS_DIR"
MYCLANG_DIR=$ETC_LLVM_DIR/myclang
mkdir -p "$DESTDIR"

# what git ref to build (commit, tag or branch)
LLVM_GIT_BRANCH=llvmorg-14.0.0
LLVM_VERSION=${LLVM_GIT_BRANCH#*-}
LLVM_DESTDIR=$DESTDIR/llvm
LLVM_GIT_URL=https://github.com/llvm/llvm-project.git
LLVM_SRCDIR=$DEPS_DIR/llvm-src
LLVM_BUILD_MODE=MinSizeRel
LLVM_ENABLE_ASSERTIONS=On

ZLIB_VERSION=1.2.12
ZLIB_CHECKSUM=207ba741d387e2c1607104cf0bd8cff27deb2605
ZLIB_DESTDIR=$DESTDIR/zlib

XC_VERSION=5.2.5
XC_CHECKSUM=0b9d1e06b59f7fe0796afe1d93851b9306b4a3b6
XC_DESTDIR=$DESTDIR/xc

OPENSSL_VERSION=1.1.1n
OPENSSL_CHECKSUM=4b0936dd798f60c97c68fc62b73033ecba6dfb0c
OPENSSL_DESTDIR=$DESTDIR/openssl

XAR_DESTDIR=$DESTDIR/xar

FORCE=false

# CO_LLVM_BUILD_COMPILER_RT: Enables building the compiler-rt suite with llvm.
# This is only required for co debug builds as it provides sanitizer runtimes.
# Building compiler-rt makes the build SIGNIFICANTLY slower (~7k extra sources.)
CO_LLVM_BUILD_COMPILER_RT=true

while [[ $# -gt 0 ]]; do case "$1" in
  -h|--help) cat << _END
usage: $0 [options]
Builds LLVM version ${LLVM_VERSION}
options:
  -no-compiler-rt  Do not build compiler-rt
  -no-assertions   Do not include assertions
  -force           Build even if it seems that build products are up to date
  -mode=<mode>     One of: Debug, Release, RelWithDebInfo, MinSizeRel (default)
  -quiet           Log less information
  -help            Show help on stdout and exit
_END
    exit ;;
  -no-compiler-rt)  CO_LLVM_BUILD_COMPILER_RT=false; shift ;;
  -no-assertions)   LLVM_ENABLE_ASSERTIONS=Off; shift ;;
  -force)           FORCE=true; shift ;;
  -mode=*)          LLVM_BUILD_MODE=${1:6}; shift ;;
  -quiet)           OPT_QUIET=true; shift ;;
  --) break ;;
  -*) _err "unknown option: $1" ;;
  *) break ;;
esac; done


# Host compiler location; prefer clang, fall back to $CC ("cc" in PATH as last resort)
HOST_CC=${HOST_CC}
HOST_CXX=${HOST_CXX}
HOST_ASM=${HOST_ASM}
if [ -z "$HOST_CC" ]; then
  if [ -x /usr/local/opt/llvm/bin/clang ]; then
    HOST_CC=/usr/local/opt/llvm/bin/clang
    HOST_CXX=/usr/local/opt/llvm/bin/clang++
  elif [ -x /opt/homebrew/opt/llvm/bin/clang ]; then
    HOST_CC=/opt/homebrew/opt/llvm/bin/clang
    HOST_CXX=/opt/homebrew/opt/llvm/bin/clang++
  else
    clangpath="$(command -v clang)"
    if [ -n "$clangpath" ]; then
      HOST_CC=$clangpath
      HOST_CXX=$(command -v clang++)
    else
      HOST_CC=$(command -v "${CC:-cc}")
      HOST_CXX=$(command -v "${CXX:-c++}")
      [ -x "$HOST_CC" ] ||
        _err "no host compiler found. Set HOST_CC or add clang or cc to PATH"
    fi
  fi
fi
[ -z "$HOST_ASM" ] && HOST_ASM=$HOST_CC
[ -x "$HOST_CC" ] || _err "${HOST_CC} is not an executable file"
export CC=${HOST_CC}
export CXX=${HOST_CXX}
export ASM=${HOST_ASM}


# Note: If you are getting errors (like for example "redefinition of module 'libxml2'") and
# are building on macOS, try this to make sure you don't have two different clangs installed:
#   sudo rm -rf /Library/Developer/CommandLineTools
#   sudo xcode-select --install
#

# Requirements for building clang.
# https://llvm.org/docs/GettingStarted.html#software
#   CMake     >=3.13.4  Makefile/workspace generator
#   GCC       >=5.1.0 C/C++ compiler
#   python    >=3.6 Automated test suite
#   zlib      >=1.2.3.4 Compression library
#   GNU Make  3.79, 3.79.1
#
# We use ninja, so we need that too.
# Oh, and openssl needs perl to build, lol.

# -------------------------------------------------------------------------
# zlib (required by llvm)

if [ ! -f "$ZLIB_DESTDIR/lib/libz.a" ] ||
   [ "$(cat "$ZLIB_DESTDIR/version" 2>/dev/null)" != "$ZLIB_VERSION" ]
then
  _download_pushsrc https://zlib.net/zlib-${ZLIB_VERSION}.tar.gz $ZLIB_CHECKSUM

  ./configure --static --prefix=

  make -j$(nproc)
  make check

  rm -rf "$ZLIB_DESTDIR"
  mkdir -p "$ZLIB_DESTDIR"
  make DESTDIR="$ZLIB_DESTDIR" install

  echo "$ZLIB_VERSION" > "$ZLIB_DESTDIR/version"
  _popsrc
fi

# -------------------------------------------------------------------------
# xc (liblzma required by xar)

if [ ! -f "$XC_DESTDIR/lib/liblzma.a" ] ||
   [ "$(cat "$XC_DESTDIR/version" 2>/dev/null)" != "$XC_VERSION" ]
then
  _download_pushsrc https://tukaani.org/xz/xz-$XC_VERSION.tar.xz $XC_CHECKSUM

  ./configure \
    --prefix= \
    --enable-static \
    --disable-shared \
    --disable-rpath \
    --disable-werror \
    --disable-doc \
    --disable-nls \
    --disable-dependency-tracking \
    --disable-xz \
    --disable-xzdec \
    --disable-lzmadec \
    --disable-lzmainfo \
    --disable-lzma-links \
    --disable-scripts \
    --disable-doc

  make -j$(nproc)
  LD_LIBRARY_PATH="$PWD/src/liblzma/.libs" make check

  rm -rf "$XC_DESTDIR"
  mkdir -p "$XC_DESTDIR"
  make DESTDIR="$XC_DESTDIR" install
  rm -rf "$XC_DESTDIR/bin"

  echo "$XC_VERSION" > "$XC_DESTDIR/version"
  _popsrc
fi

# -------------------------------------------------------------------------
# openssl (required by xar)

if [ ! -f "$OPENSSL_DESTDIR/lib/libcrypto.a" ] ||
   [ "$(cat "$OPENSSL_DESTDIR/version" 2>/dev/null)" != "$OPENSSL_VERSION" ]
then
  _download_pushsrc \
    https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz $OPENSSL_CHECKSUM

  ./config \
    --prefix=/ \
    --libdir=lib \
    --openssldir=/etc/ssl \
    no-shared \
    no-zlib \
    no-async \
    no-comp \
    no-idea \
    no-mdc2 \
    no-rc5 \
    no-ec2m \
    no-sm2 \
    no-sm4 \
    no-ssl2 \
    no-ssl3 \
    no-seed \
    no-weak-ssl-ciphers \
    -Wa,--noexecstack

  make -j$(nproc)

  rm -rf "$OPENSSL_DESTDIR"
  mkdir -p "$OPENSSL_DESTDIR"
  make DESTDIR="$OPENSSL_DESTDIR" install_sw

  echo "$OPENSSL_VERSION" > "$OPENSSL_DESTDIR/version"
  _popsrc
fi

# -------------------------------------------------------------------------
# xar (required by lld's mach-o linker, liblldMachO2.a)

XAR_SRCDIR=deps/xar-src
XAR_VERSION=$(cat "$XAR_SRCDIR/version")
if [ ! -f "$XAR_DESTDIR/lib/libxar.a" ] ||
   [ "$(cat "$XAR_DESTDIR/version" 2>/dev/null)" != "$XAR_VERSION" ]
then
  _pushd deps/xar-src

  CFLAGS="-I$OPENSSL_DESTDIR/include -I$ZLIB_DESTDIR/include -I$XC_DESTDIR/include" \
  CPPFLAGS="-I$OPENSSL_DESTDIR/include -I$ZLIB_DESTDIR/include -I$XC_DESTDIR/include" \
  LDFLAGS="-L$OPENSSL_DESTDIR/lib -L$ZLIB_DESTDIR/lib -L$XC_DESTDIR/lib" \
  ./configure --enable-static --disable-shared --prefix=

  make -j$(nproc)

  rm -rf "$XAR_DESTDIR"
  mkdir -p "$XAR_DESTDIR"
  make DESTDIR="$XAR_DESTDIR" install
  # rm -rf "$XAR_DESTDIR/bin" "$XAR_DESTDIR/share"

  echo "$XAR_VERSION" > "$XAR_DESTDIR/version"
  _popd
fi

# -------------------------------------------------------------------------
# llvm & clang

# fetch or update llvm sources
SOURCE_CHANGED=false
if _git_pull_if_needed "$LLVM_GIT_URL" "$LLVM_SRCDIR" "$LLVM_GIT_BRANCH"; then
  SOURCE_CHANGED=true
fi

# _llvm_build <build-type> [args to cmake ...]
_llvm_build() {
  local build_type=$1 ;shift  # Debug | Release | RelWithDebInfo | MinSizeRel
  local build_dir=build-$build_type
  _pushd "$LLVM_SRCDIR"
  ! $SOURCE_CHANGED || rm -rf $build_dir
  mkdir -p $build_dir
  _pushd $build_dir

  local EXTRA_CMAKE_ARGS=()
  if command -v xcrun >/dev/null; then
    EXTRA_CMAKE_ARGS+=( -DDEFAULT_SYSROOT="$(xcrun --show-sdk-path)" )
  fi

  LLVM_ENABLE_PROJECTS="clang;lld"

  if $CO_LLVM_BUILD_COMPILER_RT; then
    LLVM_ENABLE_PROJECTS="$LLVM_ENABLE_PROJECTS;compiler-rt"
    EXTRA_CMAKE_ARGS+=( \
      -DCOMPILER_RT_BUILD_XRAY=OFF \
      -DCOMPILER_RT_USE_LIBCXX=OFF \
      -DCOMPILER_RT_CAN_EXECUTE_TESTS=OFF \
      -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
      -DCOMPILER_RT_BUILD_CRT=OFF \
      -DCOMPILER_RT_BUILD_PROFILE=OFF \
      -DCOMPILER_RT_BUILD_MEMPROF=OFF \
      -DSANITIZER_USE_STATIC_CXX_ABI=ON \
    )
  fi

  EXTRA_CMAKE_ARGS+=( "$@" )

  local LLVM_CFLAGS LLVM_LDFLAGS
  LLVM_CFLAGS="-I$ZLIB_DESTDIR/include"    ; LLVM_LDFLAGS="-L$ZLIB_DESTDIR/lib"
  LLVM_CFLAGS="-I$OPENSSL_DESTDIR/include" ; LLVM_LDFLAGS="-L$OPENSSL_DESTDIR/lib"
  LLVM_CFLAGS="-I$XC_DESTDIR/include"      ; LLVM_LDFLAGS="-L$XC_DESTDIR/lib"

  for _retry in 1 2; do
    if cmake -G Ninja \
      -DCMAKE_BUILD_TYPE=$build_type \
      -DCMAKE_INSTALL_PREFIX="$LLVM_DESTDIR" \
      -DCMAKE_PREFIX_PATH="$LLVM_DESTDIR" \
      -DCMAKE_C_COMPILER="$HOST_CC" \
      -DCMAKE_CXX_COMPILER="$HOST_CXX" \
      -DCMAKE_ASM_COMPILER="$HOST_ASM" \
      -DCMAKE_C_FLAGS="$LLVM_CFLAGS" \
      -DCMAKE_CXX_FLAGS="$LLVM_CFLAGS" \
      -DCMAKE_EXE_LINKER_FLAGS="$LLVM_LDFLAGS" \
      -DCMAKE_SHARED_LINKER_FLAGS="$LLVM_LDFLAGS" \
      -DCMAKE_MODULE_LINKER_FLAGS="$LLVM_LDFLAGS" \
      \
      -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;Mips;RISCV;WebAssembly;X86" \
      -DLLVM_ENABLE_PROJECTS="$LLVM_ENABLE_PROJECTS" \
      -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
      -DLLVM_ENABLE_MODULES=OFF \
      -DLLVM_ENABLE_BINDINGS=OFF \
      -DLLVM_ENABLE_LIBXML2=OFF \
      -DLLVM_ENABLE_TERMINFO=OFF \
      -DLLVM_INCLUDE_UTILS=OFF \
      -DLLVM_INCLUDE_TESTS=OFF \
      -DLLVM_INCLUDE_GO_TESTS=OFF \
      -DLLVM_INCLUDE_EXAMPLES=OFF \
      -DLLVM_INCLUDE_BENCHMARKS=OFF \
      -DLLVM_ENABLE_OCAMLDOC=OFF \
      -DLLVM_ENABLE_Z3_SOLVER=OFF \
      -DLLVM_INCLUDE_DOCS=OFF \
      \
      -DCLANG_INCLUDE_DOCS=OFF \
      -DCLANG_ENABLE_OBJC_REWRITER=OFF \
      -DLIBCLANG_BUILD_STATIC=ON \
      \
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
  # Note: We could do a second pass and build with -DLLVM_BUILD_STATIC=ON now when
  # we have a static libcxx

  ninja

  # install
  _log "installing llvm at $(_relpath "$LLVM_DESTDIR")"
  rm -rf "$LLVM_DESTDIR"
  mkdir -p "$LLVM_DESTDIR"
  # cmake -DCMAKE_INSTALL_PREFIX="$DESTDIR/llvm" -P cmake_install.cmake
  cmake --build . --target install

} # _llvm_build


_update_myclang() {
  # myclang: copy "driver" code (main program code) and patch it
  _pushd "$ETC_LLVM_DIR"
  cp -v "$LLVM_SRCDIR"/clang/tools/driver/driver.cpp     $MYCLANG_DIR/driver.cc
  cp -v "$LLVM_SRCDIR"/clang/tools/driver/cc1_main.cpp   $MYCLANG_DIR/driver_cc1_main.cc
  cp -v "$LLVM_SRCDIR"/clang/tools/driver/cc1as_main.cpp $MYCLANG_DIR/driver_cc1as_main.cc
  # patch driver code
  for f in $(echo "$ETC_LLVM_DIR"/llvm-${LLVM_VERSION}-*.patch | sort); do
    [ -e "$f" ] || _err "no patches found at $ETC_LLVM_DIR/llvm-${LLVM_VERSION}-*.patch"
    [ -f "$f" ] || _err "$f is not a file"
    patch -p0 < "$f"
  done
  # to make a new patch:
  #   cd etc/llvm
  #   cp ../../deps/llvm-src/clang/tools/driver/driver.cpp myclang/driver.cc
  #   cp myclang/driver.cc myclang/driver.cc.orig
  #   # edit myclang/driver.cc
  #   diff -u myclang/driver.cc.orig myclang/driver.cc > llvm-LLVM_VERSION-001-myclang.patch
  #
}


if $FORCE || $SOURCE_CHANGED || [ ! -f "$LLVM_DESTDIR/lib/libLLVMCore.a" ]; then
  _llvm_build $LLVM_BUILD_MODE -DLLVM_ENABLE_ASSERTIONS=$LLVM_ENABLE_ASSERTIONS
  _update_myclang

  # # copy clang C headers to lib/clang
  # echo "copy lib sources: llvm/lib/clang/*/include -> lib/clang"
  # rm -rf "$PROJECT"/lib/clang
  # mkdir -p "$PROJECT"/lib
  # cp -a "$DESTDIR"/llvm/lib/clang/*/include "$PROJECT"/lib/clang

  # # Copy headers & sources for lib/libcxx, lib/libcxxabi, lib/libunwind
  # TODO: this moved to build-libcxx.sh but we may want to separate the source file
  #       copying from the actual build step, so consider breaking build-libcxx.sh apart
  #       into two separate steps ("copy sources" and "build libs".)
else
  REBUILD_ARGS=( "$@" -force )
  _log "$(_relpath "$LLVM_DESTDIR") is up to date. To rebuild: $0 ${REBUILD_ARGS[@]}"
fi


#—— END —————————————————————————————————————————————————————————————————————————————————
#
# notes & etc (rest of this file)
#

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