#!/bin/sh
if [ "$1" == "-w" ]; then
  shift
  exec ckit watch -wf=test/parse test "$@"
fi
exec ckit test "$@"
