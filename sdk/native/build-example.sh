#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

clang -std=c11 -O2 -arch "$(uname -m)" \
  -I"$HERE/include" \
  "$HERE/example.c" -o "$HERE/example" \
  -Wl,-rpath,"$HERE/lib" \
  "$HERE/lib/libhypercompress.dylib"

echo "built: $HERE/example"
