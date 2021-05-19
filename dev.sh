#!/bin/sh
exec ckit watch \
  -rsh="ASAN_OPTIONS=detect_stack_use_after_return=1 {BUILD}/co build example/hello.w" \
  co
