---
title: Benchmarks
description: How Hyper Compress compares with commercial SDKs and open source tools on real PDFs, and how to rerun every test yourself.
---

# Benchmarks

Hyper has two public benchmarks: one against the commercial SDKs from PDF Tools, Apryse and Nutrient, and one against the open source tools Ghostscript, MuPDF and qpdf. Every file, result and script for both is in the repository, so you can rerun them.

## Against commercial SDKs

Hyper was benchmarked against the PDF Tools, Apryse and Nutrient SDKs on 1,000 real PDFs (3.13 GB) from five public sources, at four levels, for a total of 13,000 engine runs. Each engine's settings were taken from its own documentation to match each level as closely as its options allow.

Size saved overall, at average page quality (SSIMULACRA2, where 100 is identical and 90 and up shows no visible difference):

| Engine | Lossless | 200 dpi | 150 dpi | 72 dpi |
|---|---|---|---|---|
| **Hyper** | **18.0% @ 99.5** | **61.0% @ 89.8** | **70.2% @ 86.4** | **79.9% @ 67.9** |
| PDF Tools | N/A | 55.1% @ 86.7 | 64.7% @ 82.9 | 75.6% @ 72.2 |
| Apryse | N/A | 41.8% @ 88.4 | 55.6% @ 85.8 | 74.6% @ 73.4 |
| Nutrient | N/A | 55.2% @ 87.8 | 61.5% @ 82.8 | 72.0% @ 70.8 |

None of the three documents a lossless mode, so their lossless results are N/A.

| Engine | Runs | Failed | Grew the file |
|---|---:|---:|---:|
| **Hyper** | **4,000** | **0** | **0** |
| Nutrient | 3,000 | 6 (0.20%) | 5 (0.2%) |
| PDF Tools | 3,000 | 0 | 64 (2.1%) |
| Apryse | 3,000 | 8 (0.27%) | 120 (4.0%) |

Median time per file, 4 files at a time on an Apple M4 laptop with 16 GB of memory, including process start for Hyper, PDF Tools and Apryse:

| Engine | Lossless | 200 dpi | 150 dpi | 72 dpi |
|---|---|---|---|---|
| Hyper | 0.15 s | 0.21 s | 0.25 s | 0.33 s |
| PDF Tools | N/A | 0.81 s | 0.81 s | 0.81 s |
| Apryse | N/A | 2.45 s | 2.47 s | 2.45 s |
| Nutrient | N/A | 0.18 s | 0.21 s | 0.17 s |

Nutrient's .NET runtime takes about 9 seconds to start, so it ran as 4 long running processes and its times leave start-up out.

The engine versions, settings for each level, the corpus, how quality was measured, results by file size and the code are in the [repository README](https://github.com/alam00000/bentopdf-hyper-compress#benchmarks) and [`bench/sdk-comparison/`](https://github.com/alam00000/bentopdf-hyper-compress/tree/main/bench/sdk-comparison), including [`MANIFEST.csv`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/bench/sdk-comparison/MANIFEST.csv) (every PDF with its download link and SHA-256) and [`RESULTS.csv`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/bench/sdk-comparison/RESULTS.csv) (every run).

## Against open source tools

Hyper Compress was benchmarked against Ghostscript, MuPDF and qpdf on 2,104 real world PDFs from four public corpora, for a total of 27,352 engine runs. The corpus selection was pre-registered with a fixed seed before any files were fetched, and every document's SHA-256 hash and origin URL are recorded, so the benchmark can be reproduced independently.

Visual fidelity was measured with Poppler, deliberately chosen as a renderer none of the tested engines share.

### Size reduction

Median size reduction at the median worst-page SSIM:

| Tier | Hyper | Ghostscript | MuPDF | qpdf |
|---|---|---|---|---|
| Lossless | **22.4% @ 1.000** | 21.1% @ 0.997 | 4.7% @ 1.000 | 6.3% @ 1.000 |
| 300 dpi | **52.3% @ 1.000** | 19.1% @ 0.996 | 14.0% @ 1.000 | n/a |
| 150 dpi | **58.2% @ 0.999** | 42.4% @ 0.994 | 19.4% @ 1.000 | n/a |
| 72 dpi | **64.8% @ 0.996** | 53.9% @ 0.990 | 28.0% @ 0.999 | n/a |

### Robustness

| Engine | Runs | Corrupt | Grew the file |
|---|---:|---:|---:|
| **Hyper** | 8,416 | **33 (0.39%)** | **0** |
| qpdf | 2,104 | 1 (0.05%) | 53 (2.5%) |
| Ghostscript | 8,416 | 256 (3.04%) | 2,089 (24.8%) |
| MuPDF | 8,416 | 333 (3.96%) | 70 (0.8%) |

Hyper had the lowest corruption rate of any engine that actually compresses images (qpdf's near-zero rate reflects that it only repacks streams, which is also why it saves 6 percent instead of 58), and it is the only engine that never returned a file larger than the input.

### Reproduce it

The full methodology, per-file results and the complete document list live in [comparison.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/comparison.md) in the repository, alongside `bench/MANIFEST.csv` and `bench/RESULTS.csv`. To run the harness against your own documents:

```bash
make bench CORPUS=path/to/corpus
```

## Next steps

- [Guarantees](/guarantees): the promises behind the robustness numbers.
- [Choose the right settings](/guides/choosing-settings): which preset matches each benchmark level.
- [Running in production](/guides/production): speed and sizing on your own hardware.
