#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

case "$(uname -m)" in
  x86_64) CPU=x64 ;;
  aarch64|arm64) CPU=arm64 ;;
  *) echo "unsupported arch $(uname -m)" >&2; exit 2 ;;
esac

LIBJXL_TAG="v0.11.1"
WORK="${HYPER_JPEGLI_WORK:-$ROOT/docker/build-work}"
SRC="$WORK/libjxl"
BUILD="$WORK/build-jpegli-linux-$CPU"
OUT="$ROOT/core/third_party/jpegli/linux-$CPU"

mkdir -p "$WORK"

if [[ ! -d "$SRC" ]]; then
  git clone --depth 1 --branch "$LIBJXL_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/libjxl/libjxl.git "$SRC"
fi

rm -rf "$BUILD"
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
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

cmake --build "$BUILD" --target jpegli-static hwy -j "$(nproc)"

JPEGLI_A="$(find "$BUILD" -name 'libjpegli-static.a' | head -1)"
HWY_A="$(find "$BUILD" -name 'libhwy.a' | head -1)"
[[ -f "$JPEGLI_A" && -f "$HWY_A" ]] || { echo "jpegli build produced no archives" >&2; exit 3; }

mkdir -p "$OUT/include" "$OUT/lib/jpegli" "$OUT/lib/jxl/base"
cp -f "$JPEGLI_A" "$OUT/libjpegli-static.a"
cp -f "$HWY_A" "$OUT/libhwy.a"

GEN_INC="$(dirname "$(find "$BUILD" -name 'jconfig.h' -path '*jpegli*' | head -1)")"
[[ -f "$GEN_INC/jmorecfg.h" && -f "$GEN_INC/jpeglib.h" ]] || {
  echo "generated jpegli headers incomplete in $GEN_INC" >&2; exit 3; }
cp -f "$GEN_INC/jconfig.h" "$GEN_INC/jmorecfg.h" "$GEN_INC/jpeglib.h" "$OUT/include/"
[[ -f "$SRC/lib/jpegli/jerror.h" ]] && cp -f "$SRC/lib/jpegli/jerror.h" "$OUT/include/jerror.h"

for h in types.h common.h encode.h decode.h; do
  [[ -f "$SRC/lib/jpegli/$h" ]] && cp -f "$SRC/lib/jpegli/$h" "$OUT/lib/jpegli/$h"
done
[[ -f "$SRC/lib/jxl/base/include_jpeglib.h" ]] && \
  cp -f "$SRC/lib/jxl/base/include_jpeglib.h" "$OUT/lib/jxl/base/include_jpeglib.h"

echo "wrote $OUT"
