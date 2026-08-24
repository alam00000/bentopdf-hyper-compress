#!/bin/sh
set -e
if [ "$#" -eq 0 ] || [ "$1" = "serve" ]; then
  exec node /engine/dist/server/server.js
fi
exec node /engine/dist/cli/bin/hyper.js "$@"
