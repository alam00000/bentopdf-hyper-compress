#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$ROOT/packages/npm/hyper-compress-wasm"

if [ ! -f "$ROOT/wasm/out/hyper-compress.wasm" ]; then
  echo "wasm/out/hyper-compress.wasm not found; run make wasm first" >&2
  exit 1
fi
if [ ! -f "$ROOT/dist/wasm/sdk.js" ]; then
  echo "dist/wasm/sdk.js not found; run npm run build first" >&2
  exit 1
fi

rm -rf "$PKG/lib" "$PKG/engine"
mkdir -p "$PKG/lib/wasm" "$PKG/lib/sdk/node" "$PKG/engine"

for f in sdk glue; do
  cp "$ROOT/dist/wasm/$f.js" "$ROOT/dist/wasm/$f.d.ts" "$PKG/lib/wasm/"
done
for f in options presets pdfa errors target; do
  cp "$ROOT/dist/sdk/node/$f.js" "$ROOT/dist/sdk/node/$f.d.ts" "$PKG/lib/sdk/node/"
done

cp "$ROOT/wasm/out/hyper-compress.js" "$ROOT/wasm/out/hyper-compress.wasm" "$PKG/engine/"
cp "$ROOT/LICENSE" "$PKG/LICENSE"

echo "assembled $PKG"
