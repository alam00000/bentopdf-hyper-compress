# Building

## The npm parts (SDK, CLI, tests)

Node 22 and nothing else:

```bash
npm ci
make check        # typecheck, lint, unit tests, version lock
```

Without built native workers the integration tests skip and everything else
still runs, so this works on a bare checkout.

Two TypeScript compilers are installed on purpose: the build compiles with
TypeScript 7 through the `tsc7` npm alias (`node node_modules/tsc7/bin/tsc`,
wired into the `build` and `typecheck` scripts), while `typescript` stays on 6
because `typescript-eslint` does not accept 7 yet. Keep both until
`typescript-eslint` supports TypeScript 7, then collapse to one and drop the
alias.

## The native engine

The engine compiles into a pinned Google PDFium checkout as extra translation
units in the `fpdfsdk` source set: `fpdf_compress.cpp` (the engine),
`hyper_type1_wrap.cc` and `hyper_jbig2_wrap.cc` (Type 1 and JBIG2 helpers),
and `hyper_generic_cmyk_icc.h` (embedded ICC data), with deps on the vendored
`core/third_party/{afdko,jbig2enc,leptonica}`. `hyper_jpegli_wrap.cc` is not
in the source set; it uses jpegli's native headers and is compiled and linked
separately against `core/third_party/jpegli/{libjpegli-static.a,libhwy.a}`.

The pin lives in `core/build/apply-tree.sh` as `PDFIUM_PIN`
(currently `162c9521f74c17bb0c8595608f23ab22cec3d407`).

1. Bootstrap the checkout once. It is gitignored and around 10 GB with
   dependencies. You need Google's depot_tools on PATH.

   ```bash
   mkdir -p core/build/gsrc && cd core/build/gsrc
   gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
   gclient sync --revision pdfium@<PDFIUM_PIN> -D
   ```

2. `core/build/apply-tree.sh` does the rest: resets the tree's patched files,
   copies the full-file patches from `core/patches/` on top, copies the engine
   sources (`core/src/` into `fpdfsdk/`, `core/include/fpdf_compress.h` into
   `public/`) and the vendored third-party libraries into the tree, writes a
   non-V8 `args.gn`, runs `gn gen` + `ninja pdfium`, verifies the
   `HyperCompress_*` exports and stages the archive into `core/prebuilt/` and
   `sdk/native/lib/`. A compressor needs no JS engine, so the build is
   non-V8 (`pdf_enable_v8=false`), which roughly quarters the archive size.

3. `core/build/build-native.sh` compiles the jpegli shim and links the worker
   binaries (`cli/prebuilt/hpdf-worker`, `hpdf-render`) against the archive.
   `sdk/native/build-dylib.sh` builds `libhypercompress.dylib`, exporting only
   the `hpdf_*` C API via `sdk/native/hpdf.syms`.

If gn's `exec_script` fails with a pyexpat error, your `python3` is broken;
point PATH at a working one (on macOS, Xcode's `/usr/bin/python3`).

### Relink only

If the archive is current and only the driver or jpegli shim changed:

```bash
core/build/build-native.sh
```

### What's in core/patches/

Full-file copies authored against the pinned revision:

- `core/fxcodec/flate/flatemodule.cpp`: Flate encode at zlib level 9.
- `core/fxcodec/fax/*`, `core/fxcodec/jpeg/*`: un-gate FaxEncode/JpegEncode
  from Windows-only, add quality/subsample/huffman/progressive JPEG options.
- `core/fpdfapi/edit/cpdf_creator.cpp`: incremental-save fixes for the
  signed-document passthrough.
- `core/fpdfapi/edit/cpdf_pagecontentgenerator.*`, `cpdf_pagecontentmanager.*`,
  `core/fpdfapi/page/cpdf_pageobjectholder.*`, `cpdf_form.*`; single
  content-stream regeneration, the resource-pruning soundness guard, and a
  null-resource-dict crash guard.
- `core/fpdfapi/page/cpdf_color.*`, `cpdf_colorspace.h`, `cpdf_basedcs.h`,
  `cpdf_pattern.h`; accessors for colour re-emission. Upstream's content
  writer only expresses DeviceRGB/Gray; the generator patch re-emits
  DeviceCMYK, patterns, and every array-backed colourspace (CalRGB, CalGray,
  Lab, ICCBased, Separation, DeviceN, Indexed) during regeneration.
- `core/fpdfapi/page/cpdf_image.*`: in-place image stream replacement.
- `fpdfsdk/BUILD.gn`: folds the engine and vendored libraries into the
  `fpdfsdk` source set behind `hyper_enable_compress`.

### Bumping the pin

Edit `PDFIUM_PIN` in `apply-tree.sh`, `gclient sync --revision pdfium@<new>`
in `core/build/gsrc`, re-run `apply-tree.sh`, and re-check every file in
`core/patches/` against the new upstream; they are full-file copies, so
upstream drift in those files must be re-merged by hand.

### Header staging

`sdk/native/include/public/` must track the same tree as the archive;
`apply-tree.sh` stages it. A stale header set fails the driver relink, or
worse, silently pins an old API surface.

## WebAssembly

Needs Emscripten and the same pinned checkout:

```bash
make wasm     # wasm/build-pdfium-wasm.sh + wasm/build-wasm.sh
```

`wasm/scripts/patch_pdfium_tree.py` prepares the checkout for the emscripten
toolchain (anchored, idempotent, reversible with `--revert`, so the checkout
keeps serving native builds). `node wasm/verify.mjs some.pdf` asserts byte and
pixel parity between the wasm and native builds.

## Release binaries

Prebuilt workers are not committed. Releases ship them as artifacts with
checksums in `BINARIES.sha256`; `make manifest` regenerates the manifest and
`make harden` strips and signs the binaries. The docker image
(`docker/Dockerfile`) runs the full native bootstrap and build in a container
and is the reference for the Linux build; see `.github/workflows/release.yml`
for the per-platform release builds.

## Other platforms

The checked build recipe is macOS arm64. Linux x64 builds the same way with
`target_os="linux"` in `args.gn`; `docker/Dockerfile` and
`.clusterfuzzlite/build.sh` both script it end to end. Windows needs a PDFium
Windows build with the same source-set additions plus the platform link step;
the leptonica and afdko overlays already carry the clang-cl fixes.
