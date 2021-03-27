#!/bin/bash
USER_PWD="$PWD"
cd "$(dirname "$0")/.."
. misc/_common.sh
set -e

HELP=false
RUN=false
RUN_EXE=bin/co
RUN_ARGS="build ./example"
OPT_CLEAN=false
OPT_RELEASE=false
MAKE_ARGS="-j$(nproc)"

while [[ $# -gt 0 ]]; do
	case "$1" in
	-h*|--help) HELP=true; shift ;;
	-run)       RUN=true; shift ;;
	-run=*)     RUN=true; RUN_ARGS="${1:5}"; shift ;;
	-clean)     OPT_CLEAN=true; shift ;;
	-release)   OPT_RELEASE=true; shift ;;
	--)         MAKE_ARGS="$MAKE_ARGS $@"; break ;;
	*)          MAKE_ARGS="$MAKE_ARGS $1"; shift ;;
	esac
done
if $HELP; then
	cat <<-EOF
	Rebuild incrementally as source files change
	usage: $0 [options] [args to make ...]
	options:
		-run[=<args>]  Run $RUN_EXE [with optional <args>] after building
		-clean         Discard build caches before building (just once in -watch mode)
		-release       Build release build instead of the default debug build
		-h, -help      Print help and exit
	EOF
	exit 0
fi

# $OPT_RELEASE || MAKE_ARGS="$MAKE_ARGS DEBUG=1 SANITIZE=1" # asan not compat with coco
$OPT_RELEASE || MAKE_ARGS="$MAKE_ARGS DEBUG=1"

! $OPT_CLEAN || make clean

WATCH_INDICATOR_PIDFILE="$(_tmpfile).pid"

RUN_PIDFILE=
if $RUN; then
	RUN_PIDFILE="$(_tmpfile).pid"
	ATEXIT+=( "_pidfile_kill '$RUN_PIDFILE'" )
fi

[ -n "$COMAXPROCS" ] || export COMAXPROCS=4

_run_after_build() {
	_pidfile_kill "$RUN_PIDFILE"
	set +e
	pushd "$USER_PWD" >/dev/null
	local cmd=$(_relpath "$RUN_EXE")
	# echo "$cmd" $RUN_ARGS
	[[ "$cmd" == "/" ]] || cmd="./$cmd"
	( "$cmd" $RUN_ARGS &
		echo $! > "$RUN_PIDFILE"
		wait
		rm -rf "$RUN_PIDFILE" ) &
	popd >/dev/null
	set -e
}

_watch_indicator() {
	local spin=( '...' ' ..' '  .' '   ' '.  ' '.. ' )
	local n=${#spin[@]}
	local i=$(( $n - 1 )) # -1 so we start with spin[0]
	local style_start="\e[2m" # 2 = dim/faint/low-intensity
	local style_end="\e[m"
	if [ ! -t 1 ]; then
		style_start=
		style_end=
	fi
	# wait for run to exit (has no effect if -run is not used)
	while true; do
		[ -f "$RUN_PIDFILE" ] || break
		sleep 0.5
	done
	while true; do
		i=$(( (i+1) % $n ))
		printf "\r${style_start}Waiting for changes to source files${spin[$(( $i ))]}${style_end} "
		sleep 0.5
	done
}

_watch_indicator_stop() {
	_pidfile_kill "$WATCH_INDICATOR_PIDFILE"
	printf "\r"
}

ATEXIT+=( "_pidfile_kill '$WATCH_INDICATOR_PIDFILE'" )
while true; do
	echo -e "\x1bc"  # clear screen ("scroll to top" style)
	if make $MAKE_ARGS; then
		_run_after_build
	fi
	_watch_indicator &
	echo $! > "$WATCH_INDICATOR_PIDFILE"
	fswatch \
		--one-event \
		--latency=0.2 \
		--extended \
		--exclude='.*' --include='\.(c|h)$' \
		--recursive \
		./src
	_watch_indicator_stop
	echo "———————————————————— restarting ————————————————————"
done
