# Hyper Compress throughput

Run of 2026-11-13. Hyper Compress 1.2.0 (the public Docker image `ghcr.io/alam00000/bentopdf-hyper-compress:v1.2.0`),
`medium` preset, running as an HTTP service. How the test works and how to repeat it is in [README.md](README.md);
every raw number is in [results/2026-11-13/](results/2026-11-13/) and the generated tables are in
[results/2026-11-13/REPORT.md](results/2026-11-13/REPORT.md).

## In short

- One 8-CPU Arm server compresses **about 11,700 real-world PDFs per hour**, or
  280,000 a day, with every core busy, no errors and no broken files.
- If the PDFs are all under 5 MB, the same server does **about 65,000 per hour** (1.6 million a day), and a
  typical file takes 0.19 seconds.
- A 32-CPU Arm server does **about 36,900 per hour** of the mixed set and about 272,000 per hour of files under 5 MB.
- It costs **3 to 6 US cents per 1,000 PDFs** at cloud list prices, and 1.5 to 2.2 cents on spot (spare-capacity) machines.
- Arm does about 47% more work per CPU than Intel and costs about 40% less per PDF.
- Memory was never the limit: at most 28 GB used, on machines with 32 to 176 GB.

![PDFs compressed per hour, every core busy](results/2026-11-13/charts/throughput.svg)

![Cost per 1,000 PDFs](results/2026-11-13/charts/cost.svg)

## The PDFs

5,003 public PDFs, 25.1 GB in total, each with a public download address, listed in [manifest.csv](manifest.csv).
The middle file is 658 KB; 617 are over 10 MB and 50 are between 100 and 408 MB (scanned public-domain books).

| Group | Files | What it is |
|---|---|---|
| general | 2,949 | everyday documents in four size bands: under 100 KB, 100 to 500 KB, 0.5 to 2 MB, 2 to 10 MB |
| large | 600 | 10 MB and over |
| forms | 300 | fillable forms |
| signed | 300 | digitally signed |
| pdfa | 300 | archival PDF/A |
| text-heavy | 300 | 5 or more pages, mostly text |
| scans | 254 | pages that are one scanned image |

Sources: Common Crawl PDFs (the SafeDocs CC-MAIN-2021-31 set on Digital Corpora, 2,591 files), GovDocs1 (852),
PubMed Central Open Access (712), arXiv (545), EUR-Lex (143), public web forms from 76 sites (110) and
public-domain Internet Archive books (50).

## Full load: every core busy

The service ran as many jobs at once as the machine has CPUs (`HYPER_CONCURRENCY` = CPUs), and the test client
kept exactly that many PDFs in flight for 10 minutes after a 1-minute warm-up, cycling through the whole set in a
fixed shuffled order. The totals include the 300 signed PDFs, which Hyper returns unchanged by default.

| Server | PDFs per hour | Per CPU | Pages per hour | Typical file | 95 in 100 within | CPU busy | Peak memory | Errors | US$ per hour | **US$ per 1,000 PDFs** | Spot US$ per 1,000 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 8-CPU Arm | **11,694** | 1,462 | 216,156 | 0.29 s | 4.3 s | 99% | 10.0 GB | 0 | 0.359 | **0.031** | 0.015 |
| 32-CPU Arm | **36,888** | 1,153 | 709,716 | 0.25 s | 4.0 s | 99% | 22.8 GB | 0 | 1.437 | **0.039** | 0.019 |
| 8-CPU Intel | **7,950** | 994 | 138,456 | 0.47 s | 8.2 s | 99% | 9.8 GB | 0 | 0.403 | **0.051** | 0.018 |
| 44-CPU Intel | **35,052** | 797 | 619,830 | 0.44 s | 7.9 s | 100% | 28.3 GB | 0 | 2.218 | **0.063** | 0.022 |

Prices are Google Cloud's list prices on the day of the run (see How this was measured). Spot prices change over time.

Bigger servers give a little less per CPU: four times the CPUs gave 3.2 times the PDFs on Arm, and 5.5 times the
CPUs gave 4.4 times on Intel. Several 8-CPU servers are the cheapest way to scale.

## By file size

How many PDFs per hour a server would do if every file were of that size, worked out from every file of that size
in the full-load run (jobs at once x 3,600 / average seconds per file). The times are under full load, with other
files sharing the server.

![PDFs per hour on one 8-CPU server, by file size](results/2026-11-13/charts/by-size.svg)

| Files | Typical file, 8-CPU Arm | 95 in 100 within | **Per hour, 8-CPU Arm** | Per hour, 8-CPU Intel | **Per hour, 32-CPU Arm** | Per hour, 44-CPU Intel | Size saved |
|---|---|---|---|---|---|---|---|
| under 100 KB | 0.07 s | 0.18 s | **340,525** | 238,789 | **1,426,174** | 1,250,000 | 35% |
| 100 to 500 KB | 0.12 s | 0.44 s | **178,854** | 110,458 | **732,650** | 580,432 | 39% |
| 0.5 to 2 MB | 0.38 s | 1.39 s | **57,709** | 31,723 | **237,251** | 177,227 | 54% |
| 2 to 5 MB | 0.96 s | 3.50 s | **22,231** | 12,678 | **91,577** | 66,967 | 61% |
| 5 to 10 MB | 1.77 s | 5.92 s | **11,894** | 6,113 | **50,672** | 35,807 | 66% |
| 10 to 100 MB | 2.46 s | 12.3 s | **7,219** | 3,544 | **30,946** | 21,949 | 78% |
| over 100 MB | 2.6 min | 11.5 min | **112** | 89 | **393** | 355 | 59% |
| **all under 5 MB** | **0.19 s** | **1.74 s** | **65,471** | 37,455 | **271,840** | 203,106 | 56% |
| all under 10 MB | 0.21 s | 2.17 s | 51,963 | 27,951 | 218,807 | 161,152 | 59% |

## By kind of PDF (32-CPU Arm, full load)

| Kind | Typical file | 95 in 100 within | Per hour | Size saved |
|---|---|---|---|---|
| everyday documents | 0.21 s | 2.2 s | 205,894 | 63% |
| forms | 0.22 s | 2.1 s | 196,180 | 62% |
| PDF/A | 0.20 s | 1.6 s | 259,260 | 65% |
| text-heavy | 0.22 s | 1.8 s | 231,651 | 64% |
| scans | 0.57 s | 3.9 s | 101,701 | 64% |
| large (10 MB and over) | 2.48 s | 75 s | 4,359 | 68% |
| signed | 0.03 s | 0.06 s | passed through | 0% |

Signed PDFs are returned unchanged by default, because changing them would break the signature, so their speed only
measures the trip through the service and is not compression speed. Scans take nearly three times as long as everyday
documents because their images are re-encoded.

## Fewer jobs than cores

| Server | 1 job at once | 2 at once | 4 at once | full load |
|---|---|---|---|---|
| 8-CPU Arm | 426 | 4,890 | 9,324 | 11,694 |
| 32-CPU Arm | 444 | 4,968 | 11,316 | 36,888 |
| 8-CPU Intel | 54 | 3,456 | 4,398 | 7,950 |
| 44-CPU Intel | 54 | 3,930 | | 35,052 |

PDFs per hour. The 1-job figures are not a fair measure of speed: every level starts at the same place in the file
order, and with a single job the 100 MB+ books near the start fill most of the 10 minutes (on Intel two of them took
7 minutes each, so only 9 files finished inside the window). They show why a service should run one job per core. For the
time a single file takes, use the tables by size above.

## How this was measured

The servers were Google Cloud virtual machines in `us-central1-a`, Debian 12, one of each. Hyper Compress runs the
same way on any Linux server; the exact machine types are named so anyone can rent the same ones and check the numbers.

| Server | Machine type | CPUs (vCPUs) | Memory | Processor |
|---|---|---|---|---|
| 8-CPU Arm | `c4a-standard-8` | 8 | 32 GB | Google Axion (Arm Neoverse V2) |
| 32-CPU Arm | `c4a-standard-32` | 32 | 128 GB | Google Axion (Arm Neoverse V2) |
| 8-CPU Intel | `c3-standard-8` | 8 | 32 GB | Intel Xeon Platinum 8481C (Sapphire Rapids), 2.7 GHz |
| 44-CPU Intel | `c3-standard-44` | 44 | 176 GB | Intel Xeon Platinum 8481C (Sapphire Rapids), 2.7 GHz |

Hyper ran from its Docker image. To check that Docker does not slow it down, the same full-load test was run twice
on one 8-CPU Intel server running Debian 13: in the Docker image, and with the same binaries run directly on the
server. Docker: 8,010 PDFs per hour, typical file 0.42 s. Direct: 8,064 PDFs per hour, typical file 0.42 s. The
0.7% difference is within run-to-run noise.

Costs use Google Cloud's on-demand list price in `us-central1` on 2026-11-13, from the Cloud Billing Catalog API
(price per vCPU and per GB of memory times the machine's size; [prices.json](prices.json)). The full method, the
scripts and how to repeat the run on any server are in [README.md](README.md).
