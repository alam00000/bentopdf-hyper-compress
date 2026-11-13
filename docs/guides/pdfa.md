---
title: PDF/A documents
description: Compress PDF/A files so they either stay conformant or stop claiming to be PDF/A, and read what Hyper did from every surface.
---

# PDF/A documents

PDF/A is the archiving version of PDF: a set of ISO rules that keep a document readable for decades, such as embedded fonts and no external content. In this guide you compress a PDF/A file with Hyper Compress in one of two ways: keep it conformant, or compress it fully and let Hyper withdraw its PDF/A claim.

Either way, Hyper never leaves a file that claims to be PDF/A after a change that may have broken the rules.

## Before you begin

- Hyper installed for at least one surface. See the [Quickstart](/getting-started).
- A PDF/A file, saved as `input.pdf`.
- Decide what the output is for:
  - It goes into an archive or a system that checks PDF/A: **keep it conformant** (step 2).
  - It is a copy for reading or email: **compress fully** (step 3).

## 1. See what Hyper detects

Hyper reads the PDF/A claim from the file's XMP metadata, the `pdfaid:part` and `pdfaid:conformance` entries that PDF/A files carry. It trusts the claim and does not validate the file against the standard.

Every result reports the **input's** claim:

| Surface | Field | Example | No claim |
|---|---|---|---|
| Node.js, WebAssembly | `pdfa` | `{ part: 2, conformance: 'b' }` | `null` |
| HTTP | `X-Pdfa` | `2b` | empty |
| CLI | none | | |

`conformance` is lower case, and empty for levels without a letter, such as plain PDF/A-4. The CLI does not print the claim; use the HTTP API, Node.js or WebAssembly when you need it.

## 2. Keep the file conformant

Set `preserveConformance`. Over HTTP, `preservePdfa=true` does the same.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set preserveConformance=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { preserveConformance: true },
});
console.log(result.pdfa, result.pdfaPreserved, result.warnings);
```

```bash [HTTP]
curl -s -o output.pdf -D - --data-binary @input.pdf \
  'http://localhost:8080/api/compress?preservePdfa=true' | grep -i '^x-'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { options: { preserveConformance: true } });
await writeFile('output.pdf', result.data);
console.log(result.pdfa, result.pdfaOutcome, result.warnings);
```

:::

From Node.js you see:

```text
{ conformance: 'b', part: 2 } true []
```

The output keeps its PDF/A claim, and Hyper skipped every step that could break the standard. `preserveConformance` has no effect on a file that does not claim PDF/A, so it is safe to set for every file.

### What preserving switches off

For a PDF/A input, `preserveConformance` changes your options before the run. Some changes come with a [warning](/warnings), because you asked for something Hyper did not do. The others are silent, because a preset turns them on by default.

| Option | Switched off for | Warning |
|---|---|---|
| `rasterizePages` | every level | `rasterizePages disabled to preserve PDF/A conformance` |
| `brotli` | every level | `brotli disabled to preserve PDF/A conformance` |
| `removeMetadata` | every level | `removeMetadata refused: PDF/A requires the XMP metadata packet` |
| `colorTarget` (set back to `original`) | every level | `colorTarget ignored to preserve PDF/A conformance` |
| `linearize` | every level | `linearize skipped to preserve PDF/A conformance` |
| `minVersion` `2.0` (set back to `none`) | PDF/A-1, 2 and 3 | `minVersion 2.0 refused: this PDF/A level requires a 1.x header` |
| `removeStandardFonts`, `unembedAliasedFonts` | every level | none |
| `removeOutputIntents` | every level | none |
| `flattenIcc` | every level | none |
| `preferJpx`, `subsetFonts` | PDF/A-1 | none |
| `removeStructTree` | level A (1a, 2a, 3a) | none |

Preserving also changes how Hyper runs:

- The engine runs in its PDF/A mode. `speed: 'quality'`, for example, does not try JPEG 2000.
- The repack step that rewrites the file's structure is skipped.
- The PDF version in the file header stays as it was. `minVersion` can still raise it within the 1.x range, or to 2.0 for PDF/A-4.
- A [size target](/guides/size-targets) is not searched. Hyper runs once and `metTarget` says whether that result fits. On a miss the warning says so: `target of <n> bytes not reached: <m> bytes; no size search runs while preserving PDF/A, so turn off preserveConformance to let Hyper go further`.

As always, if the result is not smaller, you get your original bytes.

## 3. Compress fully and withdraw the claim

Leave `preserveConformance` off. Hyper then applies every option you ask for, including the preset's defaults such as `flattenIcc`, and then removes the PDF/A claim from the output.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf' });
console.log(result.pdfa, result.pdfaPreserved);
```

```bash [HTTP]
curl -s -o output.pdf -D - --data-binary @input.pdf \
  'http://localhost:8080/api/compress' | grep -i '^x-pdfa'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input);
await writeFile('output.pdf', result.data);
console.log(result.pdfa, result.pdfaOutcome);
```

:::

From Node.js you see:

```text
{ conformance: 'b', part: 2 } false
```

`pdfa` still shows the input's claim. `pdfaPreserved: false` says the output no longer claims PDF/A.

::: warning
Without `preserveConformance`, the output is an ordinary PDF. Do not send it to a system that requires PDF/A.
:::

What happens depends on the result:

| Result | Output | Node.js `pdfaPreserved`, HTTP `X-Pdfa-Preserved` | WebAssembly `pdfaOutcome` |
|---|---|---|---|
| The file got smaller | The compressed file, with the PDF/A entries removed from its XMP metadata. The rest of the metadata stays. | `false` | `claim-withdrawn` |
| Hyper could not make it smaller | Your original bytes, with their valid claim | `true` | `preserved` |
| The claim could not be removed | Your original bytes, with the warning below | `true` | `preserved` |

When the claim cannot be removed, or the file without the claim is not smaller than the original, Hyper keeps the original and warns, with the real level filled in:

```text
the PDF/A-2b claim could not be removed from the compressed file, so the original was kept; set preserveConformance to compress it as PDF/A
```

## Reading the outcome

| Surface | Kept as PDF/A | Claim withdrawn |
|---|---|---|
| Node.js | `pdfaPreserved: true` | `pdfaPreserved: false` |
| WebAssembly | `pdfaOutcome: 'preserved'` | `pdfaOutcome: 'claim-withdrawn'` |
| HTTP | `X-Pdfa-Preserved: true` | `X-Pdfa-Preserved: false` |
| CLI | no indicator | |

`pdfaOutcome` is `none` when the input does not claim PDF/A. `pdfaPreserved` and `X-Pdfa-Preserved` are `false` in that case, so read them together with `pdfa` or `X-Pdfa`. A [signed](/guides/signed-documents) PDF/A file comes back untouched, so it counts as preserved.

## Differences between PDF/A parts

| Part | What Hyper does differently while preserving |
|---|---|
| PDF/A-1 (1a, 1b) | Also keeps `preferJpx` and `subsetFonts` off. Refuses `minVersion` 2.0. |
| PDF/A-2 and PDF/A-3 (2a, 2b, 2u, 3a, 3b, 3u) | Refuses `minVersion` 2.0. |
| PDF/A-4 (4, 4e, 4f) | Allows `minVersion` 2.0, because PDF/A-4 is based on PDF 2.0. |
| Level A (1a, 2a, 3a) | Keeps the structure tree: `removeStructTree` stays off, because level A requires tagging. |

## Troubleshooting

**A system rejects the output as not PDF/A.** You compressed without `preserveConformance`, so the claim was withdrawn. Run again with `--set preserveConformance=true` or `preservePdfa=true`.

**The file barely shrank with `preserveConformance`.** Several steps that save space, such as flattening colour profiles and the repack, are switched off. That is the cost of keeping the standard. Compare with a run without it to see the difference for your file.

**Hyper does not detect a file as PDF/A.** The file has no `pdfaid` entries in its XMP metadata, so Hyper treats it as an ordinary PDF. A file that only looks like PDF/A, without the claim, gets no special handling.

## Next steps

- [Guarantees](/guarantees): the promise behind "conformant or not claimed".
- [Warnings](/warnings): every warning, including the PDF/A ones.
- [Colour and print](/guides/colour-and-print): colour profiles and output intents.
- [Document structure](/guides/document-structure): tagging, metadata and cleanups.
