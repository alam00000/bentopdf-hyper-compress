#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

SRC="${PDFIUM_WASM_SRC:-$ROOT/core/build/gsrc/pdfium}"
SRC="$(cd "$SRC" && pwd)"
OUT="$SRC/out/wasm"

if [[ "${1:-}" == "--revert" ]]; then
  exec python3 "$HERE/scripts/patch_pdfium_tree.py" "$SRC" --revert
fi

pick_python() {
  local c p
  for c in "${HYPER_BUILD_PYTHON:-}" python3.11 /opt/homebrew/bin/python3.11 \
           python3.13 python3.12 python3 /usr/bin/python3; do
    [[ -n "$c" ]] || continue
    p="$(command -v "$c" 2>/dev/null || true)"
    [[ -x "$p" ]] || continue
    "$p" -c 'import plistlib,sys; sys.exit(0 if sys.version_info[:2] >= (3,10) else 1)' \
      2>/dev/null && { echo "$p"; return 0; }
  done
  return 1
}
PY="$(pick_python)" || {
  echo "need python3 >= 3.10 with a working pyexpat; set HYPER_BUILD_PYTHON" >&2
  exit 6
}
PYSHIM="$SRC/out/.pyshim"
mkdir -p "$PYSHIM"
ln -sf "$PY" "$PYSHIM/python3"
export PATH="$PYSHIM:$PATH"

EMCC="$(command -v emcc)" || { echo "emcc not on PATH" >&2; exit 6; }
EMROOT="$(dirname "$("$PY" -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$EMCC")")"
[[ -x "$EMROOT/emcc" ]] || EMROOT="$(cd "$(dirname "$EMCC")" && pwd)"
[[ -x "$EMROOT/emcc" ]] || { echo "cannot locate emscripten root from $EMCC" >&2; exit 6; }

GN="$SRC/buildtools/mac/gn"
NINJA="$SRC/third_party/ninja/ninja"
for tool in "$GN" "$NINJA"; do
  [[ -x "$tool" ]] || { echo "missing $tool" >&2; exit 5; }
done

echo "==> pdfium tree: $SRC"
echo "==> emscripten:  $EMROOT"
echo "==> python:      $PY"

"$ROOT/core/build/apply-tree.sh" --apply

python3 "$HERE/scripts/patch_pdfium_tree.py" "$SRC"
trap 'python3 "$HERE/scripts/patch_pdfium_tree.py" "$SRC" --revert >/dev/null' EXIT

mkdir -p "$OUT"
cat > "$OUT/args.gn" <<EOF
is_debug=false
treat_warnings_as_errors=false
pdf_use_skia=false
pdf_enable_xfa=false
pdf_enable_v8=false
pdf_enable_brotli=true
is_component_build=false
clang_use_chrome_plugins=false
pdf_is_standalone=true
use_debug_fission=false
use_custom_libcxx=false
use_sysroot=false
pdf_is_complete_lib=true
pdf_use_partition_alloc=false
symbol_level=0
is_clang=true
target_os="emscripten"
target_cpu="wasm"
emscripten_path="$EMROOT/"
EOF

echo "==> gn gen"
(cd "$SRC" && "$GN" gen "$OUT" --root="$SRC")

echo "==> ninja pdfium"
(cd "$SRC" && "$NINJA" -C "$OUT" pdfium)

AR="$OUT/obj/libpdfium.a"
[[ -f "$AR" ]] || { echo "expected $AR" >&2; exit 3; }

mkdir -p "$ROOT/wasm/prebuilt"
cp -f "$AR" "$ROOT/wasm/prebuilt/libpdfium.a"
echo "==> wrote wasm/prebuilt/libpdfium.a ($(du -h "$AR" | cut -f1))"
echo "==> run '$0 --revert' to restore the checkout for native builds"
