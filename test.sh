#!/bin/sh
[ "$1" == "-w" ] || exec ckit test
exec ckit watch test
