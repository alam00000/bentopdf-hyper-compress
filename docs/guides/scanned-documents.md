---
title: Scanned documents
description: Make scanned PDFs small while keeping the text sharp, with mixed raster content, JBIG2, 1-bit resolution limits and page rasterizing.
---

# Scanned documents

A scanned PDF is a stack of page photos, so almost all of its size is image data. Hyper Compress has four tools for scans. This guide shows when to use each one, what it costs, and how Hyper protects the pages while it works.

| Tool | Option | Best for |
|---|---|---|
| Mixed raster content | `mrc` | colour or grayscale scans of text pages |
| Shape reuse in black and white images | `jbig2Symbols` | black and white (1-bit) scans with many pages of text |
| 1-bit resolution limit | `maxDpiMono` | black and white scans at 400 dpi or more |
| Page rasterizing | `rasterizePages` | pages that are pictures already, or that you want as pictures |

## Before you begin

- Install Hyper and compress one file first. See the [Quickstart](/getting-started).
- Have a scanned PDF to try. The numbers on this page come from a 3-page, 300 dpi colour scan of text pages (3.7 MB).
- Start from the `medium` preset. If the text must stay as sharp as the scan, start from `low` and add `mrc` (step 2 shows why).

## 1. Compress with a preset first

Run the default preset so you have a number to compare against.

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({ sourcePath: 'scan.pdf', savePath: 'output.pdf' });
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

```bash [HTTP]
curl -o output.pdf --data-binary @scan.pdf \
  'http://localhost:8080/api/compress'
```

:::

```text
output.pdf
  3735039 -> 486287 bytes (87% smaller)
```

The `medium` preset resamples page images to 150 dpi and re-encodes them at quality 50. For many scans that is already enough. The cost is that text in the scan is now stored at 150 dpi, so small print gets softer.

## 2. Keep text sharp with mixed raster content

Mixed raster content (MRC) splits each scanned page into three layers: a sharp 1-bit mask that holds the text shapes at scan resolution (up to 300 dpi), a low-resolution background, and a low-resolution layer that holds the ink colour. Text stays crisp while the paper and colour areas are stored small.

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf --preset low --mrc auto
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'scan.pdf',
  savePath: 'output.pdf',
  preset: 'low',
  options: { mrc: 'auto' },
});
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

```bash [HTTP]
curl -o output.pdf --data-binary @scan.pdf \
  --url-query 'preset=low' \
  --url-query 'options={"mrc":"auto"}' \
  'http://localhost:8080/api/compress'
```

```bash [Docker]
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  scan.pdf output.pdf --preset low --mrc auto
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('scan.pdf'));
const result = await compress(input, { preset: 'low', options: { mrc: 'auto' } });
await writeFile('output.pdf', result.data);
```

```c [C]
/* the low preset pairs plus 40=1 (MRC auto), 42, 43 and 44 (layer settings) */
const int ids[]  = {1, 2, 3, 8, 18, 7, 63, 10, 13, 14, 16, 17, 20, 67, 40, 42, 43, 44};
const int vals[] = {200, 80, 4, 1, 1, 1, 10, 1, 1, 1, 1, 1, 8322, 6, 1, 75, 30, 50};
int rc = hpdf_compress_file("scan.pdf", "output.pdf", ids, vals,
                            sizeof ids / sizeof ids[0], NULL);
```

:::

On the sample scan:

| Settings | Output |
|---|---|
| `low` preset | 1,120,483 bytes |
| `low` preset with `mrc: 'auto'` | 554,840 bytes |
| `medium` preset with `mrc: 'auto'` | 486,287 bytes (MRC not used, see below) |

With `low`, MRC halves the file and keeps the text at the scan's resolution. With `medium`, the plain 150 dpi page was already as small as the MRC version, so Hyper kept the plain page.

`--mrc` on the CLI is shorthand for `--set mrc=...`. The HTTP API has no shorthand, so pass `mrc` inside `options`.

### Choose a mode

| `mrc` | What it does |
|---|---|
| `off` | The default. No page is split. |
| `auto` | Splits pages that look like scans: one or two images that together cover at least 80% of the page, and no visible text. An invisible OCR text layer is allowed. |
| `force` | Tries every page, whatever is on it. |

MRC is ignored when `lossless` is on, and so it never runs with the `lossless` preset.

### Tune the layers

| Option | Range | Default | What it changes |
|---|---|---|---|
| `mrcBackgroundDpi` | 36 to 300 | 75 | Resolution of the background and ink colour layers. The text mask keeps the scan resolution, up to 300 dpi. |
| `mrcBackgroundQuality` | 5 to 100 | 30 | JPEG quality of the background layer. |
| `mrcForegroundQuality` | 5 to 100 | 50 | JPEG quality of the ink colour layer. |
| `mrcDetectPhotos` | `true` or `false` | `true` | Finds photos on the page and stores them as their own image layer at the background resolution, instead of breaking them into black specks in the text mask. |

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf --preset low --mrc auto \
  --set mrcBackgroundDpi=100 --set mrcBackgroundQuality=40
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'scan.pdf',
  savePath: 'output.pdf',
  preset: 'low',
  options: { mrc: 'auto', mrcBackgroundDpi: 100, mrcBackgroundQuality: 40 },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @scan.pdf \
  --url-query 'preset=low' \
  --url-query 'options={"mrc":"auto","mrcBackgroundDpi":100,"mrcBackgroundQuality":40}' \
  'http://localhost:8080/api/compress'
```

:::

Raise `mrcBackgroundDpi` and `mrcBackgroundQuality` when coloured backgrounds, stamps or highlights look blotchy. Lower them for the smallest file when the background is plain paper.

## 3. Shrink black and white scans

Black and white (1-bit) scans from office scanners and fax machines are already small per page, but they add up. By default Hyper re-encodes them with lossless JBIG2, a format made for 1-bit images, so every pixel stays the same. Two options go further.

### Limit their resolution with maxDpiMono

`maxDpiMono` resamples 1-bit images above that resolution. They stay 1-bit. The default `0` leaves them at full resolution, because 1-bit text loses legibility quickly when it is scaled down. `300` is a safe value for scans made at 400 or 600 dpi. The `high` preset sets `maxDpiMono` to 150.

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf --set maxDpiMono=300
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({ sourcePath: 'scan.pdf', savePath: 'output.pdf', options: { maxDpiMono: 300 } });
```

```bash [HTTP]
curl -o output.pdf --data-binary @scan.pdf \
  --url-query 'options={"maxDpiMono":300}' \
  'http://localhost:8080/api/compress'
```

:::

An image that is lossless in the PDF, as most 1-bit scans are, is only resampled once it is more than one and a half times the limit. With `maxDpiMono: 300`, a 400 dpi scan is left alone and a 600 dpi scan is resampled. Set `forceDownsample: true` to resample every image above the limit.

### Reuse repeated shapes with jbig2Symbols

`jbig2Symbols` stores each repeated shape (most often a letter) once and reuses it across the pages. This is lossy JBIG2.

::: warning
Shape reuse can swap a shape for a similar one, for example a 6 for an 8 in a scanned invoice. Hyper decodes every page it encoded this way and compares it with the original area by area. A page is kept only when every small area matches; any page that fails keeps lossless JBIG2. Even so, do not turn this on for documents where a single wrong digit matters, such as contracts, invoices and medical records.
:::

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf --set jbig2Symbols=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({ sourcePath: 'scan.pdf', savePath: 'output.pdf', options: { jbig2Symbols: true } });
```

```bash [HTTP]
curl -o output.pdf --data-binary @scan.pdf \
  --url-query 'options={"jbig2Symbols":true}' \
  'http://localhost:8080/api/compress'
```

:::

Because the check rejects pages strictly, the gain depends heavily on the document. Compare the size with and without it on your own scans before you adopt it. `jbig2Symbols` is ignored when `lossless` is on.

## 4. Rasterize pages that are pictures

`rasterizePages` replaces each page with one rendered JPEG image. Use it for pages built from many vector shapes or overlapping images, where a single picture of the page is smaller.

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf --set rasterizePages=true --set rasterizeDpi=100
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'scan.pdf',
  savePath: 'output.pdf',
  options: { rasterizePages: true, rasterizeDpi: 100 },
});
console.log(result.warnings);
```

```bash [HTTP]
curl -s -D - -o output.pdf --data-binary @scan.pdf \
  --url-query 'options={"rasterizePages":true,"rasterizeDpi":100}' \
  'http://localhost:8080/api/compress' | grep -i '^x-warnings'
```

```c [C]
/* 60=1 rasterizes unless the document has text; 61 is the dpi, 62 the JPEG quality */
const int ids[]  = {60, 61, 62};
const int vals[] = {1, 100, 50};
int rc = hpdf_compress_file("scan.pdf", "output.pdf", ids, vals, 3, NULL);
```

:::

```text
output.pdf
  3735039 -> 216495 bytes (94% smaller)
```

| Option | Range | Default | What it changes |
|---|---|---|---|
| `rasterizeDpi` | 36 to 600 | 150 | Render resolution. Higher keeps more detail. |
| `rasterizeQuality` | 1 to 100 | 50 | JPEG quality of the rendered pages. |

Form fields are drawn into the page image and stop being fillable. Pages that would be larger than 256 million pixels at the chosen resolution are left as they are.

### The text guard

Rasterizing destroys real text: it stops being selectable and searchable, and links stop working. So before rasterizing, Hyper checks every page, including text inside reusable page parts and invisible OCR text. If any page has text, Hyper rasterizes nothing, compresses the file normally and adds this warning:

```text
warning: rasterize skipped: document has a text layer
```

In Node.js and WebAssembly it is in `result.warnings`, and over HTTP it is in the `X-Warnings` header.

To rasterize anyway, set `rasterizeText` as well. Only do this when you are sure you do not need the text layer.

::: code-group

```bash [CLI]
hyper scan.pdf output.pdf --set rasterizePages=true --set rasterizeText=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'scan.pdf',
  savePath: 'output.pdf',
  options: { rasterizePages: true, rasterizeText: true },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @scan.pdf \
  --url-query 'options={"rasterizePages":true,"rasterizeText":true}' \
  'http://localhost:8080/api/compress'
```

:::

In the C API, pass `60=2` instead of `60=1`. While Hyper is keeping a PDF/A file conformant (`preserveConformance`), rasterizing is switched off and reported as `rasterizePages disabled to preserve PDF/A conformance`.

## Which settings to start from

| Your scans | Start with |
|---|---|
| Colour or gray text pages, smallest file, softer text is fine | `medium` preset |
| Colour or gray text pages, text must stay sharp | `low` preset with `mrc: 'auto'` |
| Mixed documents where only some pages are scans | `mrc: 'auto'`, which skips pages with visible text |
| Black and white scans at 400 dpi or more | `maxDpiMono: 300` |
| Black and white scans, smallest file, wording not critical | `high` preset (1-bit images at 150 dpi), then try `jbig2Symbols: true` |
| Must fit an upload limit | a [size target](/guides/size-targets) on top of any row above |

## How it works

- **MRC checks every page it changes.** Hyper renders each page before and after the split. If the page changed visibly, or text lost ink, the page is put back as it was. A split page is also kept only when its layers are clearly smaller than the plain re-encoded page. Nothing is reported when a page is put back: the page stays as the preset left it.
- **MRC only touches the scan image.** Other content on the page, such as an invisible OCR text layer, stays as it was, so the page stays searchable.
- **The whole file is still compared.** As for every file, if the result is not smaller than your input, you get your original bytes. See [Guarantees](/guarantees).
- **Signed scans are returned untouched.** See [Signed documents](/guides/signed-documents).

To see what happened to each page image, ask for a [compression report](/report) (CLI and Node.js). After MRC or rasterizing, the original page image shows `after: null` because it was replaced.

## Troubleshooting

**`mrc` made no difference.** Either no page looked like a scan (try `mrc: 'force'`), or the split pages were not smaller than the plain pages and were kept plain. That is common with `medium` and `high`, which already shrink scans to 150 and 72 dpi. Use `low` with `mrc: 'auto'`, or raise `maxDpi`.

**Coloured areas look blotchy after MRC.** Raise `mrcBackgroundDpi` (for example to 100 or 150) and `mrcBackgroundQuality`.

**Photos on scanned pages turned into black dots.** Make sure `mrcDetectPhotos` is `true`, the default.

**`rasterize skipped: document has a text layer`, but you see no text.** The scan has an invisible OCR text layer. Keep it, and use `mrc` instead, which leaves it in place. Set `rasterizeText: true` only if you want to drop it.

**Black and white text looks broken up.** `maxDpiMono` is too low for the size of the print. Raise it to 300, or set it to `0` to keep full resolution.

## Next steps

- [Hit a file size target](/guides/size-targets): let Hyper search for the best quality under a limit.
- [Choose the right settings](/guides/choosing-settings): what every preset does and how to override it.
- [Options reference](/options): every option with its range and default.
- [Compression report](/report): check each page image before and after.
