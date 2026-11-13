# Third-party notices

This product bundles the following components, each under its own license.
Full license texts ship inside the component trees.

| Component | License | Location |
|---|---|---|
| PDFium and its bundled dependencies (zlib, libjpeg-turbo, libopenjpeg, lcms2, freetype, libpng, libtiff, agg, abseil, harfbuzz, ICU) | BSD-3-Clause and component licenses | pinned checkout under `core/build/gsrc/pdfium` |
| Brotli | MIT | via the PDFium checkout, `third_party/brotli` |
| qpdf | Apache-2.0 | `cli/prebuilt/qpdf`; source pinned and sha256-verified by `cli/build-qpdf.sh` |
| jpegli / libjxl | BSD-3-Clause | `core/third_party/jpegli` |
| Highway | Apache-2.0 | `core/third_party/jpegli/libhwy.a`, `wasm/prebuilt/libhwy.a` |
| Leptonica | Leptonica License (BSD-style) | `core/third_party/leptonica` |
| jbig2enc | Apache-2.0 | `core/third_party/jbig2enc` |
| AFDKO (subset) | Apache-2.0 | `core/third_party/afdko` |
| DM Sans | SIL Open Font License 1.1 | `web/fonts` |
| Rust crates linked into the Node.js addon, the `hyper` CLI and `hyper-server` (napi, serde, serde_json, base64, tempfile, tiny_http and their dependencies; the full list is `rust/Cargo.lock`) | MIT, Apache-2.0, ISC, Unlicense or Unicode-3.0, each crate under its own terms | crates.io, pinned by `rust/Cargo.lock` |

The engine sources under `core/src` and the rest of this repository are
licensed under the GNU Affero General Public License v3.0; see LICENSE.
