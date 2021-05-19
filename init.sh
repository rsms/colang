#!/bin/sh
set -e
cd "$(dirname "$0")"
. misc/_common.sh

[ "$1" != "-quiet" ] || OPT_QUIET=true


_init_githooks() {
  _log "---- git hooks ----"
  if [ -d .git ] && [ -d misc/git-hooks ]; then
    mkdir -p .git/hooks
    _pushd .git/hooks
    for f in ../../misc/git-hooks/*.sh; do
      HOOKFILE=$(basename "$f" .sh)
      if ! [ -f "$HOOKFILE" ]; then
        ln -vfs "$f" "$HOOKFILE"
      fi
    done
    _popd
  fi
}


_init_ckit() {
  local CKIT_GIT_BRANCH=main
  local CKIT_DIR=$DEPS_DIR/ckit
  _log "---- ckit ----"

  _git_pull_if_needed https://github.com/rsms/ckit.git "$DEPS_DIR/ckit" "$CKIT_GIT_BRANCH" || true

  _log "Using ckit at $CKIT_DIR"

  # check PATH
  local inpath=false
  if ! _in_PATH "$CKIT_DIR/bin" ||
     [ "$(command -v ckit 2>/dev/null)" != "$CKIT_DIR/bin/ckit" ]
  then
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
  local args=()
  if $OPT_QUIET; then
    args+=(-quiet)
  fi
  $SHELL misc/build-llvm.sh "${args[@]}"
}


_init_githooks
_init_ckit
_init_llvm
