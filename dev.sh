#!/bin/sh
SRCFILE=${1:-example/hello.co}
export ASAN_OPTIONS=detect_stack_use_after_return=1
exec ckit watch -rsh="{BUILD}/co build $SRCFILE" co
