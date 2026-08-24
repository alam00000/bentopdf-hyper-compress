#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PRE="$HERE/prebuilt"
OUT="$HERE/out"

PDFIUM_SRC="${PDFIUM_WASM_SRC:-$ROOT/core/build/gsrc/pdfium}"
PDFIUM_SRC="$(cd "$PDFIUM_SRC" && pwd)"

for a in libpdfium.a libqpdf.a libjpegli-static.a; do
  [[ -f "$PRE/$a" ]] || { echo "missing $PRE/$a -- run the matching build script first" >&2; exit 5; }
done

command -v emcc >/dev/null || { echo "emcc not on PATH" >&2; exit 6; }
mkdir -p "$OUT"

ABI_FLAGS=(-fwasm-exceptions -sSUPPORT_LONGJMP=wasm)

echo "==> Compiling jpegli encoder shim"
em++ -std=c++17 -O3 "${ABI_FLAGS[@]}" -DNDEBUG -fvisibility=hidden \
  -I"$PRE/jpegli" -I"$PRE/jpegli/include" \
  -c "$ROOT/core/src/hyper_jpegli_wrap.cc" \
  -o "$OUT/hyper_jpegli_wrap.o"

echo "==> Linking hyper-compress module"
em++ -O3 "${ABI_FLAGS[@]}" \
  -I"$PDFIUM_SRC" \
  -I"$ROOT/core/include" \
  -I"$PRE/include" \
  "$HERE/wasm_entry.cc" \
  "$OUT/hyper_jpegli_wrap.o" \
  "$PRE/libpdfium.a" \
  "$PRE/libqpdf.a" \
  "$PRE/libjpegli-static.a" \
  "$PRE/libhwy.a" \
  -lz -ljpeg \
  -Wl,--allow-multiple-definition \
  -o "$OUT/hyper-compress.js" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=HyperCompressModule \
  -sALLOW_MEMORY_GROWTH=1 \
  -sMAXIMUM_MEMORY=4GB \
  -sSTACK_SIZE=2MB \
  -sENVIRONMENT=web,worker,node \
  -sFILESYSTEM=0 \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8","HEAPU32","HEAP32"]' \
  -sEXPORTED_FUNCTIONS='["_hyper_compress_buffer","_hyper_decrypt_buffer","_hyper_pack_buffer","_hyper_pack_buffer_pdfa","_hyper_get_xmp","_hyper_set_xmp","_hyper_last_error","_hyper_free","_malloc","_free"]'

echo "==> built:"
ls -la "$OUT"/hyper-compress.{js,wasm}
