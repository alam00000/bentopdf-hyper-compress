#!/usr/bin/env bash
set -euo pipefail

PDFIUM_PIN=162c9521f74c17bb0c8595608f23ab22cec3d407

HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/.." && pwd)"
SRC="$HERE/gsrc/pdfium"
PATCHES="$CORE/patches"
if [ "${APPLY_TREE_ASAN:-0}" = "1" ]; then
  OUT="$SRC/out/mac-arm64-nov8-asan"
else
  OUT="$SRC/out/mac-arm64-nov8"
fi

[ -d "$SRC" ] || { echo "ERROR: $SRC missing  - bootstrap with gclient (see header)."; exit 1; }
[ -d "$PATCHES" ] || { echo "ERROR: $PATCHES missing."; exit 1; }

CUR="$(git -C "$SRC" rev-parse HEAD)"
if [ "$CUR" != "$PDFIUM_PIN" ]; then
  echo "ERROR: tree at $CUR, pin is $PDFIUM_PIN."
  echo "  cd $HERE/gsrc && gclient sync --revision pdfium@$PDFIUM_PIN -D"
  exit 1
fi

"$HERE/apply-sources.sh" "$SRC"

[ "${1:-}" = "--apply" ] && { echo "Applied (no build)."; exit 0; }

mkdir -p "$OUT"
cat > "$OUT/args.gn" <<'ARGS'
is_debug = false
treat_warnings_as_errors = false
pdf_use_skia = false
pdf_enable_xfa = false
pdf_enable_v8 = false
pdf_enable_brotli = true
is_component_build = false
clang_use_chrome_plugins = false
pdf_is_standalone = true
pdf_is_complete_lib = true
pdf_use_partition_alloc = false
use_debug_fission = false
use_custom_libcxx = false
use_sysroot = false
symbol_level = 0
target_os = "mac"
target_cpu = "arm64"
ARGS
if [ "${APPLY_TREE_ASAN:-0}" = "1" ]; then
  printf 'is_asan = true\nis_ubsan = true\nsymbol_level = 1\n' >> "$OUT/args.gn"
fi

GN="$SRC/buildtools/mac/gn"
NINJA="$SRC/third_party/ninja/ninja"
echo "==> gn gen"
(cd "$SRC" && "$GN" gen "$OUT" --root="$SRC")
echo "==> ninja pdfium"
(cd "$SRC" && "$NINJA" -C "$OUT" pdfium)

AR="$OUT/obj/libpdfium.a"
[ -f "$AR" ] || { echo "ERROR: $AR not produced."; exit 1; }
NM_SYMS=$(nm "$AR" 2>/dev/null | grep -E " T _(HyperCompress_|FPDF_InitLibrary)" || true)
for sym in HyperCompress_CreateOptions HyperCompress_SetOption HyperCompress_Execute \
           HyperCompress_DocIsSigned HyperCompress_CloseOptions FPDF_InitLibrary; do
  echo "$NM_SYMS" | grep -q " T _$sym\$" || { echo "ERROR: $sym missing from archive."; exit 1; }
done
mkdir -p "$CORE/prebuilt" "$CORE/../sdk/native/lib"
[ -f "$CORE/prebuilt/libpdfium.a" ] && [ ! -f "$CORE/prebuilt/libpdfium.a.backup" ] && \
  mv "$CORE/prebuilt/libpdfium.a" "$CORE/prebuilt/libpdfium.a.backup"
cp "$AR" "$CORE/prebuilt/libpdfium.a"
cp "$AR" "$CORE/../sdk/native/lib/libpdfium.a"
mkdir -p "$CORE/../sdk/native/include/public"
rsync -a --delete "$SRC/public/" "$CORE/../sdk/native/include/public/"
cp "$CORE/include/fpdf_compress.h" "$CORE/../sdk/native/include/public/fpdf_compress.h"
echo "==> staged: core/prebuilt/libpdfium.a ($(du -h "$AR" | cut -f1)) @ google-pdfium $PDFIUM_PIN"
