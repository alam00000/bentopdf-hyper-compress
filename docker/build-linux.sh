#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

case "$(uname -m)" in
  x86_64) CPU=x64 ;;
  aarch64|arm64) CPU=arm64 ;;
  *) echo "unsupported arch $(uname -m)" >&2; exit 2 ;;
esac

SRC="${PDFIUM_SRC:-$ROOT/core/build/gsrc/pdfium}"
PIN="$(grep '^PDFIUM_PIN=' "$ROOT/core/build/apply-tree.sh" | cut -d= -f2)"
if [ ! -d "$SRC" ]; then
  echo "ERROR: pdfium checkout not found at $SRC" >&2
  echo "  mkdir -p $(dirname "$SRC") && cd $(dirname "$SRC")" >&2
  echo "  gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git" >&2
  echo "  gclient sync --revision pdfium@$PIN -D --no-history" >&2
  exit 1
fi
CUR="$(git -C "$SRC" rev-parse HEAD)"
[ "$CUR" = "$PIN" ] || { echo "ERROR: tree at $CUR, pin is $PIN." >&2; exit 1; }

JP="$ROOT/core/third_party/jpegli/linux-$CPU"
[ -f "$JP/libjpegli-static.a" ] || "$HERE/build-jpegli-linux.sh"

"$ROOT/core/build/apply-sources.sh" "$SRC"

OUTDIR="out/linux-$CPU"
cd "$SRC"
mkdir -p "$OUTDIR"
cat > "$OUTDIR/args.gn" <<ARGS
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
use_sysroot = true
symbol_level = 0
target_os = "linux"
target_cpu = "$CPU"
ARGS
buildtools/linux64/gn gen "$OUTDIR" --root=.
third_party/ninja/ninja -C "$OUTDIR" pdfium

AR="$SRC/$OUTDIR/obj/libpdfium.a"
[ -f "$AR" ] || { echo "ERROR: $AR not produced." >&2; exit 1; }
CXX="$SRC/third_party/llvm-build/Release+Asserts/bin/clang++"
LDFLAGS=""
if command -v "$CXX" >/dev/null 2>&1; then
  [ -x "$SRC/third_party/llvm-build/Release+Asserts/bin/ld.lld" ] && LDFLAGS="-fuse-ld=lld"
else
  CXX=clang++
  command -v ld.lld >/dev/null 2>&1 && LDFLAGS="-fuse-ld=lld"
fi

ENGINE_O="$SRC/$OUTDIR/obj/fpdfsdk/fpdfsdk/fpdf_compress.o"
[ -f "$ENGINE_O" ] || { echo "ERROR: $ENGINE_O not produced." >&2; exit 1; }
NM="$SRC/third_party/llvm-build/Release+Asserts/bin/llvm-nm"
[ -x "$NM" ] || NM=nm
if ! "$NM" "$ENGINE_O" | grep 'T HyperCompress_Execute' >/dev/null; then
  echo "ERROR: engine object does not export HyperCompress_*." >&2
  "$NM" --version >&2 || true
  "$NM" "$ENGINE_O" | grep -i 'hypercompress' >&2 || echo "  (no HyperCompress symbols at all)" >&2
  exit 1
fi

INC="$ROOT/sdk/native/include"
mkdir -p "$INC/public"
rsync -a --delete "$SRC/public/" "$INC/public/"

BUILDOUT="$ROOT/core/build/out"
BINDIR="$ROOT/cli/prebuilt"
mkdir -p "$BUILDOUT" "$BINDIR"

"$CXX" -std=c++17 -O2 -fPIC -fvisibility=hidden -DNDEBUG \
  -I"$JP" -I"$JP/include" \
  -c "$ROOT/core/src/hyper_jpegli_wrap.cc" -o "$BUILDOUT/hyper_jpegli_wrap.o"

for drv in hpdf_worker hpdf_render; do
  [ -f "$ROOT/core/driver/$drv.cpp" ] || continue
  "$CXX" -std=c++17 -O2 -I"$INC" -c "$ROOT/core/driver/$drv.cpp" -o "$BUILDOUT/$drv.o"
  "$CXX" $LDFLAGS -o "$BINDIR/${drv/_/-}" \
    "$BUILDOUT/$drv.o" "$BUILDOUT/hyper_jpegli_wrap.o" \
    -Wl,--whole-archive "$AR" -Wl,--no-whole-archive \
    "$JP/libjpegli-static.a" "$JP/libhwy.a" \
    -lpthread -ldl -lm
  strip "$BINDIR/${drv/_/-}"
done

echo "built: $BINDIR/hpdf-worker $BINDIR/hpdf-render (linux-$CPU)"
