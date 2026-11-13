---
title: Warnings
description: Every warning Hyper Compress can return, when it appears, where you see it, and what to do about it.
---

# Warnings

A warning means Hyper Compress did less than you asked, because doing it would have been unsafe for this file or would not have helped. The compression still succeeded. This page lists every warning, word for word.

An empty warnings list means every option you asked for was applied.

## Where warnings appear

| Surface | Where |
|---|---|
| CLI | One `warning: <text>` line per warning on stderr, before the result. The exit code stays 0. |
| Node.js SDK | `result.warnings`, an array of strings. |
| Rust crate | `result.warnings`, a `Vec<String>`. |
| HTTP API | The `X-Warnings` response header: a URL-encoded JSON array, empty when there are none. Decode with `JSON.parse(decodeURIComponent(header))`. See [HTTP API](/http-api). |
| WebAssembly | `result.warnings`, an array of strings. |
| C API | No warnings list. See [C API](#c-api). |

Warnings are fixed English strings. Match on their start (for example `startsWith('linearize')`) rather than the full text, because some contain numbers or names from your file.

## Every warning

"Native" below means the CLI, the Node.js SDK, the Rust crate and the HTTP API, which share one core.

### Rasterizing and size

| Warning | When | Surfaces | What to do |
|---|---|---|---|
| `rasterize skipped: document has a text layer` | You set `rasterizePages`, and the document has real text (including invisible OCR text). Hyper refuses to turn text into pictures. | Native | Leave it: the file was still compressed. If you want every page as an image anyway, also set `rasterizeText`; text then stops being selectable and searchable. See [Scanned documents](/guides/scanned-documents). |
| `target of <target> bytes not reached: <size> bytes is the smallest this document compresses to at quality 5 and 36 dpi. For image-only documents, rasterizePages can go further.` | You set a size target and even the smallest result is above it. You get that smallest result, and `metTarget` is `false`. | All | Raise the target, or for scans try `rasterizePages` or `mrc`. See [Hit a file size target](/guides/size-targets). |
| `target of <target> bytes not reached: <size> bytes; no size search runs while preserving PDF/A, so turn off preserveConformance to let Hyper go further` | You set a size target and `preserveConformance` on a PDF/A file. Hyper compressed once, without a search, and the result is above the target. | All | Turn off `preserveConformance` if the size matters more than PDF/A. See [PDF/A archives](/guides/pdfa). |
| `target of <target> bytes not reached: signed documents are returned untouched; set removeSignatures to compress them` | You set a size target on a signed file that is larger than the target. Signed files are returned unchanged, so no search runs. | All | Set `removeSignatures` if a smaller unsigned file is acceptable. See [Signed documents](/guides/signed-documents). |

### Signatures

| Warning | When | Surfaces | What to do |
|---|---|---|---|
| `digital signatures removed so the document could be compressed` | You set `removeSignatures` on a signed document, and the compressed file was used. `signaturesRemoved` is `true`. The document is no longer signed. | All | Expected. Sign the output again if you need a signature. See [Signed documents](/guides/signed-documents). |

### Not applied because the original was kept

Hyper never returns a file larger than the input. When the converted file was not smaller, it returns your original, and these options did not take effect.

| Warning | When | Surfaces | What to do |
|---|---|---|---|
| `colorTarget <value> not applied: the converted file was not smaller than the original, so the original was kept` | You set `colorTarget` to `rgb`, `cmyk` or `gray` (and not `lossless`), and the result was not smaller than the input. | Native | If you need the colour conversion even when it costs size, Hyper cannot do it. Convert with another tool. See [Colour and print](/guides/colour-and-print). |
| `linearize not applied: the optimized file was not smaller than the original, so the original was kept` | You set `linearize` (fast web view), and the result was not smaller than the input. | Native | Linearize the original with another tool, for example `qpdf --linearize`. |

### WebAssembly only

| Warning | When | Surfaces | What to do |
|---|---|---|---|
| `linearize is not available in the WebAssembly build` | You set `linearize`. | WebAssembly | Use a native surface, or linearize the output afterwards. |

### Reports

| Warning | When | Surfaces | What to do |
|---|---|---|---|
| `report unavailable: the PDF structure could not be read` | You asked for a [report](/report), and the input or output could not be read for it, or `qpdf` is not available. No report is returned or written. | CLI, Node.js SDK, Rust crate | Check that `qpdf` is installed and found (`HYPER_QPDF`). If it is, the file's structure is unusual; the compressed output is still fine. |

### PDF/A

PDF/A is the archiving version of PDF. A PDF/A file carries a claim in its metadata that it meets the standard. These warnings only appear for a file that claims PDF/A.

With `preserveConformance` on, Hyper switches off every step that would break the standard. These are the ones you asked for that it switched off:

| Warning | When | Surfaces |
|---|---|---|
| `rasterizePages disabled to preserve PDF/A conformance` | You set `rasterizePages`. | All |
| `brotli disabled to preserve PDF/A conformance` | You set `brotli`. | All |
| `removeMetadata refused: PDF/A requires the XMP metadata packet` | You set `removeMetadata`. | All |
| `colorTarget ignored to preserve PDF/A conformance` | You set `colorTarget` to anything but `original`. | All |
| `linearize skipped to preserve PDF/A conformance` | You set `linearize`. | All |
| `minVersion 2.0 refused: this PDF/A level requires a 1.x header` | You set `minVersion` to `2.0` on a PDF/A-1, -2 or -3 file. | All |

What to do: nothing, if keeping the file valid PDF/A matters more. If it does not, turn `preserveConformance` off; Hyper then applies everything and removes the PDF/A claim instead.

Without `preserveConformance`, one more warning can appear:

| Warning | When | Surfaces | What to do |
|---|---|---|---|
| `the PDF/A-<level> claim could not be removed from the compressed file, so the original was kept; set preserveConformance to compress it as PDF/A` | Hyper compressed the file, which may break PDF/A, but could not remove the claim from the result. Rather than return a file that claims a standard it may not meet, it returned your original. `<level>` is for example `2b`. In WebAssembly, `pdfaOutcome` is then `preserved`. | All | Set `preserveConformance` as the warning says. See [PDF/A archives](/guides/pdfa). |

## Options switched off without a warning

With `preserveConformance` on a PDF/A file, Hyper also switches these off when you set them, without a warning:

| Option | Switched off for |
|---|---|
| `removeStandardFonts` | every PDF/A level |
| `unembedAliasedFonts` | every PDF/A level |
| `removeOutputIntents` | every PDF/A level |
| `flattenIcc` | every PDF/A level |
| `preferJpx` | PDF/A-1 |
| `subsetFonts` | PDF/A-1 |
| `removeStructTree` | levels with conformance `a` (PDF/A-1a, -2a, -3a) |

`flattenIcc` and `subsetFonts` are on in most presets, so they are switched off on almost every PDF/A file you compress with `preserveConformance`. With `preserveConformance`, a size target is also not searched: Hyper compresses once and reports whether that met the target.

Some options have no effect in certain cases by design, without a warning:

- `lossless` (and the `lossless` preset) ignores `imageQuality`, every resolution limit, `colorTarget`, `grayscale`, `flattenIcc`, `jpegSubsampling`, `jpegProgressive`, `preferJpx`, `clipImages`, `mrc`, `jbig2Symbols` and `contentPrecision`.
- `rasterizeText`, `rasterizeDpi` and `rasterizeQuality` do nothing without `rasterizePages`.
- `mrcBackgroundDpi`, `mrcBackgroundQuality`, `mrcForegroundQuality` and `mrcDetectPhotos` do nothing while `mrc` is `off`.
- `grayscale` does nothing when `colorTarget` is not `original`.

## Outcomes that are not warnings

These are reported in the result, not as warnings:

| Outcome | How you see it |
|---|---|
| The file could not be made smaller, so you got the original | Output size equals input size. |
| The input is signed, so you got it unchanged | `signed: true`; CLI `[signed: original preserved]`; HTTP `X-Signed: true`. |
| You set `removeSignatures`, but the compressed file was not smaller, so the signed original came back | `signed: true` and `signaturesRemoved: false`. |
| The size target was met or missed | `metTarget`; CLI `[target met]` or `[target NOT met - best effort]`. |

Input that is not a PDF, and a PDF that needs a password you did not give, are [errors](/errors), not warnings.

## C API

The C API returns codes, not warnings. Two things are reported differently:

- A refused rasterize prints `[hyper] rasterize skipped: document has a text layer` to the process's stderr.
- A signed input gives return code (or status) `2`.

See [C API](/c-api).

## Next steps

- [Errors](/errors): failures, as opposed to warnings.
- [Guarantees](/guarantees): the promises these warnings protect.
- [Options](/options): every option and its range.
