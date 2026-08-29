<p align="center"><img src="https://raw.githubusercontent.com/alam00000/bentopdf-hyper-compress/main/web/images/logo.svg" width="80"></p>

<h1 align="center">Hyper Compress</h1>

<p align="center">
  <strong>The best open source PDF compression engine.</strong>
</p>

Hyper Compress is a high fidelity, content preserving PDF compression engine built to make PDFs smaller without unnecessarily changing the document.

Hyper Compress is built by the [BentoPDF](https://github.com/alam00000/bentopdf) team. The same engine is available as a CLI, Node SDK, C library, self hosted service, and a WebAssembly build that runs entirely in the browser.

Try it at [hyper.bentopdf.com](https://hyper.bentopdf.com). Full documentation lives at [hyper.bentopdf.com/docs](https://hyper.bentopdf.com/docs/).

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/LICENSE) ![GitHub Stars](https://img.shields.io/github/stars/alam00000/bentopdf-hyper-compress?style=social)

---

## Table of Contents

* [Why Hyper Compress](#why-hyper-compress)
* [Benchmarks](#benchmarks)
* [Install](#install)
* [Usage](#usage)

  * [CLI](#cli)
  * [Node SDK](#node-sdk)
  * [Self-hosted service](#self-hosted-service)
  * [Browser (WebAssembly)](#browser-webassembly)
  * [C API](#c-api)
* [Compression Levels](#compression-levels)
* [What the Engine Does](#what-the-engine-does)
* [What the Engine Refuses to Do](#what-the-engine-refuses-to-do)
* [Building from Source](#building-from-source)
* [Testing](#testing)
* [Project Layout](#project-layout)
* [Licensing](#licensing)
* [Contributing](#contributing)

---

## Why Hyper Compress

PDF compression has three major problems: **files ending up being larger after compression, sacrificing document content for size, and producing PDFs that are broken and non conforming.**


Hyper Compress is built around solving these three problems.

* **Monotonic compression.** Hyper never returns a larger file. If compression does not save space, the original PDF is returned unchanged. 

* **Content preserving compression.** Hyper includes a true lossless mode for compression without image re encoding or quality loss. For more aggressive compression, it preserves searchable text and document structure while making only the changes required by the selected compression level all while producing a conforming PDF.

* **Built for reliability.** Hyper validates its output and verifies transformations that can affect visual fidelity. When a change cannot reproduce a page faithfully, it is rolled back. Across the benchmark, Hyper had a **0.39% corruption rate**, the lowest of any engine measured.

* **One engine, everywhere.** The same compression engine powers the CLI, Node SDK, C API, self-hosted service, and WebAssembly build.


---

## Benchmarks

Hyper Compress was benchmarked against Ghostscript, MuPDF, and qpdf using **2,104 real world PDFs from four public corpora**, for a total of **27,352 engine runs**.

Visual fidelity was measured with Poppler, which was deliberately chosen as an independent renderer rather than one shared by the engines being tested.

The full methodology, per-file results, and complete document list are available in [comparison.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/comparison.md).

Median size reduction at the median worst page SSIM:

| Tier     | Hyper             | Ghostscript   | MuPDF         | qpdf         |
| -------- | ----------------- | ------------- | ------------- | ------------ |
| Lossless | **22.4% @ 1.000** | 21.1% @ 0.997 | 4.7% @ 1.000  | 6.3% @ 1.000 |
| 300 dpi  | **52.3% @ 1.000** | 19.1% @ 0.996 | 14.0% @ 1.000 | n/a          |
| 150 dpi  | **58.2% @ 0.999** | 42.4% @ 0.994 | 19.4% @ 1.000 | n/a          |
| 72 dpi   | **64.8% @ 0.996** | 53.9% @ 0.990 | 28.0% @ 0.999 | n/a          |

Robustness across the same benchmark:

| Engine      |  Runs |        Corrupt | Grew the file |
| ----------- | ----: | -------------: | ------------: |
| **Hyper**   | 8,416 | **33 (0.39%)** |         **0** |
| qpdf        | 2,104 |      1 (0.05%) |     53 (2.5%) |
| Ghostscript | 8,416 |    256 (3.04%) | 2,089 (24.8%) |
| MuPDF       | 8,416 |    333 (3.96%) |     70 (0.8%) |

> [!NOTE]
> The corpus selection was pre registered using a fixed seed before any files were fetched. Every document's SHA-256 hash and origin URL are recorded, so the benchmark can be independently reproduced.
>
> You can also run the harness against your own documents:
>
> `make bench CORPUS=path/to/corpus`

---

## Install

Prebuilt binaries for macOS, Linux, and Windows are attached to each [release](https://github.com/alam00000/bentopdf-hyper-compress/releases).

Download the archive for your platform, verify it against `SHA256SUMS`, and unpack it into `cli/prebuilt/`.

For the self-hosted service:

```bash
docker run -d -p 8080:8080 ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

To build the engine yourself, see [BUILDING.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/BUILDING.md).

---

## Usage

### CLI

```bash
hyper input.pdf output.pdf                          # medium preset (default)

hyper input.pdf output.pdf --preset high            # smallest output

hyper scan.pdf out.pdf --target-size 2MB            # specify a target size

hyper locked.pdf out.pdf --password hunter2         # encrypted input

hyper in.pdf out.pdf --set grayscale=true           # override an option

hyper --help                                        # every flag, with examples
```

`--target-size` searches for the highest image quality that fits under your target, pushing down to quality 5 and 36 dpi if it has to. If the target is reachable you get the best looking file that fits; if it is not, you get the smallest file the engine could produce along with a warning saying how small that was.

### Node SDK

```js
import { compress } from 'hyper-compress';

const r = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'medium',
});

console.log(r.originalSize, '->', r.compressedSize);
```

The result includes the original and compressed sizes, whether the document was signed, its declared PDF/A level, and whether a requested target size was reached.

Errors are typed:

`decrypt_failed`, `engine_error`, `timeout`, `cancelled`.

Parsing runs in a worker subprocess with a timeout. If hostile or malformed input causes the worker to fail, it is isolated from your main process.

### Self-hosted service

The Docker image includes a web interface at `http://localhost:8080` and an HTTP API.

Your files are processed on your own server.

```bash
curl -o out.pdf --data-binary @in.pdf \
  'http://localhost:8080/api/compress?preset=high'
```

See [SELF-HOSTING.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/SELF-HOSTING.md) for the complete API, configuration options, and deployment notes.

### Browser (WebAssembly)

The `web/` directory contains a complete browser build.

The engine is compiled to WebAssembly and runs locally in the browser. Files are not uploaded, and the build makes is air gapped by default.

The same WebAssembly build is on npm as [`hyper-compress-wasm`](https://www.npmjs.com/package/hyper-compress-wasm), a buffer-based SDK for platforms the native package does not cover:

```js
import { compress } from 'hyper-compress-wasm';

const result = await compress(pdfBytes, { preset: 'medium' });
```

```bash
node wasm/verify.mjs some.pdf
```

This verifies that the WebAssembly output is byte-identical to the native build.

### C API

`sdk/native` exposes a small C ABI:

* `hpdf_compress_file`
* `hpdf_compress_buffer`
* `hpdf_last_error`

The shared library exports only these symbols, and the ABI is additive only within a major version.

---

## Compression Levels

| Preset     | Image quality | Max DPI | Use for                                    |
| ---------- | ------------: | ------: | ------------------------------------------ |
| `low`      |            80 |     200 | Archival copies and minimal visible change |
| `medium`   |            50 |     150 | The default; good for sharing and email    |
| `high`     |            20 |      72 | Smallest output and screen reading         |
| `lossless` |           n/a |    none | No image re-encoding; pixel-identical      |

Every option behind these presets is also available individually.

You can create custom profiles using image quality and resolution, grayscale conversion, colour reduction, font subsetting and unembedding, metadata and annotation stripping, PDF 2.0 Brotli streams, and page rasterisation for image-only documents.

---

## What the Engine Does

Depending on the document and selected options, the engine can:

* re encode images with jpegli
* downsample images above a configured resolution ceiling
* choose between JPEG, JPEG 2000, Flate, CCITT, and JBIG2 on a per image basis
* convert Type 1 fonts to CFF
* subset embedded fonts to the glyphs actually used
* merge duplicate font programs
* unembed fonts that are metric-compatible clones of the standard 14 fonts
* regenerate content streams with correct handling for CMYK, CalRGB, Lab, ICCBased, Separation, DeviceN, Indexed, and pattern fills
* deduplicate identical objects and prune unused resources
* flatten ICC profiles
* remove selected metadata
* optionally apply PDF 2.0 Brotli stream compression
* optionally rasterise pages when the document is genuinely image-only

The engine applies multiple techniques mentioned above to achieve best possible compression.

---

## Building from Source

The JavaScript parts require Node 22:

```bash
npm ci

make check
```

The native engine builds against a pinned PDFium checkout. The initial bootstrap is several gigabytes.

See [BUILDING.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/BUILDING.md) for complete instructions, the patch inventory, and platform-specific notes.

---

## Testing

```bash
make check                      # typecheck, lint, unit and integration tests

make regress                    # regression failset, every preset

make bench CORPUS=path/to/dir   # benchmark harness from comparison.md

make fuzz                       # libFuzzer harness
```

`make regress` is the gate for bug fixes.

Every PDF in `tests/regression/failset/` represents a file that previously exposed a bug. The suite checks validity, pixel fidelity, and text-layer survival across every relevant preset.

A clean run reports zero failing pairs.

Fuzzing runs through ClusterFuzzLite on every pull request that touches the engine, and discovered crashes are pinned as regression seeds.

---

## Licensing

Hyper Compress is licensed under the [GNU AGPL v3](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/LICENSE).

If you need to use Hyper Compress in a proprietary or closed-source product, a commercial license is available. Contact us at [contact@bentopdf.com](mailto:contact@bentopdf.com).

Bundled third-party components retain their own licenses. See [NOTICE.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/NOTICE.md) for the full list.

---

## Contributing

Contributions are welcome.

Before opening your first pull request, there are two things to know:

1. Contributors need to sign the [Contributor License Agreement](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/ICLA.md). The bot will prompt you automatically.
2. Bug fixes should include the PDF that reproduces the issue so it can be added to the regression suite.

See [CONTRIBUTING.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/CONTRIBUTING.md) for the full contribution guide.

For security vulnerabilities, please see [SECURITY.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/SECURITY.md).
