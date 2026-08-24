#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${HYPER_WASM_WORK:-$HERE/build-work}"
PREFIX="$HERE/prebuilt"

QPDF_VERSION="12.3.2"
SHA256_TARBALL="6cba2f9f2cd887d905faeb99e0e51a307b217920d1bbf3e9cfbb2e8178a2deda"

command -v emcmake >/dev/null || { echo "emcmake not on PATH (source emsdk_env.sh)" >&2; exit 6; }

mkdir -p "$WORK" "$PREFIX"
cd "$WORK"

TARBALL="qpdf-$QPDF_VERSION.tar.gz"
if [[ ! -f "$TARBALL" ]]; then
  echo "==> Downloading qpdf $QPDF_VERSION"
  curl -fsSL -o "$TARBALL" \
    "https://github.com/qpdf/qpdf/releases/download/v$QPDF_VERSION/$TARBALL"
fi
echo "$SHA256_TARBALL  $TARBALL" | shasum -a 256 -c -

if [[ ! -d "qpdf-$QPDF_VERSION" ]]; then
  tar xzf "$TARBALL"
fi

echo "==> Ensuring emscripten ports (zlib, libjpeg)"
embuilder build zlib libjpeg >/dev/null 2>&1 || true
SYSROOT="$(em-config CACHE 2>/dev/null)/sysroot"
[[ -f "$SYSROOT/lib/wasm32-emscripten/libz.a" ]] || {
  echo "emscripten zlib port missing under $SYSROOT" >&2; exit 5; }

echo "==> Configuring libqpdf for wasm"
emcmake cmake -S "qpdf-$QPDF_VERSION" -B build-qpdf-wasm \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DUSE_IMPLICIT_CRYPTO=OFF \
  -DREQUIRE_CRYPTO_NATIVE=ON \
  -DDEFAULT_CRYPTO=native \
  -DBUILD_DOC=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_CXX_FLAGS="-fwasm-exceptions" \
  -DPKG_CONFIG_EXECUTABLE=/usr/bin/false \
  -DZLIB_INCLUDE_DIR="$SYSROOT/include" \
  -DZLIB_LIBRARY="$SYSROOT/lib/wasm32-emscripten/libz.a" \
  -DZLIB_H_PATH="$SYSROOT/include" \
  -DZLIB_LIB_PATH="$SYSROOT/lib/wasm32-emscripten/libz.a" \
  -DJPEG_INCLUDE_DIR="$SYSROOT/include" \
  -DJPEG_LIBRARY="$SYSROOT/lib/wasm32-emscripten/libjpeg.a" \
  -DLIBJPEG_H_PATH="$SYSROOT/include" \
  -DLIBJPEG_LIB_PATH="$SYSROOT/lib/wasm32-emscripten/libjpeg.a" \
  >/dev/null

echo "==> Building libqpdf"
cmake --build build-qpdf-wasm --target libqpdf -j "$(sysctl -n hw.ncpu)"

AR="$(find build-qpdf-wasm -name 'libqpdf.a' | head -1)"
[[ -f "$AR" ]] || { echo "libqpdf.a not produced" >&2; exit 3; }

cp -f "$AR" "$PREFIX/libqpdf.a"
mkdir -p "$PREFIX/include"
cp -Rp "qpdf-$QPDF_VERSION/include/qpdf" "$PREFIX/include/"
for gen in libqpdf/qpdf/qpdf-config.h include/qpdf/auto_job_schema.hh; do
  [[ -f "build-qpdf-wasm/$gen" ]] && cp -f "build-qpdf-wasm/$gen" "$PREFIX/include/qpdf/" || true
done

echo "==> wrote $PREFIX/libqpdf.a ($(du -h "$AR" | cut -f1))"
