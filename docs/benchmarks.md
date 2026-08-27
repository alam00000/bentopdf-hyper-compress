# Benchmarks

Hyper Compress was benchmarked against Ghostscript, MuPDF and qpdf on 2,104 real world PDFs from four public corpora, for a total of 27,352 engine runs. The corpus selection was pre-registered with a fixed seed before any files were fetched, and every document's SHA-256 hash and origin URL are recorded, so the benchmark can be reproduced independently.

Visual fidelity was measured with Poppler, deliberately chosen as a renderer none of the tested engines share.

## Size reduction

Median size reduction at the median worst-page SSIM:

| Tier | Hyper | Ghostscript | MuPDF | qpdf |
|---|---|---|---|---|
| Lossless | **22.4% @ 1.000** | 21.1% @ 0.997 | 4.7% @ 1.000 | 6.3% @ 1.000 |
| 300 dpi | **52.3% @ 1.000** | 19.1% @ 0.996 | 14.0% @ 1.000 | n/a |
| 150 dpi | **58.2% @ 0.999** | 42.4% @ 0.994 | 19.4% @ 1.000 | n/a |
| 72 dpi | **64.8% @ 0.996** | 53.9% @ 0.990 | 28.0% @ 0.999 | n/a |

## Robustness

| Engine | Runs | Corrupt | Grew the file |
|---|---:|---:|---:|
| **Hyper** | 8,416 | **33 (0.39%)** | **0** |
| qpdf | 2,104 | 1 (0.05%) | 53 (2.5%) |
| Ghostscript | 8,416 | 256 (3.04%) | 2,089 (24.8%) |
| MuPDF | 8,416 | 333 (3.96%) | 70 (0.8%) |

Hyper had the lowest corruption rate of any engine that actually compresses images (qpdf's near-zero rate reflects that it only repacks streams, which is also why it saves 6 percent instead of 58), and it is the only engine that never returned a file larger than the input.

## Reproduce it

The full methodology, per-file results and the complete document list live in [comparison.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/comparison.md) in the repository, alongside `bench/MANIFEST.csv` and `bench/RESULTS.csv`. To run the harness against your own documents:

```bash
make bench CORPUS=path/to/corpus
```
