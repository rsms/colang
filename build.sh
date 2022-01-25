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
            --exclude='.*' --include='\.(c|cc|cpp|m|mm|h|hh|sh)$' \
            --recursive \
            cmd $(basename "$0")
    if [ -n "$RUN_PID" ]; then
      kill $(jobs -p) 2>/dev/null && wait $(jobs -p) 2>/dev/null || true
      RUN_PID=
    fi
  done
  exit 0
fi

# set SKIA_DEBUG=1 in env to use Skia debug build
#
LUAJIT=deps/luajit
SKIA=deps/skia
SKIA_OUT=$SKIA/out/release
[ -n "$SKIA_DEBUG" ] && SKIA_OUT=$SKIA/out/debug
SKIA_DEFS=$(grep 'defines =' $SKIA_OUT/obj/skia.ninja | sed 's/defines = //')
SKIA_APP_SOURCES=(
  $SKIA/tools/sk_app/CommandSet.cpp \
  $SKIA/tools/sk_app/Window.cpp \
  $SKIA/tools/sk_app/WindowContext.cpp \
)
CFLAGS=( $CFLAGS )
LDFLAGS=( $LDFLAGS )
if [ "$BUILD_MODE" = "opt" ]; then
  CFLAGS+=( -O3 )
  LDFLAGS+=( -dead_strip -flto )
else
  CFLAGS+=( -DDEBUG )
fi

case "$(uname -s)" in
  Darwin)
    SYSROOT=$(echo /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOS*.sdk | cut -d' ' -f1)
    [ -d $SYSROOT ] || SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
    [ -d $SYSROOT ] || _err "$SYSROOT not found"
    SKIA_APP_SOURCES+=(
      $SKIA/tools/sk_app/MetalWindowContext.mm \
      $SKIA/tools/sk_app/mac/main_mac.mm \
      $SKIA/tools/sk_app/mac/MetalWindowContext_mac.mm \
      $SKIA/tools/sk_app/mac/Window_mac.mm \
    )
    LDFLAGS+=(
      -isysroot $SYSROOT \
      -Wl,-w \
      -Wl,-platform_version,macos,10.15,11.00 \
      -Wl,-rpath,@loader_path/. \
      -framework ApplicationServices \
      -framework AppKit \
      -framework Quartz \
      -framework Metal \
    )
  ;;
esac


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
  ${CFLAGS[@]} $
  -I$LUAJIT/src $
  $SKIA_DEFS

cflags_c = $
  -std=c11

cflags_cxx = $
  -std=c++14 $
  -Wno-c++17-extensions $
  -fvisibility-inlines-hidden $
  -fno-exceptions $
  -fno-rtti $
  -I$SKIA

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

_END


SKIA_APP_OBJECTS=()
for SOURCE in ${SKIA_APP_SOURCES[@]}; do
  OBJECT=\$objdir/${SOURCE//\//.}.o
  SKIA_APP_OBJECTS+=( "$OBJECT" )
  case "$SOURCE" in
    *.c|*.m)         echo "build $OBJECT: cc $SOURCE" >> build.ninja ;;
    *.cc|*.cpp|*.mm) echo "build $OBJECT: cxx $SOURCE" >> build.ninja ;;
  esac
done

ALL=()
for d in cmd/*; do
  case "$d" in .*) continue ;; esac # skip dot files
  [ -d "$d" ] || continue # skip non-dirs
  if ! [ -f "$d/main.c" -o -f "$d/main.cc" ]; then
    echo "skipping dir $d (does not contain a main.c or main.cc file)"
    continue
  fi

  if [ -f $d/build.ninja ]; then
    echo "subninja build.ninja" >> build.ninja
    continue
  fi

  OBJECTS=()

  # load custom CFLAGS
  EXTRA_CFLAGS=()
  CFLAGS_FILE=$d/cflags.txt
  [ -f "$CFLAGS_FILE" ] && EXTRA_CFLAGS+=( $(cat "$CFLAGS_FILE") )

  # load custom LDFLAGS
  EXTRA_LDFLAGS=()
  LDFLAGS_FILE=$d/ldflags.txt
  if [ -f "$LDFLAGS_FILE" ]; then
    for s in $(cat "$LDFLAGS_FILE"); do
      if [ "$s" = "\$LIB_SKIA_APP" ]; then
        OBJECTS+=( "${SKIA_APP_OBJECTS[@]}" )
      else
        s=${s//\$SKIA/$SKIA_OUT}
        s=${s//\$LUAJIT/$LUAJIT}
        EXTRA_LDFLAGS+=( "$s" )
      fi
    done
  fi

  # find source files
  SOURCES=( $(find "$d" -name '*.c' -or -name '*.cc' -or -name '*.mm') )
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

  EXE=$(basename "$d")
  ALL+=( $EXE )

  echo "build \$outdir/$EXE: link ${OBJECTS[@]}" >> build.ninja
  if [ -n "$EXTRA_LDFLAGS" ]; then
    echo "  ldflags = \$ldflags ${EXTRA_LDFLAGS[@]}" >> build.ninja
  fi
done

echo >> build.ninja
for target in "${ALL[@]}"; do
  echo "build $target: phony \$outdir/$target" >> build.ninja
done

echo "default ${ALL[@]}" >> build.ninja

if [ -n "$RUN" ]; then
  ninja "$@"
  echo $RUN
  exec $RUN
fi
exec ninja "$@"
