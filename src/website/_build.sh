#!/bin/sh
set -e
cd "$(dirname "$0")"
. ../../misc/_common.sh

# npm info rsms-mkweb
MKWEB_AR_VERSION=0.1.10
MKWEB_AR_URL=https://registry.npmjs.org/rsms-mkweb/-/rsms-mkweb-${MKWEB_AR_VERSION}.tgz
MKWEB_AR_URL_SHA1=f6281e10b8f38cf9ae8e5529f8cbc02f18d6e2c8
MKWEB_AR_NAME=mkweb-${MKWEB_AR_VERSION}.tgz
MKWEB_EXE=$DEPS_DIR/mkweb/mkweb-${MKWEB_AR_VERSION}

if ! [ -f "$MKWEB_EXE" ]; then
  _download "$MKWEB_AR_URL" $MKWEB_AR_URL_SHA1 "$MKWEB_AR_NAME"
  MKWEB_DIR=$(dirname "$MKWEB_EXE")
  rm -rf "$MKWEB_DIR"
  _extract_tar "$(_downloaded_file "$MKWEB_AR_NAME")" "$MKWEB_DIR"
  (cd "$MKWEB_DIR" &&
    npm i --omit dev --no-audit --no-bin-links --no-fund --no-package-lock)
  cp "$MKWEB_DIR/dist/mkweb" "$MKWEB_EXE"
  chmod +x "$MKWEB_EXE"
fi

MKWEB_ARGS=( -verbose -opt )
# if [ -z "$1" -o "$1" != "-w" ]; then
#   MKWEB_ARGS+=( -opt )
# fi

exec "$MKWEB_EXE" "${MKWEB_ARGS[@]}" "$@"
