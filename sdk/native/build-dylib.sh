#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/../../core" && pwd)"
ARCH="$(uname -m)"
JPEGLI="$CORE/third_party/jpegli"
STATIC="$CORE/prebuilt/libpdfium.a"
WORK="$CORE/build/out"
mkdir -p "$WORK"

if [ ! -f "$STATIC" ]; then
  echo "ERROR: $STATIC not found (see core/build/REBUILD.md)." >&2
  exit 1
fi

clang++ -std=c++17 -O2 -arch "$ARCH" -fvisibility=hidden -DNDEBUG \
  -I"$JPEGLI" -I"$JPEGLI/include" \
  -c "$CORE/src/hyper_jpegli_wrap.cc" -o "$WORK/hyper_jpegli_wrap.o"

clang -std=c11 -O2 -arch "$ARCH" -I"$HERE/include" \
  -c "$HERE/hpdf_wrap.c" -o "$WORK/hpdf_wrap.o"

clang++ -shared -arch "$ARCH" -o "$HERE/lib/libhypercompress.dylib" \
  "$WORK/hpdf_wrap.o" "$WORK/hyper_jpegli_wrap.o" \
  -Wl,-force_load,"$STATIC" \
  "$JPEGLI/libjpegli-static.a" "$JPEGLI/libhwy.a" \
  -exported_symbols_list "$HERE/hpdf.syms" \
  -install_name @rpath/libhypercompress.dylib \
  -framework CoreFoundation -framework CoreGraphics -framework CoreText \
  -framework AppKit -framework Foundation

echo "built: $HERE/lib/libhypercompress.dylib"
