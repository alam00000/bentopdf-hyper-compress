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

## 📜 Licensing

Hyper Compress is **dual-licensed** to fit your needs:

| License | Best For | Price |
| --- | --- | --- |
| **AGPL-3.0** | Open-source projects with public source code | **Free** |
| **Commercial** | Proprietary / closed-source products | **from $99/year** |
| **OEM** | Redistribution, reselling as an SDK, or offering Hyper Compress as a service | **from $1,999/year** |

<p align="center">
  <a href="https://www.bentopdf.com/hyper-compress#pricing">
    <img src="https://img.shields.io/badge/🚀_Get_Commercial_License-from_$99%2Fyear-6366f1?style=for-the-badge&labelColor=1f2937" alt="Get Commercial License">
  </a>
</p>

> **Solo $99** · **Indie $199** · **Business $499** per year · **One closed-source product per license** · **Unlimited developers from Indie up** · **No AGPL obligations**

> **Using BentoPDF?** Its commercial license already includes Hyper Compress as a bundled component for eligible products (companies under US$1 million in revenue and funding, through BentoPDF's own tools on the end user's device). Using the engine on its own, on a server, through an API or in other software needs a Hyper Compress license.

For OEM terms, contact [contact@bentopdf.com](mailto:contact@bentopdf.com). See the [full license terms](COMMERCIAL-LICENSE.md) and the [pricing page](https://www.bentopdf.com/hyper-compress#pricing).

📖 For more details, see the [Licensing Page](https://hyper.bentopdf.com/docs/licensing).

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

* **Signed documents handled safely.** A digitally signed PDF is returned byte for byte by default, so its signature stays valid. Set `removeSignatures` to compress it anyway: Hyper removes the digital signature, keeps the visible signature on the page, compresses the document and reports `signaturesRemoved`, so the file can be signed again.

* **Built for reliability.** Hyper validates its output and verifies transformations that can affect visual fidelity. When a change cannot reproduce a page faithfully, it is rolled back. Across the benchmark, Hyper had a **0.39% corruption rate**, the lowest of any engine measured.

* **One engine, everywhere.** The same compression engine powers the CLI, Node SDK, C API, self-hosted service, and WebAssembly build.


---

## Benchmarks

### Against commercial SDKs: PDF Tools, Apryse and Nutrient

Hyper Compress was benchmarked against the PDF Tools, Apryse and Nutrient SDKs on **1,000 real PDFs (3.13 GB) from five public sources**, at four levels, for a total of **13,000 engine runs**. We make Hyper, so everything needed to check or repeat these numbers is in [`bench/sdk-comparison/`](bench/sdk-comparison/): the exact list of PDFs with download links and checksums, every per-file result, and the code.

Each engine's settings were taken from its own documentation to match each level as closely as its options allow. If you know a setting that would serve an engine better at the same level, open an issue and we will rerun it.

Size saved overall, @ average page quality (SSIMULACRA2, where 100 is identical):

| Engine | Lossless | 200 dpi | 150 dpi | 72 dpi |
| --- | --- | --- | --- | --- |
| **Hyper** | **18.0% @ 99.5** | **61.0% @ 89.8** | **70.2% @ 86.4** | **79.9% @ 67.9** |
| PDF Tools | N/A | 55.1% @ 86.7 | 64.7% @ 82.9 | 75.6% @ 72.2 |
| Apryse | N/A | 41.8% @ 88.4 | 55.6% @ 85.8 | 74.6% @ 73.4 |
| Nutrient | N/A | 55.2% @ 87.8 | 61.5% @ 82.8 | 72.0% @ 70.8 |

Robustness across the same benchmark:

| Engine | Runs | Failed | Grew the file |
| --- | ---: | ---: | ---: |
| **Hyper** | **4,000** | **0** | **0** |
| Nutrient | 3,000 | 6 (0.20%) | 5 (0.2%) |
| PDF Tools | 3,000 | 0 | 64 (2.1%) |
| Apryse | 3,000 | 8 (0.27%) | 120 (4.0%) |

> [!NOTE]
> PDF Tools, Apryse and Nutrient have no lossless mode: none of their documentation describes a lossless optimization setting, so their lossless results are N/A.

<details>
<summary>Engines, machine and corpus</summary>

| Engine | Version | How it was run |
|---|---|---|
| Hyper Compress | `hyper-compress` 1.1.4 (npm) | `hyper in.pdf out.pdf --preset <level>` |
| PDF Tools SDK | `pdftools_sdk` 1.21.0 (Python) | [`engines/pdftools.py`](bench/sdk-comparison/engines/pdftools.py) |
| Apryse SDK | `apryse-sdk` 12.2.1 (Python), PDFNet 12.2.0 | [`engines/apryse.py`](bench/sdk-comparison/engines/apryse.py) |
| Nutrient .NET SDK | `GdPicture.API` 14.4.11.1 (.NET 10) | [`engines/nutrient/Program.cs`](bench/sdk-comparison/engines/nutrient/Program.cs), `GdPicturePDFReducer` |

Machine: MacBook Pro (Apple M4, 16 GB), macOS 26.2. Tools: Node 22.23.2, Python 3.14, .NET 10.0.105, Poppler 26.02.0, qpdf 12.4.2, SSIMULACRA2 from libjxl 0.11.1.

The 1,000 PDFs were drawn with a fixed seed and stratified by size: 200 files each under 100 KB, 100–500 KB, 0.5–2 MB and 2–10 MB, 93 over 10 MB, and 107 more picked at random. The exact list, with the download link, size, page count and SHA-256 of every file, is [`MANIFEST.csv`](bench/sdk-comparison/MANIFEST.csv).

| Source | Files | What it is |
|---|---|---|
| [Common Crawl PDFs](https://digitalcorpora.org/corpora/file-corpora/cc-main-2021-31-pdf-untruncated/) (Digital Corpora) | 489 | PDFs found on the web |
| [GovDocs1](https://digitalcorpora.org/corpora/file-corpora/files/) (Digital Corpora) | 199 | US government documents |
| [PubMed Central open access](https://pmc.ncbi.nlm.nih.gov/tools/openftlist/) | 136 | Medical and science papers |
| [arXiv](https://arxiv.org) | 134 | Preprints |
| [EUR-Lex](https://eur-lex.europa.eu) | 42 | EU law in several languages |

</details>

<details>
<summary>Settings for each engine</summary>

| Level | Hyper | PDF Tools | Apryse | Nutrient |
|---|---|---|---|---|
| Lossless | preset `lossless` | N/A | N/A | N/A |
| 200 dpi | preset `low` (200 dpi, JPEG 80) | `Web` profile, 200 dpi, threshold 300, quality 0.8, `BALANCED` | JPEG quality 8/10, above 300 dpi → 200 | downscale to 200 dpi, `ImageQualityHigh` |
| 150 dpi | preset `medium` (150 dpi, JPEG 50) | `Web`, 150 dpi, threshold 225, quality 0.5 | JPEG quality 5/10, above 225 → 150 | downscale to 150 dpi, `ImageQualityMedium` |
| 72 dpi | preset `high` (72 dpi, JPEG 20, black and white scans 150 dpi) | `Web`, 72 dpi, threshold 108, quality 0.2 | JPEG quality 2/10, above 108 → 72 | downscale to 72 dpi, `ImageQualityLow` |

All engines: fonts subset and merged, metadata kept.

Black and white scans: no engine was set to reuse similar-looking shapes across a page (lossy JBIG2 pattern matching), which can swap one letter or number for another:

- Hyper stores them with lossless JBIG2.
- Apryse uses JBIG2 with threshold 10, which its documentation describes as [lossless where possible](https://docs.apryse.com/api/ios/Classes/PTMonoImageSettings.html).
- Nutrient uses JBIG2 with `JBIG2PMSThreshold` 1.0. Its documentation says [values below 1 can be lossy](https://www.nutrient.io/api/gdpicture/GdPicture.NET.14~GdPicture14.PDFReducerConfiguration~JBIG2PMSThreshold.html).
- PDF Tools: its SDK documents that "the JBIG2 compression quality is fixed at 1 (lossless)" (`ImageRecompressionOptions.compression_quality`).

Resolution of black and white scans still differs by engine: Hyper keeps full resolution, except at 72 dpi where it uses 150 dpi; PDF Tools and Nutrient shrink them to the level's resolution; Apryse leaves their resolution alone.

</details>

<details>
<summary>How it was measured</summary>

- **Size:** output bytes. If an engine returned a bigger file, the bigger file counts. A failed file counts as its original size.
- **Failed:** no output, an error, more than 10 minutes on one file, or a different page count from the original.
- **Time:** wall time per file, 4 files at a time for every engine. For Hyper, PDF Tools and Apryse this includes starting the process (Node or Python and SDK initialisation). Nutrient's .NET runtime takes about 9 seconds to start, so it ran as 4 long-running processes and its times exclude start-up.
- **Quality:** pages 1 and 2 of every output are drawn at 100 dpi with Poppler's `pdftoppm` and compared with the original pages using [SSIMULACRA2](https://github.com/cloudinary/ssimulacra2). 100 means identical, 90 and up no visible difference, under 70 visible on close look, under 50 clearly damaged.

Known limitations:

- Quality is scored on the first two pages of each file, drawn by one renderer (Poppler).
- Times depend on the machine. Compare engines with each other, not with numbers from other hardware.

</details>

<details>
<summary>Full results by level and by file size</summary>

#### Lossless

| Engine | Output | Saved overall | Median saving per file | Failed | Files made bigger | Total time | Median time per file | Avg page quality (SSIMULACRA2) | Pages < 50 | Pages < 70 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Hyper** | **2.56 GB** | **18.0%** | **20.6%** | **0** | **0** | **8.0 min** | **0.15 s** | **99.5** | **0.1%** | **0.5%** |
| PDF Tools | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| Apryse | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| Nutrient | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |


#### Low (200 dpi)

| Engine | Output | Saved overall | Median saving per file | Failed | Files made bigger | Total time | Median time per file | Avg page quality (SSIMULACRA2) | Pages < 50 | Pages < 70 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Hyper** | **1.22 GB** | **61.0%** | **44.5%** | **0** | **0** | **14.5 min** | **0.21 s** | **89.8** | **2.1%** | **10.5%** |
| PDF Tools | 1.40 GB | 55.1% | 41.4% | 0 | 26 | 20.5 min | 0.81 s | 86.7 | 5.6% | 16.6% |
| Apryse | 1.82 GB | 41.8% | 29.4% | 1 | 46 | 43.6 min | 2.45 s | 88.4 | 2.7% | 9.3% |
| Nutrient | 1.40 GB | 55.2% | 42.0% | 2 | 2 | 34.2 min | 0.18 s | 87.8 | 4.4% | 12.3% |

- Hyper vs PDF Tools: Hyper smaller on **704** files, PDF Tools smaller on 250, within 1% on 46 (of 1000 both handled).
- Hyper vs Apryse: Hyper smaller on **814** files, Apryse smaller on 157, within 1% on 28 (of 999 both handled).
- Hyper vs Nutrient: Hyper smaller on **543** files, Nutrient smaller on 382, within 1% on 73 (of 998 both handled).

#### Medium (150 dpi)

| Engine | Output | Saved overall | Median saving per file | Failed | Files made bigger | Total time | Median time per file | Avg page quality (SSIMULACRA2) | Pages < 50 | Pages < 70 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Hyper** | **0.93 GB** | **70.2%** | **54.5%** | **0** | **0** | **17.0 min** | **0.25 s** | **86.4** | **4.4%** | **18.1%** |
| PDF Tools | 1.10 GB | 64.7% | 50.1% | 0 | 20 | 19.7 min | 0.81 s | 82.9 | 8.6% | 21.4% |
| Apryse | 1.39 GB | 55.6% | 37.6% | 4 | 42 | 44.1 min | 2.47 s | 85.8 | 4.2% | 14.0% |
| Nutrient | 1.20 GB | 61.5% | 47.5% | 2 | 2 | 36.7 min | 0.21 s | 82.8 | 8.1% | 19.1% |

- Hyper vs PDF Tools: Hyper smaller on **701** files, PDF Tools smaller on 253, within 1% on 46 (of 1000 both handled).
- Hyper vs Apryse: Hyper smaller on **808** files, Apryse smaller on 164, within 1% on 24 (of 996 both handled).
- Hyper vs Nutrient: Hyper smaller on **585** files, Nutrient smaller on 357, within 1% on 56 (of 998 both handled).

#### Highest (72 dpi)

| Engine | Output | Saved overall | Median saving per file | Failed | Files made bigger | Total time | Median time per file | Avg page quality (SSIMULACRA2) | Pages < 50 | Pages < 70 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Hyper** | **0.63 GB** | **79.9%** | **67.5%** | **0** | **0** | **20.7 min** | **0.33 s** | **67.9** | **29.2%** | **45.0%** |
| PDF Tools | 0.76 GB | 75.6% | 64.8% | 0 | 18 | 18.9 min | 0.81 s | 72.2 | 23.1% | 38.0% |
| Apryse | 0.79 GB | 74.6% | 58.9% | 3 | 32 | 44.0 min | 2.45 s | 73.4 | 20.2% | 35.3% |
| Nutrient | 0.87 GB | 72.0% | 60.1% | 2 | 1 | 35.6 min | 0.17 s | 70.8 | 24.6% | 38.3% |

- Hyper vs PDF Tools: Hyper smaller on **729** files, PDF Tools smaller on 233, within 1% on 38 (of 1000 both handled).
- Hyper vs Apryse: Hyper smaller on **790** files, Apryse smaller on 175, within 1% on 32 (of 997 both handled).
- Hyper vs Nutrient: Hyper smaller on **634** files, Nutrient smaller on 309, within 1% on 55 (of 998 both handled).

#### Saved overall by input size

| Level | Engine | < 100 KB | 100–500 KB | 0.5–2 MB | 2–10 MB | > 10 MB |
|---|---|---|---|---|---|---|
| **Lossless** | **Hyper** | **27.5%** | **33.5%** | **28.7%** | **19.9%** | **15.3%** |
| Lossless | PDF Tools | N/A | N/A | N/A | N/A | N/A |
| Lossless | Apryse | N/A | N/A | N/A | N/A | N/A |
| Lossless | Nutrient | N/A | N/A | N/A | N/A | N/A |
| **Low** | **Hyper** | **33.7%** | **44.6%** | **49.3%** | **58.2%** | **64.3%** |
| Low | PDF Tools | 28.7% | 43.1% | 47.9% | 49.0% | 59.3% |
| Low | Apryse | 22.8% | 34.8% | 33.3% | 39.5% | 44.3% |
| Low | Nutrient | 33.1% | 43.1% | 44.3% | 54.1% | 57.5% |
| **Medium** | **Hyper** | **35.3%** | **48.8%** | **57.2%** | **67.6%** | **73.8%** |
| Medium | PDF Tools | 32.1% | 48.2% | 56.4% | 59.1% | 68.9% |
| Medium | Apryse | 23.1% | 37.6% | 42.7% | 54.3% | 58.4% |
| Medium | Nutrient | 35.5% | 47.3% | 50.4% | 61.3% | 63.6% |
| **Highest** | **Hyper** | **44.1%** | **58.8%** | **68.3%** | **77.5%** | **83.1%** |
| Highest | PDF Tools | 36.6% | 56.6% | 67.4% | 69.4% | 80.1% |
| Highest | Apryse | 30.9% | 50.9% | 61.3% | 72.7% | 78.0% |
| Highest | Nutrient | 40.0% | 54.6% | 61.0% | 71.4% | 74.4% |

</details>

<details>
<summary>Reproduce</summary>

Requirements: macOS or Linux, Node 22, Python 3.11 or later, .NET 10, Poppler (`pdftoppm`), qpdf, `ssimulacra2` (from libjxl), `bc` and `perl`. From [`bench/sdk-comparison/`](bench/sdk-comparison/):

```sh
python3 fetch_corpus.py                        # downloads the 1,000 PDFs and checks every SHA-256
npm install                                    # hyper-compress 1.1.4
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt --extra-index-url https://pypi.apryse.com
dotnet build engines/nutrient -c Release -o engines/nutrient/bin
export APRYSE_KEY=... PDFTOOLS_KEY=... NUTRIENT_KEY=...   # your licence keys
./run.sh                                       # writes results.csv (takes several hours)
python3 report.py results.csv                  # prints the tables above
```

`./run.sh hyper` runs one engine. `python3 report.py` with no argument prints the tables from our own [`RESULTS.csv`](bench/sdk-comparison/RESULTS.csv). Expect sizes to match ours to within a few bytes; PDF Tools and Apryse write the current date and a new document ID into each file.

| File | What it is |
|---|---|
| [`MANIFEST.csv`](bench/sdk-comparison/MANIFEST.csv) | The 1,000 PDFs: file name, source, bytes, pages, SHA-256, download link |
| [`RESULTS.csv`](bench/sdk-comparison/RESULTS.csv) | Every result: engine, level, file, input and output bytes, pass or fail, exit code, seconds, quality of pages 1 and 2 |
| [`fetch_corpus.py`](bench/sdk-comparison/fetch_corpus.py) | Downloads the corpus and checks it against `MANIFEST.csv` |
| [`run.sh`](bench/sdk-comparison/run.sh), [`job.sh`](bench/sdk-comparison/job.sh) | Run the engines and score each output |
| [`engines/`](bench/sdk-comparison/engines) | The exact settings used for PDF Tools, Apryse and Nutrient |
| [`report.py`](bench/sdk-comparison/report.py) | Builds the tables from a results file |

</details>

### Against open-source tools: Ghostscript, MuPDF and qpdf

Hyper Compress was benchmarked against Ghostscript, MuPDF, and qpdf using **2,104 real world PDFs from four public corpora**, for a total of **27,352 engine runs**.

Visual fidelity was measured with Poppler, which was deliberately chosen as an independent renderer rather than one shared by the engines being tested.

The full methodology, per-file results, and complete document list are available in [comparison.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/comparison.md).

Median size reduction at the median worst page SSIM:

| Engine      | Lossless          | 300 dpi           | 150 dpi           | 72 dpi            |
| ----------- | ----------------- | ----------------- | ----------------- | ----------------- |
| **Hyper**   | **22.4% @ 1.000** | **52.3% @ 1.000** | **58.2% @ 0.999** | **64.8% @ 0.996** |
| Ghostscript | 21.1% @ 0.997     | 19.1% @ 0.996     | 42.4% @ 0.994     | 53.9% @ 0.990     |
| MuPDF       | 4.7% @ 1.000      | 14.0% @ 1.000     | 19.4% @ 1.000     | 28.0% @ 0.999     |
| qpdf        | 6.3% @ 1.000      | n/a               | n/a               | n/a               |

Robustness across the same benchmark:

| Engine      |  Runs |        Corrupt | Grew the file |
| ----------- | ----: | -------------: | ------------: |
| **Hyper**   | **8,416** | **33 (0.39%)** | **0** |
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
hyper input.pdf                                     # writes input-compressed.pdf

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

Fuzzing runs through ClusterFuzzLite on every pull request that touches the engine, and again nightly for a longer session. Discovered crashes are pinned as regression seeds.

---

## Licensing

Hyper Compress is licensed under the [GNU AGPL v3](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/LICENSE).

If you need to use Hyper Compress in a proprietary or closed-source product, [commercial licenses](https://www.bentopdf.com/hyper-compress#pricing) are available as annual subscriptions from $99/year, under the [Hyper Compress Commercial License](COMMERCIAL-LICENSE.md). For OEM terms, contact us at [contact@bentopdf.com](mailto:contact@bentopdf.com).

Bundled third-party components retain their own licenses. See [NOTICE.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/NOTICE.md) for the full list.

---

## Contributing

Contributions are welcome.

Before opening your first pull request, there are two things to know:

1. Contributors need to sign the [Contributor License Agreement](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/ICLA.md). The bot will prompt you automatically.
2. Bug fixes should include the PDF that reproduces the issue so it can be added to the regression suite.

See [CONTRIBUTING.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/CONTRIBUTING.md) for the full contribution guide.

For security vulnerabilities, please see [SECURITY.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/SECURITY.md).
