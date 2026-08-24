#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/.." && pwd)"
SRC="${1:?usage: apply-sources.sh <pdfium-checkout>}"
PATCHES="$CORE/patches"
[ -d "$SRC" ] || { echo "ERROR: $SRC missing."; exit 1; }
[ -d "$PATCHES" ] || { echo "ERROR: $PATCHES missing."; exit 1; }

PATCHED_FILES=$(cd "$PATCHES" && find . -type f | sed 's|^\./||' | sort)
DIRTY=$(git -C "$SRC" diff --name-only | sort)
UNEXPECTED=$(comm -23 <(echo "$DIRTY") <(echo "$PATCHED_FILES") | sed '/^$/d' || true)
if [ -n "$UNEXPECTED" ]; then
  echo "ERROR: tree has modifications outside the patch set:"
  echo "$UNEXPECTED" | sed 's/^/  /'
  echo "Commit them as patches or revert them, then re-run."
  exit 1
fi

echo "==> Resetting patched files to upstream @ pin"
echo "$PATCHED_FILES" | while read -r f; do
  git -C "$SRC" checkout -q -- "$f" 2>/dev/null || true
done

echo "==> Applying patches"
(cd "$PATCHES" && find . -type f | while read -r f; do
  mkdir -p "$SRC/$(dirname "$f")"
  cp "$f" "$SRC/$f"
done)

echo "==> Copying engine sources"
cp "$CORE/src/fpdf_compress.cpp" \
   "$CORE/src/hyper_type1_wrap.cc" \
   "$CORE/src/hyper_jbig2_wrap.cc" \
   "$CORE/src/hyper_generic_cmyk_icc.h" \
   "$SRC/fpdfsdk/"
cp "$CORE/include/fpdf_compress.h" "$SRC/public/fpdf_compress.h"

perl -pi -e \
  's|core/fxcodec/jbig2/JBig2_DocumentContext\.h|core/fxcodec/jbig2/jbig2_document_context.h|; s|third_party/harfbuzz-ng/src/src/|third_party/harfbuzz/src/src/|g;' \
  "$SRC/fpdfsdk/fpdf_compress.cpp"

echo "==> Vendored third_party (afdko / jbig2enc / leptonica)"
for d in afdko jbig2enc leptonica; do
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete "$CORE/third_party/$d/" "$SRC/third_party/$d/"
  else
    rm -rf "$SRC/third_party/$d"
    mkdir -p "$SRC/third_party/$d"
    cp -R "$CORE/third_party/$d/." "$SRC/third_party/$d/"
  fi
done

echo "==> Include preflight"
MISSING=0
while read -r inc; do
  if [ ! -f "$SRC/$inc" ] && [ ! -f "$SRC/third_party/$inc" ]; then
    echo "  MISSING: $inc"; MISSING=1
  fi
done < <(grep -hoE '#include "(core|public|fpdfsdk|constants|third_party)/[^"]+"' \
           "$SRC/fpdfsdk/fpdf_compress.cpp" | sed 's/#include "//; s/"//')
[ "$MISSING" = 0 ] || { echo "ERROR: engine includes missing from tree."; exit 1; }
