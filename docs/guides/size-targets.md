---
title: Hit a file size target
description: Ask Hyper for a file under a given size, read whether it got there, and understand the search, its limits and its cost in time.
---

# Hit a file size target

Upload portals and email servers often cap attachments at a fixed size. In this guide you ask Hyper Compress for a file under a size, check whether it got there, and learn what to do when it cannot.

You get the best looking file that fits. If nothing fits, you get the smallest file Hyper can make, and a clear "not met" signal instead of an error.

## Before you begin

- Hyper installed for at least one surface. See the [Quickstart](/getting-started).
- A PDF with images, saved as `input.pdf`. A size target works by lowering image quality and resolution, so a document that is mostly text has little room to move.
- A preset in mind. The search starts from the preset's settings; `medium` if you name none. See [Choose the right settings](/guides/choosing-settings).

## 1. Ask for a size

Pass the target in bytes, or with a unit in the CLI.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --target-size 1MB
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  targetSizeBytes: 1024 * 1024,
});
console.log(result.compressedSize, result.metTarget);
```

```bash [HTTP]
curl -s -o output.pdf -D - --data-binary @input.pdf \
  'http://localhost:8080/api/compress?targetSizeBytes=1048576' | grep -i '^x-'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { targetSizeBytes: 1024 * 1024 });
await writeFile('output.pdf', result.data);
console.log(result.compressedSize, result.metTarget);
```

:::

From the CLI you see:

```text
output.pdf
  2593056 -> 1023827 bytes (61% smaller) [target met]
```

Without a target, `medium` made this file 1,252,473 bytes. With the target, Hyper lowered image quality until the file fit under 1,048,576 bytes.

How each surface takes the size:

| Surface | Parameter | Units |
|---|---|---|
| CLI | `--target-size` | A byte count, or a number with `B`, `K`, `KB`, `M` or `MB` in any case. Decimals work (`1.5MB`). A kilobyte is 1024 bytes, so `2MB` is 2,097,152 bytes. `GB` is not accepted. |
| Node.js | `targetSizeBytes` | Bytes. Decimals are rounded down. 0, negative or non-numeric values mean no target. |
| HTTP | `targetSizeBytes` query parameter | Bytes. Decimals are rounded down. 0, negative or non-numeric values return `400 bad_target_size`. |
| WebAssembly | `targetSizeBytes` | Bytes. Decimals are rounded down. 0 or negative means no target. |

In the CLI, a size it cannot read, or `0`, is a usage error: the command exits with code 2.

## 2. Check whether the target was met

Every surface reports the outcome:

| Surface | Met | Not met | No target |
|---|---|---|---|
| CLI | `[target met]` after the sizes | `[target NOT met - best effort]` | nothing |
| Node.js, WebAssembly | `metTarget: true` | `metTarget: false` | `metTarget: null` |
| HTTP | `X-Met-Target: true` | `X-Met-Target: false` | `X-Met-Target` empty |

A miss is not an error. You still get a valid, smaller PDF, the CLI still exits with code 0, and the HTTP API still answers 200. A miss also adds this warning, with the real numbers filled in:

```text
warning: target of 409600 bytes not reached: 999426 bytes is the smallest this document compresses to at quality 5 and 36 dpi. For image-only documents, rasterizePages can go further.
output.pdf
  2593056 -> 999426 bytes (61% smaller) [target NOT met - best effort]
```

In code, branch on `metTarget`:

```js
import { compress } from 'hyper-compress';

const limit = 400 * 1024;
const result = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', targetSizeBytes: limit });

if (result.metTarget === false) {
  console.log(`Best effort: ${result.compressedSize} bytes, limit ${limit}`);
}
```

## 3. Go further on image-only documents

The search never changes what kind of document you have. If a scan or a print-to-PDF file still misses, you can allow Hyper to replace each page with one rendered image:

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --target-size 400KB \
  --set rasterizePages=true --set rasterizeDpi=100
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  targetSizeBytes: 400 * 1024,
  options: { rasterizePages: true, rasterizeDpi: 100 },
});
console.log(result.metTarget, result.warnings);
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'targetSizeBytes=409600' \
  --url-query 'options={"rasterizePages":true,"rasterizeDpi":100}' \
  'http://localhost:8080/api/compress'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, {
  targetSizeBytes: 400 * 1024,
  options: { rasterizePages: true, rasterizeDpi: 100 },
});
await writeFile('output.pdf', result.data);
console.log(result.metTarget, result.warnings);
```

:::

Hyper refuses to rasterize a document that has any text, including an invisible OCR layer, and says so with the warning `rasterize skipped: document has a text layer`. Your text stays searchable. [Scanned documents](/guides/scanned-documents) covers scans in depth.

## How it works

A size target runs the engine several times and keeps the best result.

1. **First pass.** Hyper compresses with your preset and options, as it would without a target. If that file already fits, it is the result, and nothing else runs.
2. **Quality search.** Otherwise Hyper turns `lossless` off and runs a binary search on `imageQuality`, from just below the starting quality down to 5, in at most 7 runs. The starting quality is the preset's `imageQuality` capped at 95 (80 if the preset's quality is 5). Every other option, including the resolution limits, stays as you set it. The search keeps the highest quality that fits.
3. **Resolution ladder.** If no quality fits, Hyper tries quality 5 at 72 dpi, then 50 dpi, then 36 dpi, with `forceDownsample` on so every colour and grayscale image above the limit is resampled. `maxDpiColor` and `maxDpiGray` are reset so the ladder's limit applies to both; `maxDpiMono` keeps your value. The ladder stops at the first rung that fits.
4. **Result.** If any run fit, you get the best fitting file: the highest quality from the search, or the first ladder rung that fit. If none fit, you get the smallest file any run produced. As always, if that is not smaller than your original, you get the original bytes.

::: info
With the `lossless` preset, a target that the first pass misses makes the run lossy: the search turns `lossless` off and tries qualities from 94 down to 5, because there is no other way to make images smaller. Without a target, `lossless` never re-encodes an image.
:::

### Cost in time

The search costs up to 11 engine runs (1 first pass, up to 7 quality steps, up to 3 ladder rungs), each followed by a repack. On the file above, a plain run took 1.5 seconds and the targeted run took about 10 seconds. A target that the first pass already meets costs nothing extra.

Each stage has its own time limit (10 minutes by default), so a long search is bounded per run, not as a whole. A run that passes the stage limit stops the call with the `timeout` error and a message such as `the engine pass ran longer than the 600000 ms stage limit`. To bound the whole call:

| Surface | Whole-call limit |
|---|---|
| Node.js | `totalTimeoutMs`. With a target, this is the setting that bounds the call. The run fails with `timeout`. |
| HTTP | `HYPER_TIMEOUT_MS` on the server (default 600000) covers the whole request; past it you get `504 timeout`. See [Self-hosting](/self-hosting). |
| CLI | No whole-call limit. |
| WebAssembly | No time limits; run it in a worker if you need to stop it. See [WebAssembly](/wasm). |

### When there is no search

- **PDF/A with `preserveConformance`.** While Hyper keeps a PDF/A document conformant, it runs once and does not search. `metTarget` still says whether that one result fits. On a miss you get this warning instead of the usual one:

  ```text
  target of <n> bytes not reached: <m> bytes; no size search runs while preserving PDF/A, so turn off preserveConformance to let Hyper go further
  ```

  See [PDF/A documents](/guides/pdfa).
- **Signed documents.** A signed PDF comes back untouched, so there is nothing to search. `metTarget` says whether the original fits. On a miss you get this warning:

  ```text
  target of <n> bytes not reached: signed documents are returned untouched; set removeSignatures to compress them
  ```

  Set `removeSignatures` to compress it; see [Signed documents](/guides/signed-documents).

## Troubleshooting

**The target is missed by a wide margin.** Most of the file is not images: text, fonts, vector drawings or attachments. Lower quality cannot shrink those. Look at the [compression report](/report) to see where the bytes are, and consider `--preset high` or the cleanup [options](/options).

**The result fits but looks rough.** The target forced quality or resolution very low. Raise the target, or start from `--preset low` so the search starts at a higher quality.

**`error: --target-size expects a size like 2MB, 500KB or a byte count`.** The CLI accepts `B`, `K`, `KB`, `M` and `MB` only. Write `1GB` as `1024MB`.

**The HTTP request returns `504`.** The search ran past `HYPER_TIMEOUT_MS`. Raise it on the server, or send a target that the first pass can reach.

## Next steps

- [Choose the right settings](/guides/choosing-settings): the preset sets where the search starts.
- [Scanned documents](/guides/scanned-documents): MRC and rasterizing for scans that miss.
- [Compression report](/report): see which images and fonts take the space.
- [Errors and cancellation](/guides/errors-and-cancellation): timeouts and stopping a long run.
