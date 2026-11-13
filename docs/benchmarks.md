---
title: Benchmarks
description: How Hyper Compress compares with commercial SDKs and open source tools on real PDFs, and how to rerun every test yourself.
---

# Benchmarks

Hyper has three public benchmarks: one against the commercial SDKs from PDF Tools, Apryse and Nutrient, one against the open source tools Ghostscript, MuPDF and qpdf, and one of [throughput](#throughput): how many PDFs per hour it compresses as a service. Every file, result and script is in the repository, so you can rerun them.

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

## Throughput

How many PDFs per hour Hyper compresses when it runs as an HTTP service. It ran from the public Docker image (version 1.2.0, `medium` preset) with one job per CPU (`HYPER_CONCURRENCY` equal to the CPU count), and a test client kept that many PDFs in flight for 10 minutes after a 1 minute warm-up.

The corpus is **5,003 real public PDFs**, 25 GB in total, from 1 KB to 408 MB: everyday documents in four size bands, 600 files over 10 MB (50 over 100 MB), and 300 each of forms, signed, PDF/A and text-heavy documents, plus 254 scans. They come from Common Crawl (the SafeDocs set on Digital Corpora), GovDocs1, PubMed Central, arXiv, EUR-Lex, public web forms and public-domain Internet Archive books.

| Server | PDFs per hour | Per hour, files under 5 MB | Typical file | Errors | Cost per 1,000 PDFs |
|---|---|---|---|---|---|
| 8-CPU Arm | **11,694** | **65,471** | 0.29 s | 0 | **$0.031** |
| 32-CPU Arm | **36,888** | **271,840** | 0.25 s | 0 | **$0.039** |
| 8-CPU Intel | 7,950 | 37,455 | 0.47 s | 0 | $0.051 |
| 44-CPU Intel | 35,052 | 203,106 | 0.44 s | 0 | $0.063 |

![PDFs compressed per hour, every core busy](/images/throughput-per-hour.svg)

Every file completed, with no errors and no broken output, and every server's CPUs were 99% busy. Arm is Google Axion (Neoverse V2), Intel is Xeon Platinum 8481C (Sapphire Rapids); costs are Google Cloud on-demand list prices, November 2026. Memory peaked at 10 GB on the 8-CPU servers and 28 GB on the 44-CPU server.

![Cost per 1,000 PDFs](/images/throughput-cost.svg)

### By file size

How many PDFs per hour one server would do if every file were of one size, worked out from the full-load runs. Times are with every core busy.

| Files | Typical file, 8-CPU Arm | 95 in 100 within | Per hour, 8-CPU Arm | Per hour, 32-CPU Arm | Size saved |
|---|---|---|---|---|---|
| under 100 KB | 0.07 s | 0.18 s | 340,525 | 1,426,174 | 35% |
| 100 to 500 KB | 0.12 s | 0.44 s | 178,854 | 732,650 | 39% |
| 0.5 to 2 MB | 0.38 s | 1.39 s | 57,709 | 237,251 | 54% |
| 2 to 5 MB | 0.96 s | 3.50 s | 22,231 | 91,577 | 61% |
| 5 to 10 MB | 1.77 s | 5.92 s | 11,894 | 50,672 | 66% |
| 10 to 100 MB | 2.46 s | 12.3 s | 7,219 | 30,946 | 78% |
| over 100 MB | 2.6 min | 11.5 min | 112 | 393 | 59% |
| **all under 5 MB** | **0.19 s** | **1.74 s** | **65,471** | **271,840** | 56% |

![PDFs per hour on one 8-CPU server, by file size](/images/throughput-by-size.svg)

On the 32-CPU Arm server, scans took nearly three times as long as everyday documents (0.57 s against 0.21 s for a typical file) because their images are re-encoded. Signed PDFs are returned unchanged by default, so they finish in a few hundredths of a second; they are 300 of the 5,003 files and are included in the totals.

### Rerun it

The report with every table, the corpus list with each file's SHA-256 and public origin, the raw results and the scripts are in [`bench/throughput`](https://github.com/alam00000/bentopdf-hyper-compress/tree/main/bench/throughput). To run it on your own server:

```bash
python3 bench/throughput/fetch_corpus.py bench/throughput/manifest.csv corpus/
docker run -d --network host -e HYPER_CONCURRENCY=8 -e HYPER_QUEUE=16 -e HYPER_MAX_UPLOAD_MB=1024 \
  -e HYPER_TIMEOUT_MS=3600000 ghcr.io/alam00000/bentopdf-hyper-compress:v1.2.0
python3 bench/throughput/loadtest.py --corpus corpus/ --manifest bench/throughput/manifest.csv \
  --concurrency 8 --duration 600 --warmup 60 --out results/my-server
```

## Next steps

- [Guarantees](/guarantees): the promises behind the robustness numbers.
- [Choose the right settings](/guides/choosing-settings): which preset matches each benchmark level.
- [Running in production](/guides/production): speed and sizing on your own hardware.
