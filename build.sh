#!/bin/bash
set -e
cd "$(dirname "$0")"
_err() { echo -e "$0:" "$@" >&2 ; exit 1; }

OUTDIR=out
BUILD_MODE=safe  # debug | safe | fast
WATCH=
RUN=

while [[ $# -gt 0 ]]; do case "$1" in
  -w)      WATCH=1; shift ;;
  -run=*)  RUN=${1:5}; shift ;;
  -debug)  BUILD_MODE=debug; shift ;;
  -safe)   BUILD_MODE=safe; shift ;;
  -fast)   BUILD_MODE=fast; shift ;;
  -h|-help|--help) cat << _END
usage: $0 [options] [<target> ...]
options:
  -safe      Build optimized product with assertions enabled (default)
  -fast      Build optimized product without assertions
  -debug     Build debug product
  -w         Rebuild as sources change
  -run=<cmd> Run <cmd> after successful build
  -help      Show help on stdout and exit
_END
    exit ;;
  --) break ;;
  -*) _err "unknown option: $1" ;;
  *) break ;;
esac; done

# -w to enter "watch & build & run" mode
if [ -n "$WATCH" ]; then
  command -v fswatch >/dev/null || _err "fswatch not found in PATH"
  RUN_PID=
  _exit() {
    kill $RUN_PID 2>/dev/null || true
    exit
  }
  trap _exit SIGINT  # make sure we can ctrl-c in the while loop
  while true; do
    echo -e "\x1bc"  # clear screen ("scroll to top" style)
    BUILD_OK=1
    BUILD_ARGS=( "-$BUILD_MODE" "$@" )
    "./$(basename "$0")" "${BUILD_ARGS[@]}" || BUILD_OK=
    printf "\e[2m> watching files for changes...\e[m\n"
    if [ -n "$BUILD_OK" -a -n "$RUN" ]; then
      export ASAN_OPTIONS=detect_stack_use_after_return=1
      echo $RUN
      ( trap 'kill $(jobs -p)' SIGINT ; $RUN ; echo "$RUN exited $?" ) &
      RUN_PID=$!
    fi
    fswatch --one-event --extended --latency=0.1 \
            --exclude='.*' --include='\.(c|cc|cpp|m|mm|h|hh|sh|py)$' \
            --recursive \
            src $(basename "$0")
    if [ -n "$RUN_PID" ]; then
      kill $(jobs -p) 2>/dev/null && wait $(jobs -p) 2>/dev/null || true
      RUN_PID=
    fi
  done
  exit 0
fi


# use llvm at deps/llvm if available
if [ -x deps/llvm/bin/clang ]; then
  export PATH=$PWD/deps/llvm/bin:$PATH
  export CC=clang
  export CXX=clang++
  export AR=llvm-ar
  CC_IS_CLANG=1
else
  export CC=${CC:-cc}
fi
CC_PATH=$(command -v $CC)
CC_PATH=${CC_PATH##$PWD/}
[ -f "$CC_PATH" ] || _err "CC (\"$CC\") not found"
if [ -z "$CC_IS_CLANG" ] && $CC --version 2>/dev/null | head -n1 | grep -q clang; then
  CC_IS_CLANG=1
fi

# check compiler and clear $OUTDIR if cflags or compiler changed
CCONFIG_FILE=$OUTDIR/cconfig.txt
CCONFIG="$CC_PATH: $(sha256sum "$CC_PATH" | cut -d' ' -f1)"
if [ "$(cat "$CCONFIG_FILE" 2>/dev/null)" != "$CCONFIG" ]; then
  [ -f "$CCONFIG_FILE" ] && echo "compiler config changed"
  rm -rf "$OUTDIR"
  mkdir -p "$OUTDIR"
  echo "$CCONFIG" > "$CCONFIG_FILE"
fi


CFLAGS=( $CFLAGS )
LDFLAGS=( $LDFLAGS )

CFLAGS+=( -DCO_WITH_LIBC )  # TODO: wasm

WITH_LUAJIT=deps/luajit
if [ -n "$WITH_LUAJIT" ]; then
  CFLAGS+=( -I$WITH_LUAJIT/src -DCO_WITH_LUAJIT )
  LDFLAGS+=( $WITH_LUAJIT/src/libluajit.a )
fi

if [ "$BUILD_MODE" != "debug" ]; then
  CFLAGS+=( -O3 -march=native )
  [ "$BUILD_MODE" = "fast" ] && CFLAGS+=( -DNDEBUG )
  LDFLAGS+=( -dead_strip -flto )
else
  CFLAGS+=( -DDEBUG -ferror-limit=10 )
fi

# enable llvm address and UD sanitizer in debug builds
if [ -n "$CC_IS_CLANG" -a "$BUILD_MODE" = "debug" ]; then
  CFLAGS+=(
    -fsanitize=address,undefined \
    -fsanitize-address-use-after-scope \
    -fno-omit-frame-pointer \
    -fno-optimize-sibling-calls \
  )
  LDFLAGS+=(
    -fsanitize=address,undefined \
  )
fi

# Note: -fms-extensions enables composable structs in clang & GCC
# See https://gcc.gnu.org/onlinedocs/gcc-11.2.0/gcc/Unnamed-Fields.html
# TODO: test with gcc; may require -fplan9-extensions

cat << _END > build.ninja
ninja_required_version = 1.3
outdir = $OUTDIR
objdir = \$outdir/$BUILD_MODE

cflags = $
  -g $
  -fcolor-diagnostics $
  -Wall -Wextra -Wvla $
  -Wno-missing-field-initializers $
  -Wno-unused-parameter $
  -Werror=implicit-function-declaration $
  -Wcovered-switch-default ${CFLAGS[@]}

cflags_c = $
  -std=c11 -fms-extensions -Wno-microsoft

cflags_cxx = $
  -std=c++14 $
  -fvisibility-inlines-hidden $
  -fno-exceptions $
  -fno-rtti

ldflags = ${LDFLAGS[@]}

rule link
  command = c++ \$ldflags -o \$out \$in
  description = link \$out

rule cc
  command = cc -MD -MF \$out.d \$cflags \$cflags_c -c \$in -o \$out
  depfile = \$out.d
  description = compile \$in

rule cxx
  command = c++ -MD -MF \$out.d \$cflags \$cflags_cxx -c \$in -o \$out
  depfile = \$out.d
  description = compile \$in

rule ast_gen
  command = python3 src/parse/ast_gen.py \$in \$out

build \$objdir/ast_gen.mark: ast_gen src/parse/ast.h src/parse/ast.c | src/parse/ast_gen.py
_END

SOURCES=( $(find src -name '*.c' -or -name '*.cc' -or -name '*.mm') )
OBJECTS=()
for SOURCE in "${SOURCES[@]}"; do
  OBJECT=\$objdir/${SOURCE//\//.}.o
  OBJECTS+=( "$OBJECT" )
  case "$SOURCE" in
    *.c|*.m)
      echo "build $OBJECT: cc $SOURCE" >> build.ninja
      if [ -n "$EXTRA_CFLAGS" ]; then
        echo "  cflags = \$cflags ${EXTRA_CFLAGS[@]}" >> build.ninja
      fi
      ;;
    *.cc|*.cpp|*.mm)
      echo "build $OBJECT: cxx $SOURCE" >> build.ninja
      ;;
  esac
done
echo >> build.ninja

echo "build co: phony \$outdir/co" >> build.ninja
echo "build \$outdir/co: link ${OBJECTS[@]} | \$objdir/ast_gen.mark" >> build.ninja
echo >> build.ninja

echo "default co" >> build.ninja

if [ -n "$RUN" ]; then
  ninja "$@"
  echo $RUN
  exec $RUN
fi
exec ninja "$@"
