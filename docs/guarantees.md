# Guarantees

Other PDF compression tools have a problem. Files get bigger, signatures break, standards quietly stop being valid, and tools sometimes skip things without telling you.

Hyper is built to avoid exactly that. These guarantees are enforced by the engine itself, no matter how you use it.

## The output is never larger than the input

Compression is monotonic. The engine compares its result against the original and returns whichever is smaller, so the worst case is your original bytes back unchanged. Across the 2,104 document benchmark, Hyper grew a file zero times; the tools it was compared against grew files in up to a quarter of runs.

## Signed documents come back untouched

A signed PDF can't be changed without breaking its signature. So Hyper doesn't try.

If it finds a digital signature, it returns the exact original file and tells you why it wasn't compressed and the result reports `signed: true` so your code can tell why nothing shrank.

## PDF/A stays compliant or stops claiming it is

Hyper will never leave a PDF claiming to meet the PDF/A standard when it no longer does.

With `preserveConformance` enabled, anything that could break compliance is automatically skipped. Without it, Hyper removes the PDF/A claim if the document no longer meets the standard. Each dropped step is reported in `warnings`

## Rasterization cannot destroy text

`rasterizePages` replaces pages with rendered images, which is dramatic for scans but destructive for anything with real text. The engine walks every page's content, including text inside form XObjects and invisible OCR layers, and refuses to rasterize when any text exists. The refusal is reported in the result's `warnings` rather than silently ignored.


## A size target gets Hyper's best effort

When you set a target file size, Hyper works at it properly. It searches for the highest image quality that still fits under your limit, and if nothing fits, it keeps going: quality all the way down to 5, then resolution down through 72, 50 and 36 dpi.

If your target is reachable, you get the best looking result that fits. If it is not, you get the smallest file Hyper could produce, `metTarget: false`, and a warning saying how small it managed to get. You are never handed a failure when a smaller file was still possible.

Two things Hyper will not do to hit a number: it will not silently change what kind of document you have, so page rasterization stays opt-in through `rasterizePages` (the warning points at it when it would help), and it will not break any of the guarantees above.

## Nothing fails silently

Whenever the engine does less than you asked for instead of doing something unsafe, the result's `warnings` array says so and why: a rasterize request skipped over a text layer, or rasterize and Brotli dropped to preserve PDF/A. An empty array means every requested option was applied. Hard failures are typed errors (`decrypt_failed`, `engine_error`, `timeout`, `cancelled`), never silent fallbacks.

## Hostile input is contained

Malformed PDFs can crash or hang software. Hyper isolates parsing so a bad file doesn't take down your application or service.

The engine is also fuzzed continuously through ClusterFuzzLite, on every pull request that touches it and again nightly, and every crash we find is turned into a permanent regression test.

