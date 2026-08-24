#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${HYPER_WASM_WORK:-$HERE/build-work}"
OUT="$HERE/prebuilt"

LIBJXL_TAG="v0.11.1"
SRC="$WORK/libjxl"
BUILD="$WORK/build-jpegli-wasm"

command -v emcmake >/dev/null || { echo "emcmake not on PATH" >&2; exit 6; }

mkdir -p "$WORK" "$OUT"

if [[ ! -d "$SRC" ]]; then
  echo "==> Cloning libjxl $LIBJXL_TAG"
  git clone --depth 1 --branch "$LIBJXL_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/libjxl/libjxl.git "$SRC"
fi

WASM_FLAGS="-fwasm-exceptions -sSUPPORT_LONGJMP=wasm"

echo "==> Configuring libjxl for wasm"
rm -rf "$BUILD"
emcmake cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_C_FLAGS="$WASM_FLAGS" \
  -DCMAKE_CXX_FLAGS="$WASM_FLAGS" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DJPEGXL_ENABLE_JPEGLI=ON \
  -DJPEGXL_ENABLE_JPEGLI_LIBJPEG=ON \
  -DJPEGXL_ENABLE_TOOLS=OFF \
  -DJPEGXL_ENABLE_DOXYGEN=OFF \
  -DJPEGXL_ENABLE_MANPAGES=OFF \
  -DJPEGXL_ENABLE_BENCHMARK=OFF \
  -DJPEGXL_ENABLE_EXAMPLES=OFF \
  -DJPEGXL_ENABLE_SKCMS=ON \
  -DJPEGXL_ENABLE_VIEWERS=OFF \
  -DJPEGXL_ENABLE_PLUGINS=OFF \
  -DJPEGXL_ENABLE_DEVTOOLS=OFF \
  -DJPEGXL_ENABLE_JNI=OFF \
  -DJPEGXL_ENABLE_TRANSCODE_JPEG=OFF \
  -DJPEGXL_BUNDLE_LIBPNG=OFF \
  -DJPEGXL_ENABLE_OPENEXR=OFF \
  -DJPEGXL_ENABLE_SJPEG=OFF \
  -DHWY_ENABLE_TESTS=OFF \
  -DHWY_ENABLE_EXAMPLES=OFF \
  -DHWY_ENABLE_CONTRIB=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  >/dev/null

echo "==> Building jpegli-static + hwy"
cmake --build "$BUILD" --target jpegli-static hwy -j "$(sysctl -n hw.ncpu)"

JPEGLI_A="$(find "$BUILD" -name 'libjpegli-static.a' | head -1)"
HWY_A="$(find "$BUILD" -name 'libhwy.a' | head -1)"
[[ -f "$JPEGLI_A" ]] || { echo "libjpegli-static.a not produced" >&2; exit 3; }
[[ -f "$HWY_A" ]] || { echo "libhwy.a not produced" >&2; exit 3; }

cp -f "$JPEGLI_A" "$OUT/libjpegli-static.a"
cp -f "$HWY_A" "$OUT/libhwy.a"

mkdir -p "$OUT/jpegli/include" "$OUT/jpegli/lib/jpegli" "$OUT/jpegli/lib/jxl/base"

GEN_INC="$(dirname "$(find "$BUILD" -name 'jconfig.h' -path '*jpegli*' | head -1)")"
[[ -f "$GEN_INC/jmorecfg.h" && -f "$GEN_INC/jpeglib.h" ]] || {
  echo "generated jpegli headers incomplete in $GEN_INC" >&2; exit 3; }
cp -f "$GEN_INC/jconfig.h" "$GEN_INC/jmorecfg.h" "$GEN_INC/jpeglib.h" "$OUT/jpegli/include/"
f="$(find "$SRC/lib/jpegli" -maxdepth 1 -name 'jerror.h' | head -1)"
[[ -n "$f" ]] && cp -f "$f" "$OUT/jpegli/include/jerror.h" || true

for h in types.h common.h encode.h decode.h; do
  [[ -f "$SRC/lib/jpegli/$h" ]] && cp -f "$SRC/lib/jpegli/$h" "$OUT/jpegli/lib/jpegli/$h" || true
done
[[ -f "$SRC/lib/jxl/base/include_jpeglib.h" ]] && \
  cp -f "$SRC/lib/jxl/base/include_jpeglib.h" "$OUT/jpegli/lib/jxl/base/include_jpeglib.h" || true

echo "==> wrote $OUT/libjpegli-static.a ($(du -h "$JPEGLI_A" | cut -f1)) + libhwy.a ($(du -h "$HWY_A" | cut -f1))"
