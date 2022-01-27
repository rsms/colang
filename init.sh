#!/bin/sh
set -e
cd "$(dirname "$0")"
SCRIPT_FILE=$PWD/"$(basename "$0")"

# directory to house dependencies
DEPS=deps
DEPS_ABS=$PWD/$DEPS
HOST_SYS=$(uname -s)
HOST_ARCH=$(uname -m)
HOST_OS=
case "$(uname -a)" in
  *20.*Ubuntu*) HOST_OS=ubuntu-20 ;;
  *16.*Ubuntu*) HOST_OS=ubuntu-16 ;;
esac

_err() { echo -e "$0:" "$@" >&2 ; exit 1; }
_hascmd() { command -v "$1" >/dev/null || return 1; }

_needcmd() {
  while [ $# -gt 0 ]; do
    if ! _hascmd "$1"; then
      _err "missing $1 -- please install or use a different shell"
    fi
    shift
  done
}

# _git_dep <dir> <git-url> <git-hash>
_git_dep() {
  local DIR=$1 ; shift
  local GIT_URL=$1 ; shift
  local GIT_TREE=$1 ; shift
  local FORCE=false
  [ "$(cat "$DIR/git-version" 2>/dev/null)" != "$GIT_TREE" ] ||
    return 1
  [ -d "$DIR" ] && [ ! -d "$DIR/.git" ] && rm -rf "$DIR"
  if [ ! -d "$DIR" ]; then
    git clone "$GIT_URL" "$DIR"
    FORCE=true
  fi
  git -C "$DIR" fetch origin --tags
  local GIT_COMMIT=$(git -C "$DIR" rev-list -n 1 "$GIT_TREE")
  if $FORCE || [ "$(git -C "$DIR" rev-parse HEAD)" != "$GIT_COMMIT" ]; then
    git -C "$DIR" checkout --detach "$GIT_COMMIT" --
    return 0
  fi
  echo "$GIT_TREE" > "$DIR/git-version"
  return 1
}

# _sha256sum [<file>]
_sha256sum() { sha256sum "$@" | cut -f 1 -d ' '; }

# _verify_sha256 file expected_checksum
_verify_sha256() {
  echo "verifying sha256sum of ${1##$PWD/}"
  [ "$2" = "$(_sha256sum "$1")" ] || return 1
}

# _download url sha256-checksum [filename]
# Download file from url. If filename is not given (basename url) is used.
# If DEPS/download/filename exists, then only download if the checksum does not match.
_download() {
  local URL="$1"
  local CHECKSUM="$2"
  local CHECKSUM_ACTUAL=
  local FILENAME="$DEPS/download/$(basename "${3:-"$URL"}")"
  local DID_DOWNLOAD=
  while [ ! -e "$FILENAME" ] || ! _verify_sha256 "$FILENAME" "$CHECKSUM"; do
    if [ -n "$DID_DOWNLOAD" ]; then
      echo "Checksum for $FILENAME failed" >&2
      echo "  Actual:   $(_sha256sum "$FILENAME")" >&2
      echo "  Expected: $CHECKSUM" >&2
      return 1
    fi
    rm -rf "$FILENAME"
    echo "fetch $URL -> ${FILENAME##$PWD/}"
    mkdir -p "$(dirname "$FILENAME")"
    if _hascmd curl; then
      curl -L --progress-bar -o "$FILENAME" "$URL"
    else
      wget --show-progress -o "$FILENAME" "$URL"
    fi
    DID_DOWNLOAD=y
  done
}

# _downloaded_file filename|url -- Prints path to a file downloaded by _download
_downloaded_file() { echo "$DEPS/download/$(basename "$1")"; }

# _extract_tar tarfile outdir
_extract_tar() {
  local tarfile="$1"
  local outdir="$2"
  [ -e "$tarfile" ] || _err "$tarfile not found"
  local extract_dir="$DEPS/download/.extract-$(basename "$tarfile")"
  rm -rf "$extract_dir"
  mkdir -p "$extract_dir"
  echo "extracting ${tarfile##$PWD/} -> ${outdir##$PWD/}"
  XZ_OPT='-T0' tar -C "$extract_dir" -xf "$tarfile"
  rm -rf "$outdir"
  mkdir -p "$(dirname "$outdir")"
  mv -f "$extract_dir"/* "$outdir"
  rm -rf "$extract_dir"
}


# ---------- host utilities ----------
# check for programs and shell features needed
_needcmd \
  pushd popd basename head grep stat awk \
  tar git cmake ninja sha256sum python3
_hascmd curl || _hascmd wget || _err "curl nor wget found in PATH"

rm -f $DEPS/configured
mkdir -p $DEPS


echo "---------- llvm with clang & lld ----------"
LLVM_VERSION=13.0.0

if [ "$(cat $DEPS/llvm-version 2>/dev/null)" != $LLVM_VERSION ] ||
   [ ! -x $DEPS/llvm/bin/clang ]
then
  if [ "$HOST_SYS" = "Darwin" ]; then
    # macos release 13.0.0 from github.com/llvm/llvm-project/releases is broken,
    # it's not able to find libc on macOS. Instead we use llvm from Homebrew.
    LLVM_DID_INSTALL=
    while true; do
      SEARHDIRS=
      SEARHDIRS="$SEARHDIRS /opt/homebrew/opt/llvm"
      SEARHDIRS="$SEARHDIRS /usr/local/opt/llvm"
      rm -rf $DEPS/llvm
      for dir in $SEARHDIRS; do
        echo "try $dir/bin/clang"
        if [ -f "$dir/bin/clang" ] &&
           "$dir/bin/clang" --version >&2 >/dev/null
        then
          dir2="$("$dir/bin/clang" -print-search-dirs \
            | grep programs: | cut -d= -f2 | cut -d: -f1)"
          echo "$dir2/llvm-objcopy"
          if [ "$dir2" != "" ] && [ -f "$dir2/llvm-objcopy" ]; then
            ln -s "$(dirname "$dir2")" $DEPS/llvm
            break
          else
            echo "warning: skip candidate LLVM=$dir: no llvm-objcopy" >&2
          fi
        fi
      done
      if [ -x $DEPS/llvm/bin/clang ]; then
        break
      fi
      if [ -z $LLVM_DID_INSTALL ] && _hascmd brew; then
        echo "Did not find llvm in Homebrew directory; installing..."
        echo "brew install llvm"
              brew install llvm
        LLVM_DID_INSTALL=1
      else
        _err "llvm not found (tried: $SEARHDIRS). Install with: brew install llvm"
      fi
    done
  else # OS != macOS
    # fetch official prebuilt package from llvm-project
    LLVM_URL=https://github.com/llvm/llvm-project/releases/download
    LLVM_URL=$LLVM_URL/llvmorg-$LLVM_VERSION/clang+llvm-$LLVM_VERSION
    case "$HOST_SYS $HOST_ARCH $HOST_OS" in
    "Linux x86_64 ubuntu-20")
      LLVM_URL=$LLVM_URL-x86_64-linux-gnu-ubuntu-20.04.tar.xz
      LLVM_SHA256=2c2fb857af97f41a5032e9ecadf7f78d3eff389a5cd3c9ec620d24f134ceb3c8
      ;;
    "Linux x86_64 ubuntu-16")
      LLVM_URL=$LLVM_URL-x86_64-linux-gnu-ubuntu-16.04.tar.xz
      LLVM_SHA256=76d0bf002ede7a893f69d9ad2c4e101d15a8f4186fbfe24e74856c8449acd7c1
      ;;
    *)
      _err "Don't know how or where to get LLVM for $HOST_SYS $HOST_ARCH ($HOST_OS)"
      ;;
    esac
    _download "$LLVM_URL" $LLVM_SHA256
    _extract_tar "$(_downloaded_file "$LLVM_URL")" $DEPS/llvm
  fi
  echo $LLVM_VERSION > $DEPS/llvm-version
  rm -f $DEPS/cc-tested # needs testing
fi
echo "ready: $DEPS/llvm ($($DEPS/llvm/bin/clang --version | head -n1))"
echo "export PATH=$DEPS_ABS/llvm/bin:\$PATH"

export PATH=$DEPS_ABS/llvm/bin:$PATH
export CC=clang
export CXX=clang++

for cmd in clang clang++ llvm-objcopy llvm-ar lld; do
  if ! _hascmd $cmd; then
    rm -f $DEPS/llvm-version  # make next run update llvm
    _err "$cmd missing in PATH; LLVM installation broken"
  fi
done


# ---------- test compiler ----------
if ! [ -f $DEPS/cc-tested ]; then
  CC_TEST_DIR=out/cc-test
  rm -rf $CC_TEST_DIR
  mkdir -p $CC_TEST_DIR
  pushd $CC_TEST_DIR >/dev/null
  echo "Running tests in $CC_TEST_DIR"

  cat << _END_ > hello.c
#include <stdio.h>
int main(int argc, const char** argv) {
  printf("hello from %s\n", argv[0]);
  return 0;
}
_END_

  cat << _END_ > hello.cc
#include <iostream>
int main(int argc, const char** argv) {
  std::cout << "hello from " << argv[0] << "\n";
  return 0;
}
_END_

  printf "CC=";  command -v $CC
  printf "CXX="; command -v $CXX

  CC_TEST_STATIC=1
  if [ "$HOST_SYS" = "Darwin" ]; then
    # can't link libc statically on mac
    CC_TEST_STATIC=
  fi

  echo "Compile C and C++ test programs:"
  set -x
  $CC  -Wall -std=c17     -O2         -o hello_c_d  hello.c
  $CXX -Wall -std=gnu++17 -O2         -o hello_cc_d hello.cc
  if [ -n "$CC_TEST_STATIC" ]; then
    $CC  -Wall -std=c17     -O2 -static -o hello_c_s  hello.c
    $CXX -Wall -std=gnu++17 -O2 -static -o hello_cc_s hello.cc
  fi
  set +x
  echo "Compile test programs: OK"

  echo "Run test programs:"
  for f in hello_c_d hello_cc_d; do
    ./${f}
    strip -o stipped_$f $f
    ./stipped_$f
  done
  if [ -n "$CC_TEST_STATIC" ]; then
    for f in hello_c_s hello_cc_s; do
      ./${f}
      strip -o stipped_$f $f
      ./stipped_$f
    done
  fi
  echo "Run test programs: OK"

  echo "Verifying linking..."
  LD_VERIFICATION_TOOL=llvm-objdump
  if [ "$HOST_SYS" = "Darwin" ]; then
    _needcmd otool
    LD_VERIFICATION_TOOL=otool
    _has_dynamic_linking() { otool -L "$1" | grep -q .dylib; }
  else
    _has_dynamic_linking() { llvm-objdump -p "$1" | grep -q NEEDED; }
  fi
  _has_dynamic_linking hello_c_d  || _err "hello_c_d is statically linked!"
  _has_dynamic_linking hello_cc_d || _err "hello_cc_d is statically linked!"
  if [ -n "$CC_TEST_STATIC" ]; then
    _has_dynamic_linking hello_c_s  && _err "hello_c_s has dynamic linking!"
    _has_dynamic_linking hello_cc_s && _err "hello_cc_s has dynamic linking!"
    echo "Verified static and dynamic linking with $LD_VERIFICATION_TOOL: OK"
  else
    echo "Verified dynamic linking with $LD_VERIFICATION_TOOL: OK"
  fi

  popd >/dev/null
  echo "All OK -- CC=$CC CXX=$CXX works correctly"
  echo "To re-run tests: rm $DEPS/cc-tested && $0"
  touch $DEPS/cc-tested
fi

# -----------------------------------------------------------------------


echo "---------- luajit ----------"
LUAJIT_REV=v2.1.0-beta3  # See https://repo.or.cz/luajit-2.0.git/tags
if _git_dep $DEPS/luajit https://luajit.org/git/luajit.git $LUAJIT_REV; then
  # macOS x86_64: must build with LUAJIT_ENABLE_GC64 or luaL_newstate will fail
  ( cd $DEPS/luajit &&
    make clean &&
    MACOSX_DEPLOYMENT_TARGET=10.15 CFLAGS="-DLUAJIT_ENABLE_GC64" make -j$(nproc) )
fi
echo "ready: $DEPS/luajit (git $LUAJIT_REV)"



touch $DEPS/configured
