# Throughput benchmark

How many PDFs per hour Hyper Compress sustains as an HTTP service, over a public corpus of 5,003 real PDFs. The
results of the 2026-11-13 run are in [REPORT.md](REPORT.md). Everything here is enough to repeat the test on any
Linux server and get comparable numbers.

## What is measured

For each server and each concurrency level (jobs in flight at once):

| Measure | How |
|---|---|
| Documents, pages and input GB per hour | completed requests inside a fixed window (600 s) after a warm-up (60 s), scaled to one hour |
| Response time p50, p95, p99 | per request, from sending the PDF to receiving the last byte |
| CPU busy | whole server, from `/proc/stat`, every second |
| Memory | resident memory of all engine processes (`hpdf-worker`, `qpdf`, `hyper-server`) every second; peak total and the largest single job |
| Errors and invalid outputs | any non-200 answer, and any 200 answer that is not a PDF or is larger than its input |
| Saving | total output bytes against total input bytes |

The client is closed-loop: `N` workers each send a PDF, wait for the answer and send the next one, so the service
always has exactly `N` jobs. The service runs with `HYPER_CONCURRENCY=N`. Files are taken in a fixed shuffled order
(seed 20261113) and cycled, so every server and level sees the same mix.

Results are also split by corpus group and by file size. In `summary.json` the per-group rates are each group's share
of the mixed run. The report's tables by size and kind estimate what a server would do if every file were of that
kind: jobs at once x 3,600 / average seconds per file of that kind, from the same mixed run.

## The corpus

[manifest.csv](manifest.csv) lists every file: `sha256, group, band, bytes, pages, source, source_url`. Files are
named `<sha256>.pdf`. Every file has a public origin anyone can download; test-suite files made to trigger single
rules (veraPDF, pdf.js, qpdf, pdfium) are excluded. 5,003 files, 25.1 GB, from 1 KB to 408 MB.

| Group | Files | What it is |
|---|---|---|
| general | 2,949 | everyday documents in four size bands: under 100 KB, 100 to 500 KB, 0.5 to 2 MB, 2 to 10 MB |
| large | 600 | 10 MB and over, including 50 over 100 MB |
| forms | 300 | an AcroForm with at least one widget |
| signed | 300 | a digital signature with `/ByteRange` |
| pdfa | 300 | a `pdfaid` claim in the XMP metadata |
| text-heavy | 300 | 5 or more pages, 1,000+ characters per page, images under 30% of the page |
| scans | 254 | 60% or more of sampled pages are one image covering 85% or more of the page |

Sources: Common Crawl PDFs (the SafeDocs CC-MAIN-2021-31 set on Digital Corpora, 2,591 files), GovDocs1 (852),
PubMed Central Open Access (712), arXiv (545), EUR-Lex (143), public web forms from 76 sites (110) and
public-domain Internet Archive books from 1700 to 1925 (50). Files were picked in SHA-256 order within each group.

To download the corpus:

```bash
python3 bench/throughput/fetch_corpus.py bench/throughput/manifest.csv corpus/
```

It downloads every file (unpacking single files from the Common Crawl and GovDocs zips), checks each against its
SHA-256 and reports any that changed at the source. EUR-Lex can return different bytes for the same document over
time, so some of its 143 files may be reported as changed; leave those out or replace them.

## What we used

| Server | CPUs (vCPUs) | Memory | Processor |
|---|---|---|---|
| 8-CPU Arm | 8 | 32 GB | Google Axion (Arm Neoverse V2), Google Cloud `c4a-standard-8` |
| 32-CPU Arm | 32 | 128 GB | Google Axion (Arm Neoverse V2), Google Cloud `c4a-standard-32` |
| 8-CPU Intel | 8 | 32 GB | Intel Xeon Platinum 8481C (Sapphire Rapids), 2.7 GHz, Google Cloud `c3-standard-8` |
| 44-CPU Intel | 44 | 176 GB | Intel Xeon Platinum 8481C (Sapphire Rapids), 2.7 GHz, Google Cloud `c3-standard-44` |

- Debian 12, the corpus on a disk attached to the server, client and service on the same server.
- Hyper Compress 1.2.0 from the public Docker image, `medium` preset, started as:

```bash
docker run -d --name hyper --network host -v /data/tmp:/tmp \
  -e HYPER_CONCURRENCY=8 -e HYPER_QUEUE=16 -e HYPER_MAX_UPLOAD_MB=1024 -e HYPER_TIMEOUT_MS=3600000 \
  ghcr.io/alam00000/bentopdf-hyper-compress:v1.2.0
```

  with `HYPER_CONCURRENCY` set to each level and `HYPER_QUEUE` to twice that.
- Levels run: 1, 2 and full load (as many jobs as CPUs) on every server, plus 4 on the 8-CPU servers and the
  32-CPU Arm server.
- Costs use Google Cloud's on-demand list price in `us-central1` on 2026-11-13: price per vCPU and per GB of memory
  times the server's size, in [prices.json](prices.json).

## Running it

On the server, with the service started as above:

```bash
python3 bench/throughput/loadtest.py --corpus corpus/ --manifest bench/throughput/manifest.csv \
  --concurrency 8 --duration 600 --warmup 60 --preset medium --out results/<run>/<server>
python3 bench/throughput/report.py results/<run> --prices bench/throughput/prices.json > results/<run>/REPORT.md
python3 bench/throughput/charts.py results/<run> --prices bench/throughput/prices.json --price-note "At ... prices"
```

Next to each server's results, save `machine.json` (`machine_type`, `vcpus`, `cpu`, `arch` and `memory_bytes`, from
`lscpu` and `/proc/meminfo`) and `engine-version.txt` (the output of `hyper --version`). The report names the
hardware from them, and the charts use the full-load level (concurrency equal to `vcpus`). `--prices` takes a JSON
file of server folder name to USD per hour and adds cost per 1,000 PDFs.

## Files

| File | Purpose |
|---|---|
| `REPORT.md` | the written report of the 2026-11-13 run |
| `manifest.csv` | the corpus: every file's checksum, group, size, pages and public origin |
| `fetch_corpus.py` | downloads the corpus from its public origins, checking checksums |
| `loadtest.py` | the closed-loop client and resource sampler (standard library only) |
| `report.py` | turns results into tables (`results/<run>/REPORT.md`) |
| `charts.py` | draws the report charts (`results/<run>/charts/*.svg`, light and dark) |
| `prices.json` | on-demand USD per hour per server, used for cost per 1,000 PDFs |
| `results/<run>/` | raw results (`requests.jsonl`, `samples.jsonl`, `summary.json` per level), tables and charts |
