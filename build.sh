#!/bin/bash
set -e
ORIG_PWD=$PWD
PROG=$0
cd "$(dirname "$0")"
_err() { echo "$PROG:" "$@" >&2 ; exit 1; }
_checksum() { sha256sum "$@" | cut -d' ' -f1; }

OUTDIR=build
SRCDIR=src
MAIN_EXE=co
PP_PREFIX=CO_
WASM_SYMS=etc/wasm.syms
WITH_LLVM=
XFLAGS=( $XFLAGS )
CFLAGS=( $CFLAGS -fms-extensions -Wno-microsoft )
CXXFLAGS=()
DEPSDIR=$PWD/deps
NINJA=${NINJA:-ninja}
WATCHFILES=( $SRCDIR $(basename "$0") examples )

BUILD_MODE=safe ; DEBUG=false
WATCH= ; _WATCHED=
RUN=
NINJA_ARGS=()
ONLY_CONFIGURE=false
TESTING_ENABLED=false
VERBOSE=false

while [[ $# -gt 0 ]]; do case "$1" in
  -h|-help|--help) cat << _END
usage: $0 [options] [--] [<target-or-arg-to-ninja> ...]
options:
  -safe        Enable optimizations and safe checks (default)
  -fast        Enable optimizations without any safe checks
  -debug       No optimizations, checks and assertions enabled
  -w           Rebuild as sources change
  -run=<cmd>   Run <cmd> after successful build
  -config      Just configure, don't build
  -llvm=<how>  off | static (default) | shared (default for debug)
  -v           Verbose; log more details
  -help        Show help on stdout and exit
_END
    exit ;;
  -safe)    BUILD_MODE=safe; TESTING_ENABLED=false; shift ;;
  -fast)    BUILD_MODE=fast; TESTING_ENABLED=false; shift ;;
  -debug)   BUILD_MODE=debug; TESTING_ENABLED=true; shift ;;
  -w)       WATCH=1; shift ;;
  -_w_)     _WATCHED=1; shift ;;
  -run=*)   RUN=${1:5}; shift ;;
  -config)  ONLY_CONFIGURE=true; shift ;;
  -llvm=*)  WITH_LLVM=${1:6}; shift ;;
  -v)       VERBOSE=true; NINJA_ARGS+=(-v); shift ;;
  --) shift; break ;;
  -*) _err "unknown option: $1" ;;
  *) break ;;
esac; done

BUILD_DIR=$OUTDIR/$BUILD_MODE
[ "$BUILD_MODE" = "debug" ] && DEBUG=true

case "$WITH_LLVM" in
  "") WITH_LLVM=static ; $DEBUG && WITH_LLVM=shared ;;
  static|shared) ;;  # as is
  off) WITH_LLVM= ;;
  *)   _err "invalid value \"$WITH_LLVM\" for -llvm option" ;;
esac

#————————————————————————————————————————————————————————————————————————————————————————
# setup environment (select compiler, clear $OUTDIR if compiler changed)

CC_IS_CLANG=false
CC_IS_GCC=false

if [ -z "$CC" ]; then
  # use clang from known preferred location, if available
  if [ -x "$PWD"/deps/llvm/bin/clang ]; then
    export PATH="$PWD"/deps/llvm/bin:$PATH
    CC=clang
    CXX=clang++
    CC_IS_CLANG=true
  elif [ -x /usr/local/opt/llvm/bin/clang ]; then
    export PATH=/usr/local/opt/llvm/bin:$PATH
    CC=clang
    CXX=clang++
    CC_IS_CLANG=true
  elif [ -x /opt/homebrew/opt/llvm/bin/clang ]; then
    export PATH=/opt/homebrew/opt/llvm/bin:$PATH
    CC=clang
    CXX=clang++
    CC_IS_CLANG=true
  elif command -v clang >/dev/null; then
    CC=clang
    CXX=clang++
    CC_IS_CLANG=true
  elif command -v gcc >/dev/null; then
    export PATH=$(dirname "$(command -v gcc)"):$PATH
    CC=gcc
    CXX=g++
    CC_IS_GCC=true
  else
    CC=cc
    CXX=c++
  fi
  export CC
  export CXX
  export CC_IS_CLANG=$CC_IS_CLANG
  export CC_IS_GCC=$CC_IS_GCC
fi

CC_PATH=$(command -v "$CC" || true)
[ -f "$CC_PATH" ] || _err "CC (\"$CC\") not found"

if ! $CC_IS_CLANG && ! $CC_IS_GCC; then
  CC_VERSION=$($CC --version 2>/dev/null | head -n1)
  case "$CC_VERSION" in
    "clang "*|*" clang "*) CC_IS_CLANG=true ;;
    "gcc "*|*" gcc "*)     CC_IS_GCC=true ;;
  esac
fi

$VERBOSE && { echo "CC=$CC"; echo "CXX=$CXX"; }

# check compiler and clear $OUTDIR if compiler changed
CCONFIG_FILE=$OUTDIR/cconfig.txt
CCONFIG="$CC_PATH: $(_checksum "$CC_PATH")"
if [ "$(cat "$CCONFIG_FILE" 2>/dev/null)" != "$CCONFIG" ]; then
  [ -f "$CCONFIG_FILE" ] && echo "compiler config changed"
  rm -rf "$OUTDIR"
  mkdir -p "$OUTDIR"
  echo "$CCONFIG" > "$CCONFIG_FILE"
fi

#————————————————————————————————————————————————————————————————————————————————————————
# watch (if enabled, execution of script forks here)

if [ -n "$WATCH" ]; then
  RUN_PIDFILE=${TMPDIR:-/tmp}/$(basename "$(dirname "$PWD")").build-runpid.$$
  echo "RUN_PIDFILE=$RUN_PIDFILE"

  _WATCH_FILES=()
  _WATCH_DIRS=()
  for fn in "${WATCHFILES[@]}"; do
    [ -d "$fn" ] && _WATCH_DIRS+=( "$fn" ) || _WATCH_FILES+=( "$fn" )
  done

  _killcmd() {
    local RUN_PID=$(cat "$RUN_PIDFILE" 2>/dev/null)
    if [ -n "$RUN_PID" ]; then
      kill $RUN_PID 2>/dev/null && echo "killing #$RUN_PID"
      ( sleep 0.1 ; kill -9 "$RUN_PID" 2>/dev/null || true ) &
      rm -f "$RUN_PIDFILE"
    fi
  }
  _exit() {
    _killcmd
    kill $(jobs -p) 2>/dev/null || true
    rm -f "$RUN_PIDFILE"
    exit
  }
  _find_watch_files() {
    [ ${#_WATCH_FILES[@]} -gt 0 ] &&
      echo "${_WATCH_FILES[@]}"
    [ ${#_WATCH_DIRS[@]} -gt 0 ] &&
      find "${_WATCH_DIRS[@]}" -type f -name "*.*" -not -path "*/_*" -and -not -name ".*"
  }
  _stat_mtime() { stat -c '%Y' "$1"; }
  if [ "$(uname -s)" = "Darwin" ]; then
    _stat_mtime() { stat -f '%m' "$1"; }
  fi
  _watch_source_files() {
    [ ${#WATCHFILES[@]} -eq 0 ] && _err "WATCHFILES is empty"
    if command -v fswatch >/dev/null; then
      fswatch --one-event --extended --latency=0.1 \
              --exclude='.*' --include='\.[^\.]+$' \
              --recursive \
              "${WATCHFILES[@]}"
      return
    fi
    # no watch tool available -- fall back to polling with "find"
    local mtime_max=$(_stat_mtime "$SRCDIR")
    local mtime_max_fn=
    local mtime f
    for f in $(_find_watch_files); do
      mtime=$(_stat_mtime "$f")
      if [ "$mtime" -gt "$mtime_max" ]; then
        mtime_max=$mtime
        mtime_max_fn=$f
      fi
    done
    # create reference file for find
    mkdir -p "$BUILD_DIR"
    if [ -n "$mtime_max_fn" ]; then
      cp -a "$mtime_max_fn" "$BUILD_DIR/watch-ref-mtime"
    else  # avoid silent error in find, in case mtime_max_fn is empty
      touch "$BUILD_DIR/watch-ref-mtime"
    fi
    while true; do
      sleep 0.5
      if [ ${#_WATCH_DIRS[@]} -gt 0 ]; then
        [ -n "$(find "${_WATCH_DIRS[@]}" -type f -not -path '*/.*' \
                     -newer "$BUILD_DIR/watch-ref-mtime" -print -quit)" ] &&
          return
      fi
      if [ ${#_WATCH_FILES[@]} -gt 0 ]; then
        for f in "${_WATCH_FILES[@]}"; do
          [ "$f" -nt "$BUILD_DIR/watch-ref-mtime" ] &&
            return
        done
      fi
    done
  }
  trap _exit SIGINT  # make sure we can ctrl-c in the while loop
  while true; do
    printf "\x1bc"  # clear screen ("scroll to top" style)
    BUILD_OK=1
    ${SHELL:-bash} "./$(basename "$0")" -_w_ "-$BUILD_MODE" "${NINJA_ARGS[@]}" "$@" ||
      BUILD_OK=
    printf "\e[2m> watching files for changes...\e[m\n"
    if [ -n "$BUILD_OK" -a -n "$RUN" ]; then
      export ASAN_OPTIONS=detect_stack_use_after_return=1
      export UBSAN_OPTIONS=print_stacktrace=1
      _killcmd
      ( $SHELL -c "$RUN" &
        RUN_PID=$!
        echo $RUN_PID > "$RUN_PIDFILE"
        echo "$RUN (#$RUN_PID) started"
        wait
        # TODO: get exit code from $RUN
        # Some claim wait sets the exit code, but not in my bash.
        # The idea would be to capture exit code from wait:
        #   status=$?
        echo "$RUN (#$RUN_PID) exited"
      ) &
    fi
    _watch_source_files
  done
  exit 0
fi

#————————————————————————————————————————————————————————————————————————————————————————
# construct flags
#
#   XFLAGS             compiler flags (common to C and C++)
#     XFLAGS_HOST      compiler flags specific to native host target
#     XFLAGS_WASM      compiler flags specific to WASM target
#     CFLAGS           compiler flags for C
#       CFLAGS_HOST    compiler flags for C specific to native host target
#       CFLAGS_WASM    compiler flags for C specific to WASM target
#     CXXFLAGS         compiler flags for C++
#       CXXFLAGS_HOST  compiler flags for C++ specific to native host target
#       CXXFLAGS_WASM  compiler flags for C++ specific to WASM target
#   LDFLAGS            linker flags common to all targets
#     LDFLAGS_HOST     linker flags specific to native host target (cc suite's ld)
#     LDFLAGS_WASM     linker flags specific to WASM target (llvm's wasm-ld)
#————————————————————————————
XFLAGS=(
  -g \
  -feliminate-unused-debug-types \
  -fvisibility=hidden \
  -Wall -Wextra -Wvla \
  -Wimplicit-fallthrough \
  -Wno-missing-field-initializers \
  -Wno-unused-parameter \
  -Werror=implicit-function-declaration \
  -Werror=incompatible-pointer-types \
  -Werror=int-conversion \
  "${XFLAGS[@]}" \
)
XFLAGS_HOST=()
XFLAGS_WASM=(
  -D${PP_PREFIX}NO_LIBC \
  --target=wasm32 \
  --no-standard-libraries \
  -fvisibility=hidden \
)
#————————————————————————————
CFLAGS=(
  -std=c11 \
  "${CFLAGS[@]}" \
)
CFLAGS_HOST=()
CFLAGS_WASM=()
#————————————————————————————
CXXFLAGS=(
  -std=c++14 \
  -fvisibility-inlines-hidden \
  -fno-exceptions \
  -fno-rtti \
  "${CXXFLAGS[@]}" \
)
CXXFLAGS_HOST=()
CXXFLAGS_WASM=()
#————————————————————————————
LDFLAGS_HOST=(
  $LDFLAGS \
)
LDFLAGS_WASM=(
  --no-entry \
  --no-gc-sections \
  --export-dynamic \
  --import-memory \
  $LDFLAGS_WASM \
)
#————————————————————————————
# compiler-specific flags
if $CC_IS_CLANG; then
  XFLAGS+=(
    -Wcovered-switch-default \
    -Werror=format-insufficient-args \
    -Werror=bitfield-constant-conversion \
    -Wno-pragma-once-outside-header \
  )
  [ -t 1 ] && XFLAGS+=( -fcolor-diagnostics )
  if [ "$(uname -s)" = "Darwin" ]; then
    # Use lld to avoid incompatible outdated macOS system linker.
    # If the system linker is outdated, it would fail with an error like this:
    # "ld: could not parse object file ... Unknown attribute kind ..."
    LDFLAGS_HOST+=(
      -fuse-ld="$(dirname "$CC_PATH")/ld64.lld" \
      -Wl,-platform_version,macos,10.15,10.15 \
    )
  fi
elif $CC_IS_GCC; then
  [ -t 1 ] && XFLAGS+=( -fdiagnostics-color=always )
fi

# build mode- and compiler-specific flags
if $DEBUG; then
  XFLAGS+=( -O0 -DDEBUG )
  if $CC_IS_CLANG; then
    XFLAGS+=( -ferror-limit=6 )
    # enable llvm address and UD sanitizer in debug builds
    # See https://clang.llvm.org/docs/AddressSanitizer.html
    # See https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
    XFLAGS_HOST+=(
      -fsanitize=address,undefined \
      -fsanitize-address-use-after-scope \
      -fsanitize=float-divide-by-zero \
      -fsanitize=null \
      -fsanitize=nonnull-attribute \
      -fsanitize=nullability \
      -fno-omit-frame-pointer \
      -fno-optimize-sibling-calls \
      -fmacro-backtrace-limit=0 \
    )
    LDFLAGS_HOST+=( -fsanitize=address,undefined -static-libsan )
  fi
else
  XFLAGS+=( -DNDEBUG )
  XFLAGS_HOST+=( -O3 -mtune=native -fomit-frame-pointer )
  XFLAGS_WASM+=( -Oz )
  LDFLAGS_WASM+=( --lto-O3 --no-lto-legacy-pass-manager )
  # LDFLAGS_WASM+=( -z stack-size=$[128 * 1024] ) # larger stack, smaller heap
  # LDFLAGS_WASM+=( --compress-relocations --strip-debug )
  # LDFLAGS_HOST+=( -dead_strip )
  if [ "$BUILD_MODE" = "safe" ]; then
    XFLAGS+=( -D${PP_PREFIX}SAFE )
  fi
  if $CC_IS_CLANG; then
    XFLAGS+=( -flto )
    LDFLAGS_HOST+=( -flto -Wl,-no_pie )
  fi
fi

# testing enabled?
$TESTING_ENABLED &&
  XFLAGS+=( -D${PP_PREFIX}TESTING_ENABLED )

# has WASM_SYMS file?
[ -f "$WASM_SYMS" ] &&
  LDFLAGS_WASM+=( -allow-undefined-file "$WASM_SYMS" )

# llvm?
LLVM_CFLAGS=()
LLVM_CXXFLAGS=()
if [ -n "$WITH_LLVM" ]; then
  LLVM_CFLAGS=(
    -I$DEPSDIR/llvm/include \
    $($DEPSDIR/llvm/bin/llvm-config --cflags) \
  )
  LLVM_CXXFLAGS=(
    -I$DEPSDIR/llvm/include \
    -nostdinc++ -I$DEPSDIR/llvm/include/c++/v1 -stdlib=libc++ \
    $($DEPSDIR/llvm/bin/llvm-config --cxxflags) \
  )
  LDFLAGS_HOST+=(
    -lm \
  )
  if [ "$WITH_LLVM" = shared ]; then
    LDFLAGS_HOST+=(
      "-L$DEPSDIR/llvm/lib" -lc++ \
      "-Wl,-rpath,$DEPSDIR/llvm/lib" \
      "$DEPSDIR/llvm/lib/libco-llvm-bundle-d.dylib" \
    )
  else
    LDFLAGS_HOST+=(
      "$DEPSDIR/llvm/lib/libco-llvm-bundle.a" \
      "$DEPSDIR/llvm/lib/libc++.a" \
    )
  fi
fi

#————————————————————————————————————————————————————————————————————————————————————————
# find source files
#
# name.{c,cc}       always included
# name.ARCH.{c,cc}  specific to ARCH (e.g. wasm, x86_64, arm64, etc)
# name.test.{c,cc}  only included when testing is enabled

COMMON_SOURCES=()
HOST_SOURCES=()
WASM_SOURCES=()
TEST_SOURCES=()

HOST_ARCH=$(uname -m)
for f in $(find "$SRCDIR" -name '*.c' -or -name '*.cc'); do
  case "$f" in
    */test.c|*.test.c|*.test.cc)    TEST_SOURCES+=( "$f" ) ;;
    *.$HOST_ARCH.c|*.$HOST_ARCH.cc) HOST_SOURCES+=( "$f" ) ;;
    *.wasm.c|*.wasm.cc)             WASM_SOURCES+=( "$f" ) ;;
    *)                              COMMON_SOURCES+=( "$f" ) ;;
  esac
done

$VERBOSE && {
  echo "TEST_SOURCES=${TEST_SOURCES[@]}"
  echo "HOST_SOURCES=${HOST_SOURCES[@]}"
  echo "WASM_SOURCES=${WASM_SOURCES[@]}"
  echo "COMMON_SOURCES=${COMMON_SOURCES[@]}"
}

#————————————————————————————————————————————————————————————————————————————————————————
# generate .clang_complete

if $CC_IS_CLANG; then
  echo "-I$(realpath "$SRCDIR")" > .clang_complete
  for flag in "${XFLAGS[@]}" "${XFLAGS_HOST[@]}" "${CFLAGS_HOST[@]}"; do
    echo "$flag" >> .clang_complete
  done
fi

#————————————————————————————————————————————————————————————————————————————————————————
# generate build.ninja

NF=$BUILD_DIR/new-build.ninja     # temporary file
NINJAFILE=$BUILD_DIR/build.ninja  # actual build file
NINJA_ARGS+=( -f "$NINJAFILE" )
LINKER=$CC

mkdir -p "$BUILD_DIR/obj"

cat << _END > $NF
ninja_required_version = 1.3
builddir = $BUILD_DIR
objdir = \$builddir/obj

xflags = ${XFLAGS[@]}
xflags_host = \$xflags ${XFLAGS_HOST[@]}
xflags_wasm = \$xflags ${XFLAGS_WASM[@]}

cflags = ${CFLAGS[@]}
cflags_host = \$xflags_host ${CFLAGS_HOST[@]}
cflags_wasm = \$xflags_wasm ${CFLAGS_WASM[@]}

cxxflags = ${CXXFLAGS[@]}
cxxflags_host = \$xflags_host ${CXXFLAGS_HOST[@]}
cxxflags_wasm = \$xflags_wasm ${CXXFLAGS_WASM[@]}

ldflags_host = ${LDFLAGS_HOST[@]}
ldflags_wasm = ${LDFLAGS_WASM[@]}


rule link
  command = $LINKER \$ldflags_host \$FLAGS -o \$out \$in
  description = link \$out

rule link_wasm
  command = wasm-ld \$ldflags_wasm \$FLAGS \$in -o \$out
  description = link \$out


rule cc
  command = $CC -MMD -MF \$out.d \$cflags \$cflags_host \$FLAGS -c \$in -o \$out
  depfile = \$out.d
  description = compile \$in

rule cxx
  command = $CXX -MMD -MF \$out.d \$cxxflags \$cxxflags_host \$FLAGS -c \$in -o \$out
  depfile = \$out.d
  description = compile \$in


rule cc_wasm
  command = $CC -MMD -MF \$out.d \$cflags \$cflags_wasm \$FLAGS -c \$in -o \$out
  depfile = \$out.d
  description = compile \$in

rule cxx_wasm
  command = $CXX -MMD -MF \$out.d \$cxxflags \$cxxflags_wasm \$FLAGS -c \$in -o \$out
  depfile = \$out.d
  description = compile \$in


rule ast_gen
  command = python3 src/parse/ast_gen.py \$in \$out
  generator = true

rule cxx_pch_gen
  command = $CXX \$cxxflags \$cxxflags_host \$FLAGS -x c++-header \$in -o \$out
  #clang -cc1 -emit-pch -x c++-header \$in -o \$out
  generator = true

rule cxx_pch_obj
  command = $CXX -c \$in -o \$out

build src/parse/ast_gen.h src/parse/ast_gen.c: ast_gen src/parse/ast.h | src/parse/ast_gen.py

_END

if [ -n "$WITH_LLVM" ]; then
  echo "build \$objdir/llvm-includes.pch: cxx_pch_gen src/llvm/llvm-includes.hh" >> $NF
fi


_objfile() { echo \$objdir/${1//\//.}.o; }
_gen_obj_build_rules() {
  local TARGET=$1 ; shift
  local OBJECT
  local CC_RULE=cc
  local CXX_RULE=cxx
  if [ "$TARGET" = wasm ]; then
    CC_RULE=cc_wasm
    CXX_RULE=cxx_wasm
  fi
  for SOURCE in "$@"; do
    OBJECT=$(_objfile "$TARGET-$SOURCE")
    case "$SOURCE" in
      */llvm/*.c)
        [ -z "$WITH_LLVM" ] && continue
        echo "build $OBJECT: $CC_RULE $SOURCE" >> $NF
        echo "  FLAGS = ${LLVM_CFLAGS[@]}" >> $NF
        ;;
      */llvm/*.cc)
        [ -z "$WITH_LLVM" ] && continue
        echo "build $OBJECT: $CXX_RULE $SOURCE | \$objdir/llvm-includes.pch" >> $NF
        echo "  FLAGS = -include-pch \$objdir/llvm-includes.pch ${LLVM_CXXFLAGS[@]}" >> $NF
        ;;
      *.c)
        echo "build $OBJECT: $CC_RULE $SOURCE" >> $NF
        ;;
      *.cc)
        echo "build $OBJECT: $CXX_RULE $SOURCE" >> $NF
        ;;
      *) _err "don't know how to compile this file type ($SOURCE)"
    esac
    echo "$OBJECT"
  done
}

HOST_SOURCES+=( "${COMMON_SOURCES[@]}" )
WASM_SOURCES+=( "${COMMON_SOURCES[@]}" )

if $TESTING_ENABLED; then
  HOST_SOURCES+=( "${TEST_SOURCES[@]}" )
  WASM_SOURCES+=( "${TEST_SOURCES[@]}" )
fi

HOST_OBJECTS=( $(_gen_obj_build_rules "host" "${HOST_SOURCES[@]}") )
echo >> $NF
echo "build \$builddir/$MAIN_EXE: link ${HOST_OBJECTS[@]}" >> $NF
echo >> $NF

WASM_OBJECTS=( $(_gen_obj_build_rules "wasm" "${WASM_SOURCES[@]}") )
echo >> $NF
echo "build \$builddir/$MAIN_EXE.wasm: link_wasm ${WASM_OBJECTS[@]}" >> $NF
echo >> $NF

echo "build $MAIN_EXE: phony \$builddir/$MAIN_EXE" >> $NF
echo "build $MAIN_EXE.wasm: phony \$builddir/$MAIN_EXE.wasm" >> $NF
echo "default $MAIN_EXE" >> $NF

# write build.ninja only if it changed
if [ "$(_checksum $NF)" != "$(_checksum "$NINJAFILE" 2>/dev/null)" ]; then
  mv $NF "$NINJAFILE"
  $VERBOSE && echo "wrote $NINJAFILE"
else
  rm $NF
  $VERBOSE && echo "$NINJAFILE did not change"
fi

# stop now if -config is set
$ONLY_CONFIGURE && exit

# ninja
if [ -n "$RUN" ]; then
  $NINJA "${NINJA_ARGS[@]}" "$@"
  echo $RUN
  exec $SHELL -c "$RUN"
fi
exec $NINJA "${NINJA_ARGS[@]}" "$@"
