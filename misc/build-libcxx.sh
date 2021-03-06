#!/bin/bash
set -e
cd "$(dirname "$0")/.."
. misc/_common.sh
SRCDIR=$PWD
DESTDIR=$PWD/deps/libcxx
BUILDDIR=$PWD/out/libcxx

echo "SRCDIR   $SRCDIR"
echo "DESTDIR  $DESTDIR"
echo "BUILDDIR $BUILDDIR"

rm -rf "$DESTDIR"
mkdir -p "$BUILDDIR" "$DESTDIR"

# Copy headers & sources for libcxx, libcxxabi, libunwind
for component in libcxx libcxxabi libunwind; do
  cd "$DEPS_DIR/llvm-src/$component"

  DEST_INC_DIR=$DESTDIR/$component
  if [ "$component" = "libcxx" ]; then
  	DEST_INC_DIR=$DESTDIR
  fi

  # include
  echo "copy $PWD/include/* -> $DEST_INC_DIR/include/*"
	mkdir -p "$DEST_INC_DIR/libunwind"
  for f in $(find include -not -name CMakeLists.txt); do
    if [ -d "$f" ]; then
      mkdir -p "$DEST_INC_DIR/$f"
    else
      cp -a $f "$DEST_INC_DIR/$f"
    fi
  done

  # src
  echo "copy $PWD/src/* -> $DESTDIR/$component/src/*"
  mkdir -p "$DESTDIR/$component"
  cp LICENSE.txt "$DESTDIR/$component/LICENSE.txt"
  for f in $(find src -not -name CMakeLists.txt -and -not -path 'src/support/win32*'); do
    if [ -d "$f" ]; then
      mkdir -p "$DESTDIR/$component/$f"
    else
      cp -a $f "$DESTDIR/$component/$f"
    fi
  done
done

# create Makefile from template misc/libcxx.make
echo "# generated by $(basename "$0"), $(date)" > "$BUILDDIR"/Makefile

sed -E   's:@DEPSDIR@:'"$SRCDIR"'/deps:g' "$SRCDIR"/misc/libcxx.make \
| sed -E 's:@DESTDIR@:'"$DESTDIR"':g' \
>> "$BUILDDIR"/Makefile

LIBCXX_OBJS=
LIBCXXABI_OBJS=

cd "$DESTDIR"/libcxx/src
for f in $(find . -type f -name '*.cpp'); do
	f=${f:2}  # ./foo/bar.cpp -> foo/bar.cpp
	objf=${f:0:$(( ${#f} - 4 ))}.o  # foo/bar.cpp -> foo/bar.o
	LIBCXX_OBJS="$LIBCXX_OBJS $objf"
	if [[ "$f" == *"/"* ]]; then
		cat << EOF >> "$BUILDDIR"/Makefile
libcxx/$objf: \$(DESTDIR)/libcxx/src/$f
	@mkdir -p \$(dir \$@)
	\$(CXXC) \$(CFLAGS) \$(CFLAGS_LIBCPP) -c -o \$@ \$<
EOF
	fi
done

cd "$DESTDIR"/libcxxabi/src
for f in $(find . -type f -name '*.cpp'); do
	f=${f:2}  # ./foo/bar.cpp -> foo/bar.cpp
	objf=${f:0:$(( ${#f} - 4 ))}.o  # foo/bar.cpp -> foo/bar.o
	LIBCXXABI_OBJS="$LIBCXXABI_OBJS $objf"
	if [[ "$f" == *"/"* ]]; then
		cat << EOF >> "$BUILDDIR"/Makefile
libcxxabi/$objf: \$(DESTDIR)/libcxxabi/src/$f
	@mkdir -p \$(dir \$@)
	\$(CXXC) \$(CFLAGS) \$(CFLAGS_LIBCPPABI) -c -o \$@ \$<
EOF
	fi
done

cd "$BUILDDIR"
sed -i '' -E 's:@LIBCXX_OBJS@:'"$LIBCXX_OBJS"':g' Makefile
sed -i '' -E 's:@LIBCXXABI_OBJS@:'"$LIBCXXABI_OBJS"':g' Makefile
if ! make -j$(nproc); then
	status=$?
	echo "Make failed (exit status $status)" >&2
	exit $status
fi

# cleanup
cd "$SRCDIR"
rm -rf "$BUILDDIR"

echo "libc++ archives created:"
ls -lh "$DESTDIR"/lib

