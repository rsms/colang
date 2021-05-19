#!/bin/sh
set -e
cd "$(dirname "$0")"
. misc/_common.sh

CKIT_GIT_BRANCH=main  # latest


_in_PATH() {
  case "$PATH" in
    "$1:"*|*":$1:"*|*":$1") return 0 ;;
  esac
  return 1
}

_init_ckit() {
  local CKIT_DIR=$DEPS_DIR/ckit
  _log "---- ckit ----"

  _git_pull_if_needed https://github.com/rsms/ckit.git "$DEPS_DIR/ckit" "$CKIT_GIT_BRANCH" || true

  _log "Using ckit at $CKIT_DIR"

  # check PATH
  local inpath=false
  if ! _in_PATH "$CKIT_DIR/bin" || [ "$(command -v ckit 2>/dev/null)" != "$CKIT_DIR/bin/ckit" ]; then
    echo "$(command -v ckit 2>/dev/null) <> $CKIT_DIR/bin/ckit"
    local CKIT_DIR_NICE=$CKIT_DIR
    case "$CKIT_DIR_NICE" in
      "$HOME"/*) CKIT_DIR_NICE="\$HOME${CKIT_DIR_NICE:${#HOME}}"
    esac
    _log "Please add ckit to PATH:"
    _log "  export PATH=\"$CKIT_DIR_NICE/bin:\$PATH\""
  fi
}


_init_llvm() {
  _log "---- LLVM ----"
  $SHELL misc/build-llvm.sh
}


_init_ckit
_init_llvm
