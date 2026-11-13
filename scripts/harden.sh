#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

EXES="$ROOT/cli/prebuilt/hpdf-worker $ROOT/cli/prebuilt/hpdf-render"
DYLIB="$ROOT/sdk/native/lib/libhypercompress.dylib"
ADDON="$ROOT/cli/prebuilt/hyper-compress.node"

for f in $EXES; do
  [ -f "$f" ] && strip "$f"
done
[ -f "$DYLIB" ] && strip -x "$DYLIB"
[ -f "$ADDON" ] && strip -x "$ADDON"

IDENTITY="${HPDF_SIGN_IDENTITY:--}"
for f in $EXES "$DYLIB" "$ADDON"; do
  [ -f "$f" ] && codesign -f -s "$IDENTITY" "$f"
done

echo "hardened: strip + sign complete"
