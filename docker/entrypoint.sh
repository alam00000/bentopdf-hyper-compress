#!/bin/sh
set -e
if [ "$#" -eq 0 ] || [ "$1" = "serve" ]; then
  exec /engine/cli/prebuilt/hyper-server
fi
exec /engine/cli/prebuilt/hyper "$@"
