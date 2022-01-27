#!/bin/bash
set -e
cd "$(dirname "$0")"
_err() { echo -e "$0:" "$@" >&2 ; exit 1; }

BUILD_MODE=debug
WATCH=
RUN=

while [[ $# -gt 0 ]]; do case "$1" in
  -w)     WATCH=1 ; shift ;;
  -run=*) RUN=${1:5} ; shift ;;
  -opt)   BUILD_MODE=opt ; shift ;;
  -h|-help|--help) cat << _END
usage: $0 [options] [ninja arg... | <target>]
options:
  -w         Rebuild as sources change
  -opt       Build optimized product instead of debug product
  -run=<cmd> Run <cmd> after successful build
  -help      Show help on stdout and exit
_END
    exit ;;
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
    BUILD_ARGS=( "$@" )
    [ "$BUILD_MODE" = "opt" ] && BUILD_ARGS=( -opt "${BUILD_ARGS[@]}" )
    "./$(basename "$0")" "${BUILD_ARGS[@]}" || BUILD_OK=
    printf "\e[2m> watching files for changes...\e[m\n"
    if [ -n "$BUILD_OK" -a -n "$RUN" ]; then
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

CFLAGS=( $CFLAGS )
LDFLAGS=( $LDFLAGS )

CFLAGS+=( -DCO_WITH_LIBC )  # TODO: wasm

WITH_LUAJIT=true
if $WITH_LUAJIT; then
  CFLAGS+=( -Ideps/luajit/src -DCO_WITH_LUAJIT )
  LDFLAGS+=( deps/luajit/src/libluajit.a )
fi

if [ "$BUILD_MODE" = "opt" ]; then
  CFLAGS+=( -O3 )
  LDFLAGS+=( -dead_strip -flto )
else
  CFLAGS+=( -DDEBUG -ferror-limit=10 )
fi

# case "$(uname -s)" in
#   Darwin)
#     SYSROOT=$(echo /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOS*.sdk | cut -d' ' -f1)
#     [ -d $SYSROOT ] || SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
#     [ -d $SYSROOT ] || _err "$SYSROOT not found"
#     LDFLAGS+=(
#       -isysroot $SYSROOT \
#       -Wl,-w \
#       -Wl,-platform_version,macos,10.15,11.00 \
#       -Wl,-rpath,@loader_path/. \
#       -framework ApplicationServices \
#       -framework AppKit \
#       -framework Quartz \
#       -framework Metal \
#     )
#   ;;
# esac

# Note: -fms-extensions enables composable structs in clang & GCC
# See https://gcc.gnu.org/onlinedocs/gcc-11.2.0/gcc/Unnamed-Fields.html
# TODO: test with gcc; may require -fplan9-extensions

cat << _END > build.ninja
ninja_required_version = 1.3
outdir = out
objdir = \$outdir/$BUILD_MODE

cflags = $
  -g $
  -fcolor-diagnostics $
  -Wall -Wextra -Wvla $
  -Wno-missing-field-initializers $
  -Wno-unused-parameter $
  -Werror=implicit-function-declaration $
  -Wcovered-switch-default $
  ${CFLAGS[@]}

cflags_c = $
  -std=c11 -fms-extensions -Wno-microsoft

cflags_cxx = $
  -std=c++14 $
  -Wno-c++17-extensions $
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
