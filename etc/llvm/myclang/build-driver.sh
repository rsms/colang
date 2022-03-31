#!/bin/bash
set -e
cd "$(dirname "$0")"
. ../../misc/_common.sh

OUTFILE=myclang
LLVM_PREFIX="$DEPS_DIR"/llvm
CFLAGS="-Oz -g -I$LLVM_PREFIX/include"

# Note: there's currently no stdc++ lib at LLVM_PREFIX, so use the system compiler for now.
# TODO: use clang at LLVM_PREFIX with stdc++
export PATH="$LLVM_PREFIX/bin:$PATH"
command -v clang


mkdir -p .obj
ATEXIT+=("rm -rf '$PWD/.obj'")
for f in *.cc; do
  echo "$f -> .obj/$f.o"
  clang++ -static -stdlib=libc++ -nostdinc++ -std=c++14 \
    "-I$PROJECT/lib/libcxx/include" $CFLAGS -c -o .obj/$f.o $f &
done
for f in *.c; do
  echo "$f -> .obj/$f.o"
  clang $CFLAGS -std=c17 -c -o .obj/$f.o $f &
done
wait


echo "link $OUTFILE"

clang -flto \
  -L"$LLVM_PREFIX"/lib \
  -L"$DEPS_DIR"/zlib/lib \
  -Wl,-no_pie \
  -o "$OUTFILE" \
  \
  .obj/*.o \
  "$PROJECT"/work/build/libc++.a \
  "$PROJECT"/work/build/libc++abi.a \
  "$LLVM_PREFIX"/lib/libclang*.a \
  $(llvm-config --libfiles) \
  \
  $(llvm-config --system-libs) \

# print dylibs
echo otool -L "$OUTFILE"
     otool -L "$OUTFILE"

# strip
cp "$OUTFILE" "$OUTFILE"-stripped
llvm-strip "$OUTFILE"-stripped

# test it
echo "building test program"
./"$OUTFILE"-stripped -o test/test test/main.c
./test/test
echo otool -L test/test
     otool -L test/test
