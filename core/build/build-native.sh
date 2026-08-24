#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$CORE/.." && pwd)"

ARCH="$(uname -m)"
SRC="$CORE/src"
DRV="$CORE/driver"
INC="$ROOT/sdk/native/include"
JPEGLI="$CORE/third_party/jpegli"
STATIC="$CORE/prebuilt/libpdfium.a"
OUTDIR="$CORE/build/out"
BINDIR="$ROOT/cli/prebuilt"
mkdir -p "$OUTDIR" "$BINDIR"

if [ ! -f "$STATIC" ]; then
  echo "ERROR: $STATIC not found." >&2
  echo "  Ship the prebuilt libpdfium.a." >&2
  exit 1
fi
if ! nm "$STATIC" 2>/dev/null | grep ' T _HyperCompress_Execute' >/dev/null; then
  echo "ERROR: libpdfium.a does not export HyperCompress_*." >&2
  exit 1
fi

clang++ -std=c++17 -O2 -arch "$ARCH" -fvisibility=hidden -DNDEBUG \
  -I"$JPEGLI" -I"$JPEGLI/include" \
  -c "$SRC/hyper_jpegli_wrap.cc" \
  -o "$OUTDIR/hyper_jpegli_wrap.o"

clang++ -std=c++17 -I"$INC" -c "$DRV/hpdf_worker.cpp" -o "$OUTDIR/hpdf_worker.o"

clang++ -o "$BINDIR/hpdf-worker" \
  "$OUTDIR/hpdf_worker.o" \
  "$OUTDIR/hyper_jpegli_wrap.o" \
  -Wl,-force_load,"$STATIC" \
  "$JPEGLI/libjpegli-static.a" "$JPEGLI/libhwy.a" \
  -framework CoreFoundation -framework CoreGraphics -framework AppKit \
  -framework CoreText -framework Foundation

if [ -f "$DRV/hpdf_render.cpp" ]; then
  clang++ -std=c++17 -I"$INC" -c "$DRV/hpdf_render.cpp" -o "$OUTDIR/hpdf_render.o"
  clang++ -o "$BINDIR/hpdf-render" \
    "$OUTDIR/hpdf_render.o" \
    "$OUTDIR/hyper_jpegli_wrap.o" \
    -Wl,-force_load,"$STATIC" \
    "$JPEGLI/libjpegli-static.a" "$JPEGLI/libhwy.a" \
    -framework CoreFoundation -framework CoreGraphics -framework AppKit \
    -framework CoreText -framework Foundation
fi

echo "built: $BINDIR/hpdf-worker"
