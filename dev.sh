#!/bin/sh
SRCFILE=${1:-example/hello.w}

exec ckit watch \
  -rsh="ASAN_OPTIONS=detect_stack_use_after_return=1 {BUILD}/co build $SRCFILE" \
  co
