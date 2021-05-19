set -e

SCRIPT_FILE=${BASH_SOURCE[0]}
[ -n "$SCRIPT_FILE" ] || SCRIPT_FILE=${(%):-%N}  # zsh
[ -n "$PROJECT" ] || PROJECT=$(cd "$(dirname "$SCRIPT_FILE")/.." && pwd)
ARGV0="$0"
DEPS_DIR="$PROJECT/deps"
WORK_DIR="$PROJECT/work"
WORK_BUILD_DIR="$WORK_DIR/build"
DOWNLOAD_DIR="$WORK_DIR/download"
TMPFILES_LIST="$WORK_DIR/tmp/tmpfiles.$$"

# internal state
SOURCE_DIR_STACK=()
ATEXIT=()

__atexit() {
  set +e
  cd "$PROJECT" # in case PWD is a dir being unmounted or deleted
  # execute each command in stack order (FIFO)
  local idx
  for (( idx=${#ATEXIT[@]}-1 ; idx>=0 ; idx-- )) ; do
    # echo "[atexit]" "${ATEXIT[idx]}"
    eval "${ATEXIT[idx]}"
  done
  # clean up temporary files
  if [ -f "$TMPFILES_LIST" ]; then
    while IFS= read -r f; do
      # echo "[atexit]" rm -rf "$f"
      rm -rf "$f"
    done < "$TMPFILES_LIST"
    rm -f "$TMPFILES_LIST"
  fi
  set -e
}
_onsigint() {
  echo
  exit
}
trap __atexit EXIT
trap _onsigint SIGINT


_log() { echo "$@" >&2; }


_err() {
  echo "$ARGV0:" "$@" >&2
  exit 1
}

# _relpath path [parentpath]
# Prints path relative to parentpath (or PWD if parentpath is not given)
_relpath() {
  echo "${1##${2:-$PWD}/}"
}

# _checksum [-sha256|-sha512] [<file>]
# Prints the sha1 (or sha256 or sha512) sum of file's content (or stdin if no <file> is given)
_checksum() {
  local prog=sha1sum
  if [ "$1" == "-sha256" ]; then prog=sha256sum; shift; fi
  if [ "$1" == "-sha512" ]; then prog=sha512sum; shift; fi
  $prog "$@" | cut -f 1 -d ' '
}

# _random_id [bytecount]
_random_id() {
  local bytes=${1:-32}
  head -c $bytes /dev/urandom | sha1sum | cut -d' ' -f1
}

_pushd() {
  local old_pwd=$PWD
  pushd "$1" >/dev/null
  echo "changed directory to $PWD"
}

_popd() {
  local old_pwd=$PWD
  popd >/dev/null
  echo "returned to directory $PWD"
}

# _tmpfile
# Prints a unique filename that can be written to which is automatically
# deleted when the script exits.
_tmpfile() {
  mkdir -p "$WORK_DIR/tmp"
  local file="$WORK_DIR/tmp/$(_random_id 8).$$"
  mkdir -p "$(dirname "$TMPFILES_LIST")"
  echo "$file" >> "$TMPFILES_LIST"
  echo "$file"
}

# _downloaded_file filename|url
# Prints absolute path to a file downloaded by _download
_downloaded_file() {
  echo "$DOWNLOAD_DIR/$(basename "$1")"
}

# _verify_checksum [-silent] file checksum
# checksum can be prefixed with sha1: sha256: or sha512: (e.g. sha256:checksum)
_verify_checksum() {
  local silent
  if [ "$1" == "-silent" ]; then silent=y; shift; fi
  local file="$1"
  local expected="$2"
  local prog=sha1sum
  case "$expected" in
    sha1:*)   expected=${expected:4};;
    sha256:*) expected=${expected:7}; prog=sha256sum ;;
    sha512:*) expected=${expected:7}; prog=sha512sum ;;
  esac
  local actual=$("$prog" "$file" | cut -f 1 -d ' ')
  if [ "$expected" != "$actual" ]; then
    if [ -z "$silent" ]; then
      echo "Checksum mismatch: $file" >&2
      echo "  Actual:   $actual" >&2
      echo "  Expected: $expected" >&2
    fi
    return 1
  fi
}

# _download url checksum [filename]
# Download file from url. If filename is not given (basename url) is used.
# If DOWNLOAD_DIR/filename exists, then only download if the checksum does not match.
_download() {
  local url="$1"
  local checksum="$2"
  local filename="$DOWNLOAD_DIR/$(basename "${3:-"$url"}")"
  echo "filename $filename"
  while [ ! -e "$filename" ] || ! _verify_checksum -silent "$filename" "$checksum"; do
    if [ -n "$did_download" ]; then
      echo "Checksum for $filename failed" >&2
      echo "  Actual:   $(_checksum "$filename")" >&2
      echo "  Expected: $checksum" >&2
      return 1
    fi
    rm -rf "$filename"
    echo "fetch $url"
    mkdir -p "$(dirname "$filename")"
    curl -L --progress-bar -o "$filename" "$url"
    did_download=y
  done
}

# _extract_tar tarfile outdir
_extract_tar() {
  local tarfile="$1"
  local outdir="$2"
  local name=$(basename "$tarfile")
  [ -e "$tarfile" ] || _err "$tarfile not found"
  local extract_dir="$WORK_BUILD_DIR/.extract-$name"
  rm -rf "$extract_dir"
  mkdir -p "$extract_dir"
  echo "extracting ${tarfile##$PWD/} -> ${outdir##$PWD/}"
  XZ_OPT='-T0' tar -C "$extract_dir" -xf "$tarfile"
  rm -rf "$outdir"
  mkdir -p "$(dirname "$outdir")"
  mv -f "$extract_dir"/* "$outdir"
  rm -rf "$extract_dir"
}

# _pushsrc filename|url
_pushsrc() {
  local filename="$(basename "$1")"
  local archive="$DOWNLOAD_DIR/$filename"
  local name="$(basename "$(basename "$filename" .xz)" .gz)"
  name="$(basename "$(basename "$name" .tar)" .tgz)"
  _extract_tar "$archive" "$WORK_BUILD_DIR/$name"
  SOURCE_DIR_STACK+=( "$WORK_BUILD_DIR/$name" )
  _pushd "$WORK_BUILD_DIR/$name"
}

# _download_pushsrc url sha1sum [filename]
_download_pushsrc() {
  local url=$1
  local checksum=$2
  local filename="$DOWNLOAD_DIR/$(basename "${3:-"$url"}")"
  _download "$url" "$checksum" "$filename"
  _pushsrc "$filename"
}

_popsrc() {
  _popd
  # cleanup
  if [ ${#SOURCE_DIR_STACK[@]} -gt 0 ]; then
    local i=$(( ${#SOURCE_DIR_STACK[@]} - 1 ))
    local srcdir="${SOURCE_DIR_STACK[$i]}"
    unset "SOURCE_DIR_STACK[$i]"
    rm -rf "$srcdir"
  fi
}

# _pidfile_kill pidfile
_pidfile_kill() {
  local pidfile="$1"
  # echo "_pidfile_kill $1"
  if [ -f "$pidfile" ]; then
    local pid=$(cat "$pidfile" 2>/dev/null)
    # echo "_pidfile_kill pid=$pid"
    [ -z "$pid" ] || kill $pid 2>/dev/null || true
    rm -f "$pidfile"
  fi
}

# _git_HEAD_is <hash|name>
_git_HEAD_is() {
  local want_hash=$(git rev-parse "$1")
  local head_hash=$(git rev-parse HEAD)
  [ "$want_hash" = "$head_hash" ] || return 1
}

# _git_hash_is_symbolic <hash|name>
_git_hash_is_symbolic() {
  if (echo "$1" | grep -q -E '^[a-fA-F0-9]+$'); then
    return 1
  fi
}

# _git_is_dirty [<gitdir>]
_git_is_dirty() {
  local gitargs0=()
  local gitargs1=(--untracked-files=no --ignore-submodules=dirty --porcelain)
  [ -z "$1" ] || gitargs0+=( -C "$1" )
  [ -n "$(git "${gitargs0[@]}" status "${gitargs1[@]}" 2> /dev/null)" ] || return 1
}

# _git_pull_if_needed <repourl> <gitdir> <hash|name>
_git_pull_if_needed() {
  local repourl=$1 ; shift
  local gitdir=$1  ; shift
  local githash=$1 ; shift
  if [ -d "$gitdir" ]; then
    _pushd "$gitdir"

    local githash_is_symbolic=false
    if _git_hash_is_symbolic "$githash"; then
      githash_is_symbolic=true
      echo git fetch origin
           git fetch origin
    fi

    if _git_HEAD_is "$githash"; then
      _popd
      return 1  # up to date
    fi

    if _git_is_dirty; then
      _log "git pull aborted: there are local uncommitted changes in $PWD"
      git status -s --untracked-files=no --ignore-submodules=dirty
      exit 1
    fi

    if ! $githash_is_symbolic; then
      echo git fetch origin
           git fetch origin
    fi

    echo git checkout "$githash"
         git checkout "$githash"
  else
    echo git clone --branch "$githash" "$repourl" "$gitdir"
         git clone --branch "$githash" "$repourl" "$gitdir"
  fi
  _popd
}
