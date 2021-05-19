#!/bin/sh
echo "${SHELL:-sh} init.sh -quiet"
exec ${SHELL:-sh} init.sh -quiet
