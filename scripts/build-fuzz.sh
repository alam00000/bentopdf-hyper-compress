#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CORE="$ROOT/core"
JPEGLI="$CORE/third_party/jpegli"
STATIC="$CORE/prebuilt/libpdfium.a"
OUT="$ROOT/fuzz/out"
SAN="fuzzer"
[ "${1:-}" = "--asan" ] && SAN="fuzzer,address"

[ -f "$STATIC" ] || { echo "missing $STATIC (core/build/apply-tree.sh)" >&2; exit 1; }
mkdir -p "$OUT"

CXX="${HYPER_FUZZ_CXX:-}"
if [ -z "$CXX" ]; then
  for c in /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++; do
    [ -x "$c" ] && CXX="$c" && break
  done
fi
[ -n "$CXX" ] || {
  echo "libFuzzer needs LLVM clang: brew install llvm (or set HYPER_FUZZ_CXX)" >&2
  exit 1
}

SDKROOT="$(xcrun --show-sdk-path)"

"$CXX" -std=c++17 -O1 -g -fsanitize="$SAN" -isysroot "$SDKROOT" \
  -I"$ROOT/sdk/native/include" \
  -I"$JPEGLI" -I"$JPEGLI/include" \
  "$ROOT/fuzz/fuzz_compress.cc" \
  "$CORE/src/hyper_jpegli_wrap.cc" \
  -Wl,-force_load,"$STATIC" \
  "$JPEGLI/libjpegli-static.a" "$JPEGLI/libhwy.a" \
  -framework CoreFoundation -framework CoreGraphics -framework AppKit \
  -framework CoreText -framework Foundation \
  -o "$OUT/fuzz_compress"

echo "built: $OUT/fuzz_compress  (sanitizers: $SAN)"
