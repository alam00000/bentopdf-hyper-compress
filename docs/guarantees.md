---
title: Guarantees
description: What Hyper Compress promises about every file it returns, and how each promise is enforced.
---

# Guarantees

Compression tools often fail in quiet ways: files get bigger, signatures break, archived files still claim a standard they no longer meet, and options are skipped without a word. Hyper Compress is built so none of that happens. This page lists each promise, what it means for your files, and where it holds.

| Guarantee | CLI and Node.js SDK | HTTP API | WebAssembly | Rust crate | C API |
|---|---|---|---|---|---|
| [Never bigger](#the-output-is-never-larger-than-the-input) | yes | yes | yes | yes | no, compare sizes yourself |
| [Signed documents untouched](#signed-documents-come-back-untouched) | yes | yes | yes | yes | yes |
| [PDF/A stays true](#pdf-a-stays-compliant-or-stops-claiming-it-is) | yes | yes | yes | yes | only with `PDFA_MODE` |
| [Rasterizing cannot destroy text](#rasterizing-cannot-destroy-text) | yes | yes | yes | yes | yes |
| [Best effort on size targets](#a-size-target-gets-hyper-s-best-effort) | yes | yes | yes | yes | no target search |
| [Nothing fails silently](#nothing-fails-silently) | yes | yes | yes | yes | return codes only |
| [Hostile input is contained](#hostile-input-is-contained) | yes | yes | no, runs in process | yes | no, runs in process |
| [Safe from many threads](#safe-to-call-from-many-threads-at-once) | yes | yes | one call at a time per instance | yes | yes, calls take turns |

## The output is never larger than the input

Hyper compares its result with the file it started from and returns whichever is smaller. The worst case is your original bytes, unchanged.

You can tell when this happened: the output size equals the input size. It is an outcome, not an error. In the public benchmarks Hyper made no file bigger in 4,000 runs against commercial SDKs, while the other engines made between 5 and 120 files bigger. See [Benchmarks](/benchmarks).

## Signed documents come back untouched

Changing any byte of a digitally signed PDF breaks the signature. When Hyper finds a signature, it returns the exact original file and sets `signed: true` (the CLI prints `[signed: original preserved]`, the HTTP API sends `X-Signed: true`).

If you would rather have a smaller file than a signed one, set `removeSignatures`. Hyper then removes the signatures, keeps the visible signature on the page, compresses the document and reports `signaturesRemoved: true` with a warning. The result is no longer signed and can be signed again. See [Signed documents](/guides/signed-documents).

## PDF/A stays compliant or stops claiming it is

A PDF/A file (the archiving version of PDF) carries a claim in its metadata that it meets the standard. Hyper never leaves that claim on a file that no longer meets it.

- **With `preserveConformance`**, every step that could break the standard is switched off, and each one you asked for is reported as a warning. The claim stays and stays true.
- **Without it**, if Hyper changes the file it removes the PDF/A claim from the metadata, so the file stops claiming a standard it may no longer meet. If Hyper returns the original unchanged, the original's claim stays, because it is still true.
- **If the claim cannot be removed** from a compressed file, Hyper returns the original instead, with a warning.

See [PDF/A archives](/guides/pdfa).

## Rasterizing cannot destroy text

`rasterizePages` replaces each page with a picture of itself, which is very effective for scans but destroys real text. Hyper checks every page first, including text inside reusable page parts and invisible OCR layers, and refuses to rasterize when any text exists. The refusal is reported as a warning.

Set `rasterizeText` when you want every page as an image anyway. Text then stops being selectable and searchable, and links stop working. See [Scanned documents](/guides/scanned-documents).

## A size target gets Hyper's best effort

When you ask for a size, Hyper searches for the highest image quality that fits, then lowers quality down to 5 and resolution down through 72, 50 and 36 dpi. If the target can be reached, you get the best looking file that fits. If not, you get the smallest file Hyper could make, `metTarget: false`, and a warning with the size it reached.

Hyper will not change what kind of document you have to hit a number: rasterizing stays your choice, and none of the other guarantees are relaxed. See [Hit a file size target](/guides/size-targets).

## Nothing fails silently

Whenever Hyper does less than you asked because doing it would be unsafe for this file, the result's `warnings` list says what was skipped and why. An empty list means every requested option was applied. Every warning is listed in [Warnings](/warnings).

Real failures are typed errors (`decrypt_failed`, `engine_error`, `timeout`, `cancelled`), never silent fallbacks. See [Errors](/errors).

## Hostile input is contained

Broken or deliberately malicious PDFs can crash or hang software. The native surfaces run the engine in a separate worker process with a time limit, so a bad file ends that process, not your application or service.

The engine is also fuzzed continuously with ClusterFuzzLite, on every pull request that touches it and again nightly, and every crash found becomes a permanent regression test.

The WebAssembly build and the C API run inside your process. For untrusted input there, use a worker thread (WebAssembly) or run the `hpdf-worker` program (C). See [Running in production](/guides/production).

## Safe to call from many threads at once

Hyper is thread safe. The CLI, the Node.js SDK, the Rust crate and the HTTP service run every compression in its own engine process, with its own memory and temporary folder, so any number of calls can run at the same time without affecting each other, and each one can use its own CPU core.

The C API runs the engine inside your process. It is safe to call from any thread: calls made at the same moment take turns, and each thread gets its own `hpdf_last_error` message. For several compressions at once from C, run several processes.

A WebAssembly instance runs one compression at a time. For parallel work, start one worker per compression; each loads its own instance.
