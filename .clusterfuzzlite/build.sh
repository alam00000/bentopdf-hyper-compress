#!/bin/bash -eu

PIN=162c9521f74c17bb0c8595608f23ab22cec3d407
ENGINE=$SRC/hyper-compress-engine
CHECKOUT=$SRC/pdfium-checkout
PDFIUM=$CHECKOUT/pdfium

mkdir -p "$CHECKOUT"
cd "$CHECKOUT"
if [ ! -d pdfium ]; then
  gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
  gclient sync --revision "pdfium@$PIN" -D --no-history
fi

"$ENGINE/core/build/apply-sources.sh" "$PDFIUM"

cd "$PDFIUM"
OUTDIR=out/ossfuzz
mkdir -p "$OUTDIR"
GN_SAN="is_asan = true"
case "${SANITIZER:-address}" in
  undefined) GN_SAN="is_ubsan_security = true" ;;
  coverage)  GN_SAN="" ;;
esac
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
use_libfuzzer = true
use_glib = false
use_sysroot = true
symbol_level = 1
target_os = "linux"
target_cpu = "x64"
$GN_SAN
ARGS
buildtools/linux64/gn gen "$OUTDIR" --root=.
third_party/ninja/ninja -C "$OUTDIR" pdfium buildtools/third_party/libc++

mkdir -p "$WORK/libcxx"
for o in "$OUTDIR"/obj/buildtools/third_party/libc++/libc++/*.o; do
  llvm-objcopy --weaken "$o" "$WORK/libcxx/$(basename "$o")"
done

$CXX $CXXFLAGS -std=c++17 -I"$PDFIUM" \
  -c "$ENGINE/fuzz/jpegli_stub.cc" -o "$WORK/jpegli_stub.o"
$CXX $CXXFLAGS -std=c++17 -I"$PDFIUM" \
  -c "$ENGINE/fuzz/fuzz_compress.cc" -o "$WORK/fuzz_compress.o"
$CXX $CXXFLAGS -fuse-ld=lld "$WORK/fuzz_compress.o" "$WORK/jpegli_stub.o" \
  -Wl,--whole-archive "$OUTDIR/obj/libpdfium.a" -Wl,--no-whole-archive \
  "$WORK"/libcxx/*.o $LIB_FUZZING_ENGINE \
  -o "$OUT/fuzz_compress"

cd "$ENGINE"
zip -j "$OUT/fuzz_compress_seed_corpus.zip" fuzz/corpus/*
