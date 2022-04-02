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
LLVM_GIT_REV=llvmorg-14.0.0
LLVM_VERSION=${LLVM_GIT_REV#*-}
LLVM_DESTDIR=$DESTDIR/llvm
LLVM_GIT_URL=https://github.com/llvm/llvm-project.git
LLVM_SRCDIR=$DEPS_DIR/llvm-src
LLVM_BUILD_MODE=MinSizeRel
LLVM_ENABLE_ASSERTIONS=On
# LLVM components (libraries) to include. See deps/llvm/bin/llvm-config --components
# windowsmanifest: needed for lld COFF
LLVM_COMPONENTS=(
  engine \
  option \
  passes \
  all-targets \
  libdriver \
  lto \
  linker \
  debuginfopdb \
  debuginfodwarf \
  windowsmanifest \
  orcjit \
)

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

DEPS_CHANGED=false

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
  DEPS_CHANGED=true
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
  DEPS_CHANGED=true
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
  DEPS_CHANGED=true
fi

# -------------------------------------------------------------------------
# libxml2 (required by xar)

LIBXML2_VERSION=2.9.13
LIBXML2_CHECKSUM=7dced00d88181d559ee76c6d8ef4571eb1bd0b26
LIBXML2_DESTDIR=$DESTDIR/libxml2

if $DEPS_CHANGED || [ ! -f "$LIBXML2_DESTDIR/lib/libxml2.a" ] ||
   [ "$(cat "$LIBXML2_DESTDIR/version" 2>/dev/null)" != "$LIBXML2_VERSION" ]
then
  _download_pushsrc \
    https://download.gnome.org/sources/libxml2/${LIBXML2_VERSION%.*}/libxml2-$LIBXML2_VERSION.tar.xz \
    $LIBXML2_CHECKSUM

  # setup.py is generated
  rm python/setup.py

  # We don't build libxml2 with icu.
  rm test/icu_parse_test.xml

  # note: need to use --prefix instead of DESTDIR during install
  # for xml2-config to function properly
  ./configure \
    "--prefix=$LIBXML2_DESTDIR" \
    --enable-static \
    --disable-shared \
    --disable-dependency-tracking \
    \
    --without-catalog      \
    --without-debug        \
    --without-docbook      \
    --without-ftp          \
    --without-http         \
    --without-html         \
    --without-html-dir     \
    --without-html-subdir  \
    --without-iconv        \
    --without-history      \
    --without-legacy       \
    --without-python       \
    --without-readline     \
    --without-modules      \
    "--with-lzma=$XC_DESTDIR" \
    "--with-zlib=$ZLIB_DESTDIR" \

  make -j$(nproc)

  rm -rf "$LIBXML2_DESTDIR"
  mkdir -p "$LIBXML2_DESTDIR"
  make install

  echo "$LIBXML2_VERSION" > "$LIBXML2_DESTDIR/version"
  _popsrc
  DEPS_CHANGED=true
fi

# -------------------------------------------------------------------------
# xar (required by lld's mach-o linker, liblldMachO2.a)

XAR_SRCDIR=$ETC_LLVM_DIR/xar
XAR_VERSION=$(cat "$XAR_SRCDIR/version")
if $DEPS_CHANGED || [ ! -f "$XAR_DESTDIR/lib/libxar.a" ] ||
   [ "$(cat "$XAR_DESTDIR/version" 2>/dev/null)" != "$XAR_VERSION" ]
then
  _pushd "$XAR_SRCDIR"

  CFLAGS="-I$OPENSSL_DESTDIR/include -I$ZLIB_DESTDIR/include -I$LIBXML2_DESTDIR/include" \
  CPPFLAGS="-I$OPENSSL_DESTDIR/include -I$ZLIB_DESTDIR/include -I$LIBXML2_DESTDIR/include" \
  LDFLAGS="-L$OPENSSL_DESTDIR/lib -L$ZLIB_DESTDIR/lib -L$LIBXML2_DESTDIR/lib" \
  ./configure \
    --prefix= \
    --enable-static \
    --disable-shared \
    --with-lzma=$XC_DESTDIR \
    --with-xml2-config=$LIBXML2_DESTDIR/bin/xml2-config \
    --without-bzip2

  make -j$(nproc)

  rm -rf "$XAR_DESTDIR"
  mkdir -p "$XAR_DESTDIR"
  make DESTDIR="$XAR_DESTDIR" install
  # rm -rf "$XAR_DESTDIR/bin" "$XAR_DESTDIR/share"

  echo "$XAR_VERSION" > "$XAR_DESTDIR/version"
  _popd
  DEPS_CHANGED=true
fi

# -------------------------------------------------------------------------
# llvm & clang

LLVM_BUILD_DIR=$LLVM_SRCDIR/build-$LLVM_BUILD_MODE

# _llvm_build <build-type> [args to cmake ...]
_llvm_build() {
  local build_type=$1 ;shift  # Debug | Release | RelWithDebInfo | MinSizeRel
  local build_dir=$(basename "$LLVM_BUILD_DIR")
  _pushd "$LLVM_SRCDIR"
  # $SOURCE_CHANGED && rm -rf $build_dir
  mkdir -p $build_dir
  _pushd $build_dir
  rm -rf co-objx

  local EXTRA_CMAKE_ARGS=()
  if command -v xcrun >/dev/null; then
    EXTRA_CMAKE_ARGS+=( -DDEFAULT_SYSROOT="$(xcrun --show-sdk-path)" )
  fi

  LLVM_ENABLE_PROJECTS="clang;lld"

  if $CO_LLVM_BUILD_COMPILER_RT; then
    LLVM_ENABLE_PROJECTS="$LLVM_ENABLE_PROJECTS;compiler-rt"
    EXTRA_CMAKE_ARGS+=( \
      -DCOMPILER_RT_BUILD_XRAY=OFF \
      -DCOMPILER_RT_CAN_EXECUTE_TESTS=OFF \
      -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
      -DSANITIZER_USE_STATIC_CXX_ABI=ON \
      -DSANITIZER_USE_STATIC_LLVM_UNWINDER=ON \
      -DCOMPILER_RT_USE_BUILTINS_LIBRARY=ON \
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
      -DCLANG_ENABLE_ARCMT=OFF \
      -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
      -DLIBCLANG_BUILD_STATIC=ON \
      \
      -DLIBCXXABI_ENABLE_SHARED=OFF \
      -DLIBCXXABI_INCLUDE_TESTS=OFF \
      -DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON \
      -DLIBCXXABI_LINK_TESTS_WITH_SHARED_LIBCXXABI=OFF \
      \
      -DLIBCXX_ENABLE_SHARED=OFF \
      -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
      -DLIBCXX_LINK_TESTS_WITH_SHARED_LIBCXX=OFF \
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


_mk_dlib_macos() {
  local DLIB_FILE=$LLVM_DESTDIR/lib/libco-llvm-bundle-d.dylib
  local LIB_VERSION=0.0.1        # used for mach-o dylib
  local LIB_VERSION_COMPAT=0.0.1 # used for mach-o dylib
  local LIBFILES=(
    $("$LLVM_DESTDIR/bin/llvm-config" --link-static --libfiles "${LLVM_COMPONENTS[@]}") \
    $ZLIB_DESTDIR/lib/libz.a \
  )
  local MACOS_FRAMEWORKS=()
  local EXTRA_LDFLAGS=( -lc++ )

  local OBJXDIR=$LLVM_BUILD_DIR/co-objx
  local OBJFILES=()
  # rm -rf "$OBJXDIR"
  mkdir -p "$OBJXDIR"
  _pushd "$OBJXDIR"
  local f name
  for f in "${LIBFILES[@]}"; do
    name=$(basename "$f")
    if ! [ -d "$name" -a "$name" -nt "$f" ]; then
      mkdir "$name"
      cd "$name"
      echo "extract $name"
      $LLVM_DESTDIR/bin/llvm-ar x "$f"
      cd ..
    fi
    OBJFILES+=( $PWD/$name/*.o )
  done
  _popd
  echo "${#OBJFILES[@]} OBJFILES"

  local SYMS_FILE=$WORK_DIR/libco-llvm-bundle.syms
  cat << _END_ > "$SYMS_FILE"
# this first explicit symbol acts as a test:
# if it is not found, building the dylib will fail with an error.
#_wgpuRenderPassEncoderEndPass

# all symbols with the prefix "wgpu" are made public
_llvm*

# the rest of the symbols are made local/internal
_END_
  # EXTRA_LDFLAGS+=( -exported_symbols_list "$SYMS_FILE" )

  echo "create ${DLIB_FILE##$PWD/}"

  ld64.lld -dylib \
    -o $DLIB_FILE \
    --color-diagnostics \
    --lto-O3 \
    -install_name "@rpath/$(basename "$DLIB_FILE")" \
    -current_version "$LIB_VERSION" \
    -compatibility_version "$LIB_VERSION_COMPAT" \
    -cache_path_lto "$WORK_DIR/llvm-dylib-lto.cache" \
    -arch $(uname -m) \
    -ObjC \
    -platform_version macos 10.15 10.15 \
    \
    -lSystem.B \
    "${EXTRA_LDFLAGS[@]}" \
    "${OBJFILES[@]}" \
    "${MACOS_FRAMEWORKS[@]}"
}


_mk_alib_macos() {
  local OUTFILE=$LLVM_DESTDIR/lib/libco-llvm-bundle.a
  local MRIFILE=$WORK_DIR/$(basename "$OUTFILE").mri
  local LIBFILES=(
    $("$LLVM_DESTDIR/bin/llvm-config" --link-static --libfiles "${LLVM_COMPONENTS[@]}") \
    $ZLIB_DESTDIR/lib/libz.a \
  )
    # $XC_DESTDIR/lib/liblzma.a \
    # $OPENSSL_DESTDIR/lib/libcrypto.a \
    # $XAR_DESTDIR/lib/libxar.a \

  echo "create ${OUTFILE##$PWD/}"
  # lld (llvm 13) does not yet support prelinking, so we produce an archive instead

  mkdir -p "$(dirname "$MRIFILE")"
  echo "create $OUTFILE" > "$MRIFILE"
  for f in "${LIBFILES[@]}"; do
    echo "addlib $f" >> "$MRIFILE"
  done
  echo "save" >> "$MRIFILE"
  echo "end" >> "$MRIFILE"
  # cat "$MRIFILE"
  llvm-ar -M < "$MRIFILE"
  llvm-ranlib "$OUTFILE"
}

_mk_lib_bundles() {
  case "$(uname -s)" in
    Darwin)
      _mk_alib_macos
      _mk_dlib_macos
      ;;
    *)
      _err "lib bundling not implemented for $(uname -s)"
      ;;
  esac
}


# fetch or update llvm sources
SOURCE_CHANGED=false
if _git_pull_if_needed "$LLVM_GIT_URL" "$LLVM_SRCDIR" "$LLVM_GIT_REV"; then
  SOURCE_CHANGED=true
fi
if $FORCE || $DEPS_CHANGED || $SOURCE_CHANGED ||
   [ ! -f "$LLVM_DESTDIR/lib/libLLVMCore.a" ]
 then
  _llvm_build $LLVM_BUILD_MODE -DLLVM_ENABLE_ASSERTIONS=$LLVM_ENABLE_ASSERTIONS
  _update_myclang
  _mk_lib_bundles

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
