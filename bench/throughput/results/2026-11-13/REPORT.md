# Hyper Compress throughput: 2026-11-13

## Summary

| Machine | vCPUs | CPU | Best jobs at once | Documents/hour | Documents/hour per vCPU | p95 | USD/hour | USD per 1,000 documents |
|---|---|---|---|---|---|---|---|---|
| c3-standard-44 | 44 | x86_64 | 44 | 35,052 | 797 | 7.90 s | 2.218 | 0.0633 |
| c3-standard-8 | 8 | x86_64 | 8 | 7,950 | 994 | 8.15 s | 0.403 | 0.0507 |
| c4a-standard-32 | 32 | aarch64 | 32 | 36,888 | 1153 | 3.98 s | 1.437 | 0.0390 |
| c4a-standard-8 | 8 | aarch64 | 8 | 11,694 | 1462 | 4.30 s | 0.359 | 0.0307 |

## c3-standard-44

44 vCPUs, x86_64, Intel(R) Xeon(R) Platinum 8481C CPU @ 2.70GHz, 185.7 GB memory, Hyper 1.2.0, preset `medium`, 5003 PDFs, 600 s per level after 60 s warm-up.

| Jobs at once | Documents/hour | Pages/hour | p50 | p95 | p99 | CPU busy | Engine memory peak | Largest job | Errors |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 54 | 7,830 | 1.00 s | 239.69 s | 366.25 s | 5% | 0.8 GB | 0.8 GB | 0 |
| 2 | 3,930 | 70,230 | 0.14 s | 3.12 s | 7.62 s | 10% | 2.4 GB | 1.8 GB | 0 |
| 44 | 35,052 | 619,830 | 0.44 s | 7.90 s | 22.30 s | 100% | 28.3 GB | 5.2 GB | 0 |

Best: 44 jobs at once.

By group at the best level:

| Group | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|
| forms | 2,052 | 0.42 s | 4.15 s | 62.1% |
| general | 20,784 | 0.36 s | 4.23 s | 62.8% |
| large | 4,038 | 4.84 s | 30.17 s | 78.0% |
| pdfa | 2,148 | 0.32 s | 3.37 s | 63.7% |
| scans | 1,806 | 1.13 s | 8.38 s | 65.4% |
| signed | 2,148 | 0.04 s | 0.08 s | 0.4% |
| text-heavy | 2,076 | 0.42 s | 3.43 s | 65.2% |

By file size at the best level:

| Size | Documents/hour | p50 | p95 |
|---|---|---|---|
| 0.5-2MB | 8,856 | 0.66 s | 2.70 s |
| 100-500KB | 9,570 | 0.20 s | 0.75 s |
| 2-10MB | 6,444 | 2.13 s | 9.11 s |
| <100KB | 6,000 | 0.10 s | 0.24 s |
| >10MB | 4,182 | 4.82 s | 30.40 s |

If every file were of one kind (44 jobs at once, from every finished request of that level; documents/hour = jobs at once x 3,600 / average seconds per file):

| Files | Measured | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|---|
| under 100 KB | 1,232 | 1,250,000 | 0.10 s | 0.25 s | 35% |
| 100 to 500 KB | 1,984 | 580,432 | 0.20 s | 0.76 s | 39% |
| 0.5 to 2 MB | 1,768 | 177,227 | 0.63 s | 2.59 s | 53% |
| 2 to 5 MB | 1,015 | 66,967 | 1.73 s | 6.63 s | 62% |
| 5 to 10 MB | 354 | 35,807 | 3.43 s | 11.00 s | 68% |
| 10 to 100 MB | 828 | 21,949 | 4.43 s | 22.61 s | 78% |
| over 100 MB | 66 | 355 | 376.12 s | 1202.36 s | 57% |
| all under 5 MB | 5,999 | 203,106 | 0.30 s | 3.12 s | 56% |
| all under 10 MB | 6,353 | 161,152 | 0.34 s | 4.00 s | 60% |
| forms | 430 | 145,996 | 0.42 s | 3.87 s | 62% |
| general | 4,302 | 154,348 | 0.36 s | 4.19 s | 63% |
| large | 854 | 3,846 | 4.99 s | 114.24 s | 68% |
| pdfa | 435 | 194,400 | 0.32 s | 3.03 s | 64% |
| scans | 358 | 64,581 | 1.16 s | 9.73 s | 64% |
| signed | 432 | 3,024,771 | 0.04 s | 0.08 s | 0% |
| text-heavy | 436 | 174,842 | 0.41 s | 3.52 s | 64% |

## c3-standard-8

8 vCPUs, x86_64, Intel(R) Xeon(R) Platinum 8481C CPU @ 2.70GHz, 33.7 GB memory, Hyper 1.2.0, preset `medium`, 5003 PDFs, 600 s per level after 60 s warm-up.

| Jobs at once | Documents/hour | Pages/hour | p50 | p95 | p99 | CPU busy | Engine memory peak | Largest job | Errors |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 54 | 7,830 | 1.04 s | 265.30 s | 405.19 s | 33% | 0.8 GB | 0.8 GB | 0 |
| 2 | 3,456 | 62,712 | 0.16 s | 3.38 s | 7.86 s | 56% | 1.3 GB | 0.9 GB | 0 |
| 4 | 4,398 | 84,738 | 0.22 s | 4.84 s | 15.04 s | 85% | 4.0 GB | 2.1 GB | 0 |
| 8 | 7,950 | 138,456 | 0.47 s | 8.15 s | 25.08 s | 99% | 9.8 GB | 5.2 GB | 0 |

Best: 8 jobs at once.

By group at the best level:

| Group | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|
| forms | 480 | 0.49 s | 3.89 s | 61.2% |
| general | 4,866 | 0.39 s | 4.39 s | 61.9% |
| large | 846 | 5.05 s | 37.56 s | 82.2% |
| pdfa | 414 | 0.28 s | 2.24 s | 59.1% |
| scans | 378 | 1.30 s | 19.35 s | 63.6% |
| signed | 444 | 0.04 s | 0.08 s | 0.0% |
| text-heavy | 522 | 0.40 s | 3.84 s | 63.2% |

By file size at the best level:

| Size | Documents/hour | p50 | p95 |
|---|---|---|---|
| 0.5-2MB | 2,016 | 0.69 s | 2.81 s |
| 100-500KB | 2,166 | 0.19 s | 0.78 s |
| 2-10MB | 1,566 | 2.28 s | 9.49 s |
| <100KB | 1,314 | 0.10 s | 0.25 s |
| >10MB | 888 | 5.00 s | 36.66 s |

If every file were of one kind (8 jobs at once, from every finished request of that level; documents/hour = jobs at once x 3,600 / average seconds per file):

| Files | Measured | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|---|
| under 100 KB | 251 | 238,789 | 0.10 s | 0.25 s | 36% |
| 100 to 500 KB | 427 | 110,458 | 0.19 s | 0.80 s | 39% |
| 0.5 to 2 MB | 386 | 31,723 | 0.66 s | 2.86 s | 54% |
| 2 to 5 MB | 217 | 12,678 | 1.69 s | 5.88 s | 61% |
| 5 to 10 MB | 91 | 6,113 | 3.07 s | 12.81 s | 66% |
| 10 to 100 MB | 178 | 3,544 | 4.94 s | 26.07 s | 80% |
| over 100 MB | 10 | 89 | 75.80 s | 896.29 s | 68% |
| all under 5 MB | 1,281 | 37,455 | 0.30 s | 3.14 s | 56% |
| all under 10 MB | 1,372 | 27,951 | 0.35 s | 4.13 s | 60% |
| forms | 96 | 26,943 | 0.48 s | 3.87 s | 61% |
| general | 946 | 27,181 | 0.38 s | 4.26 s | 63% |
| large | 178 | 1,123 | 5.16 s | 46.28 s | 75% |
| pdfa | 84 | 33,889 | 0.27 s | 2.29 s | 60% |
| scans | 72 | 8,221 | 1.55 s | 20.25 s | 62% |
| signed | 85 | 644,295 | 0.04 s | 0.08 s | 0% |
| text-heavy | 99 | 32,259 | 0.39 s | 4.63 s | 65% |

## c4a-standard-32

32 vCPUs, aarch64, Neoverse-V2, 134.8 GB memory, Hyper 1.2.0, preset `medium`, 5003 PDFs, 600 s per level after 60 s warm-up.

| Jobs at once | Documents/hour | Pages/hour | p50 | p95 | p99 | CPU busy | Engine memory peak | Largest job | Errors |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 444 | 19,452 | 0.11 s | 2.70 s | 308.85 s | 6% | 0.8 GB | 0.8 GB | 0 |
| 2 | 4,968 | 93,222 | 0.11 s | 2.34 s | 6.26 s | 12% | 2.7 GB | 2.1 GB | 0 |
| 4 | 11,316 | 215,964 | 0.11 s | 2.13 s | 6.20 s | 25% | 6.1 GB | 5.2 GB | 0 |
| 32 | 36,888 | 709,716 | 0.25 s | 3.98 s | 11.53 s | 100% | 22.8 GB | 5.3 GB | 0 |

Best: 32 jobs at once.

By group at the best level:

| Group | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|
| forms | 2,142 | 0.21 s | 2.06 s | 62.3% |
| general | 21,804 | 0.20 s | 2.25 s | 62.6% |
| large | 4,344 | 2.42 s | 21.45 s | 71.6% |
| pdfa | 2,316 | 0.20 s | 1.63 s | 65.8% |
| scans | 1,902 | 0.53 s | 3.44 s | 65.4% |
| signed | 2,202 | 0.03 s | 0.06 s | 0.4% |
| text-heavy | 2,178 | 0.23 s | 1.75 s | 64.1% |

By file size at the best level:

| Size | Documents/hour | p50 | p95 |
|---|---|---|---|
| 0.5-2MB | 9,204 | 0.36 s | 1.38 s |
| 100-500KB | 10,128 | 0.12 s | 0.41 s |
| 2-10MB | 6,690 | 1.11 s | 4.33 s |
| <100KB | 6,378 | 0.07 s | 0.15 s |
| >10MB | 4,488 | 2.42 s | 19.85 s |

If every file were of one kind (32 jobs at once, from every finished request of that level; documents/hour = jobs at once x 3,600 / average seconds per file):

| Files | Measured | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|---|
| under 100 KB | 1,330 | 1,426,174 | 0.07 s | 0.16 s | 35% |
| 100 to 500 KB | 2,151 | 732,650 | 0.12 s | 0.41 s | 39% |
| 0.5 to 2 MB | 1,906 | 237,251 | 0.35 s | 1.34 s | 53% |
| 2 to 5 MB | 1,093 | 91,577 | 0.96 s | 3.35 s | 62% |
| 5 to 10 MB | 381 | 50,672 | 1.73 s | 5.88 s | 68% |
| 10 to 100 MB | 899 | 30,946 | 2.29 s | 10.72 s | 77% |
| over 100 MB | 73 | 393 | 174.67 s | 986.67 s | 57% |
| all under 5 MB | 6,480 | 271,840 | 0.18 s | 1.63 s | 57% |
| all under 10 MB | 6,861 | 218,807 | 0.19 s | 2.10 s | 60% |
| forms | 458 | 196,180 | 0.22 s | 2.11 s | 62% |
| general | 4,643 | 205,894 | 0.21 s | 2.23 s | 63% |
| large | 930 | 4,359 | 2.48 s | 75.17 s | 68% |
| pdfa | 477 | 259,260 | 0.20 s | 1.58 s | 65% |
| scans | 393 | 101,701 | 0.57 s | 3.86 s | 64% |
| signed | 465 | 3,507,458 | 0.03 s | 0.06 s | 0% |
| text-heavy | 467 | 231,651 | 0.22 s | 1.84 s | 64% |

## c4a-standard-8

8 vCPUs, aarch64, Neoverse-V2, 33.6 GB memory, Hyper 1.2.0, preset `medium`, 5003 PDFs, 600 s per level after 60 s warm-up.

| Jobs at once | Documents/hour | Pages/hour | p50 | p95 | p99 | CPU busy | Engine memory peak | Largest job | Errors |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 426 | 19,302 | 0.10 s | 2.71 s | 309.23 s | 26% | 0.8 GB | 0.8 GB | 0 |
| 2 | 4,890 | 90,972 | 0.11 s | 2.41 s | 6.43 s | 48% | 2.7 GB | 2.1 GB | 0 |
| 4 | 9,324 | 168,192 | 0.16 s | 2.91 s | 8.33 s | 84% | 6.4 GB | 5.3 GB | 0 |
| 8 | 11,694 | 216,156 | 0.29 s | 4.30 s | 12.65 s | 99% | 10.0 GB | 5.3 GB | 0 |

Best: 8 jobs at once.

By group at the best level:

| Group | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|
| forms | 612 | 0.27 s | 2.07 s | 61.2% |
| general | 7,122 | 0.24 s | 2.34 s | 61.9% |
| large | 1,290 | 2.62 s | 17.91 s | 74.0% |
| pdfa | 714 | 0.22 s | 1.75 s | 64.4% |
| scans | 540 | 0.64 s | 5.34 s | 61.2% |
| signed | 714 | 0.04 s | 0.07 s | 0.0% |
| text-heavy | 702 | 0.21 s | 1.67 s | 62.0% |

By file size at the best level:

| Size | Documents/hour | p50 | p95 |
|---|---|---|---|
| 0.5-2MB | 2,946 | 0.40 s | 1.48 s |
| 100-500KB | 3,192 | 0.13 s | 0.44 s |
| 2-10MB | 2,226 | 1.24 s | 4.59 s |
| <100KB | 1,980 | 0.07 s | 0.18 s |
| >10MB | 1,350 | 2.58 s | 17.48 s |

If every file were of one kind (8 jobs at once, from every finished request of that level; documents/hour = jobs at once x 3,600 / average seconds per file):

| Files | Measured | Documents/hour | p50 | p95 | Saved |
|---|---|---|---|---|---|
| under 100 KB | 400 | 340,525 | 0.07 s | 0.18 s | 35% |
| 100 to 500 KB | 664 | 178,854 | 0.12 s | 0.44 s | 38% |
| 0.5 to 2 MB | 586 | 57,709 | 0.38 s | 1.39 s | 54% |
| 2 to 5 MB | 342 | 22,231 | 0.96 s | 3.50 s | 61% |
| 5 to 10 MB | 122 | 11,894 | 1.77 s | 5.92 s | 66% |
| 10 to 100 MB | 277 | 7,219 | 2.46 s | 12.28 s | 78% |
| over 100 MB | 16 | 112 | 154.04 s | 687.59 s | 59% |
| all under 5 MB | 1,992 | 65,471 | 0.19 s | 1.74 s | 56% |
| all under 10 MB | 2,114 | 51,963 | 0.21 s | 2.17 s | 59% |
| forms | 139 | 48,709 | 0.23 s | 2.01 s | 61% |
| general | 1,444 | 48,573 | 0.23 s | 2.33 s | 62% |
| large | 277 | 1,544 | 2.58 s | 25.23 s | 71% |
| pdfa | 151 | 65,816 | 0.20 s | 1.73 s | 64% |
| scans | 108 | 19,535 | 0.69 s | 5.90 s | 62% |
| signed | 145 | 803,247 | 0.03 s | 0.07 s | 0% |
| text-heavy | 143 | 60,328 | 0.21 s | 1.88 s | 63% |

