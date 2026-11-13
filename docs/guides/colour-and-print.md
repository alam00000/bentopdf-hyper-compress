---
title: Colour and print
description: Convert a PDF to RGB, CMYK or grayscale, keep image quality high enough for print, and know what Hyper Compress does and does not do with colour.
---

# Colour and print

Hyper Compress can convert a document to one colour model and keep images detailed enough for printing. This guide shows the settings for print and for screen, how colour conversion works, and how to check what was converted.

::: warning
Hyper is a compression engine, not a prepress tool. It does not create or check PDF/X files (the print exchange version of PDF), does not add output intents, does not apply ink limits or overprint rules, and does not touch spot colours. If a print shop needs a certified PDF/X file, run your prepress tool after Hyper, or ask the shop which settings it accepts.
:::

## Before you begin

- Install Hyper and compress one file first. See the [Quickstart](/getting-started).
- For print, start from the `low` preset: images at quality 80 and at most 200 dpi.
- For screen, start from `medium` (150 dpi) or `high` (72 dpi).
- If the file is a PDF/A archive and must stay one, read step 7 first: colour conversion is switched off there.

## 1. Choose the image resolution

Resolution has the biggest effect on both print quality and size. `maxDpi` sets the highest resolution colour and grayscale images keep; images above it are resampled down.

| Use | `maxDpi` | Preset that matches |
|---|---|---|
| Commercial print, photos that must stay sharp | 300 | none; override `low` |
| Office printing, archiving | 200 | `low` |
| Sharing and email | 150 | `medium` |
| Reading on screen only | 72 | `high` |

`maxDpi` takes 0 to 600, and 0 leaves resolution alone. To set colour and grayscale images separately, use `maxDpiColor` and `maxDpiGray`. Black and white (1-bit) images have their own limit, `maxDpiMono`; see [Scanned documents](/guides/scanned-documents).

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --preset low --set maxDpi=300
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'low',
  options: { maxDpi: 300 },
});
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'preset=low' \
  --url-query 'options={"maxDpi":300}' \
  'http://localhost:8080/api/compress'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { preset: 'low', options: { maxDpi: 300 } });
await writeFile('output.pdf', result.data);
```

:::

Hyper never scales an image up. An image that is lossless in the PDF is only resampled once it is more than one and a half times the limit, because resampling it saves little; set `forceDownsample: true` to resample every image above the limit.

## 2. Keep colour detail in JPEG images

Two more settings matter for print:

| Option | Values | Default | Print advice |
|---|---|---|---|
| `imageQuality` | 5 to 100 | 50 in `medium`, 80 in `low` | 80 or more |
| `jpegSubsampling` | `auto`, `444`, `422`, `420` | `auto` | `444` or `auto` |

`jpegSubsampling` controls chroma subsampling: how much colour detail JPEG keeps compared with brightness detail. `auto` lets the encoder decide and keeps full colour resolution. `444` always keeps it. `422` halves colour resolution horizontally, and `420` halves it both ways, which is the smallest and can blur fine coloured lines and small coloured text. Both options are ignored when `lossless` is on.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --preset low --set maxDpi=300 \
  --set imageQuality=85 --set jpegSubsampling=444
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'low',
  options: { maxDpi: 300, imageQuality: 85, jpegSubsampling: '444' },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'preset=low' \
  --url-query 'options={"maxDpi":300,"imageQuality":85,"jpegSubsampling":"444"}' \
  'http://localhost:8080/api/compress'
```

```c [C]
/* low preset pairs, with 1=300 (maxDpi), 2=85 (quality) and 53=3 (4:4:4) */
const int ids[]  = {1, 2, 3, 8, 18, 7, 63, 10, 13, 14, 16, 17, 20, 67, 53};
const int vals[] = {300, 85, 4, 1, 1, 1, 10, 1, 1, 1, 1, 1, 8322, 6, 3};
int rc = hpdf_compress_file("input.pdf", "output.pdf", ids, vals,
                            sizeof ids / sizeof ids[0], NULL);
```

:::

For files that must not change at all, use the `lossless` preset instead: images are only stored more efficiently and every pixel stays the same.

## 3. Convert to one colour model

`colorTarget` converts the document to one colour model.

| `colorTarget` | What is converted |
|---|---|
| `original` | Nothing. The default. |
| `rgb` | Images, text and vector colours, to sRGB |
| `cmyk` | Images, text and vector colours, to CMYK. Pure grays and pure black become K-only (black ink only). |
| `gray` | Images, text and vector colours, to grayscale |

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set colorTarget=cmyk
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { colorTarget: 'cmyk' },
});
console.log(result.warnings);
```

```bash [HTTP]
curl -s -D - -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"colorTarget":"cmyk"}' \
  'http://localhost:8080/api/compress' | grep -i '^x-warnings'
```

```bash [Docker]
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf --set colorTarget=cmyk
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { options: { colorTarget: 'cmyk' } });
await writeFile('output.pdf', result.data);
console.log(result.warnings);
```

```c [C]
/* 51 is the colour target (0 original, 1 sRGB, 2 CMYK, 3 gray); 69=1 also converts text and vectors */
const int ids[]  = {2, 3, 51, 69};
const int vals[] = {50, 4, 2, 1};
int rc = hpdf_compress_file("input.pdf", "output.pdf", ids, vals, 4, NULL);
```

:::

When every conversion was applied and the file got smaller, `warnings` is empty.

### What colour conversion keeps

- **Spot colours** (Separation and DeviceN, the named inks such as Pantone colours), **patterns** and **shadings** (smooth gradients) are kept as they are.
- **An image is only converted when the converted image is not larger.** A CMYK image holds four channels instead of three, so photos often stay in RGB after `colorTarget: 'cmyk'`. Step 4 shows how to check.
- **The whole file falls back.** If the converted file ends up no smaller than your input, Hyper returns your original bytes and adds a warning:

  ```text
  warning: colorTarget cmyk not applied: the converted file was not smaller than the original, so the original was kept
  ```

- **Lossless runs skip it.** `colorTarget` is ignored when `lossless` is on, without a warning.

## 4. Check what was converted

Ask for a [compression report](/report) and read each image's colour space before and after. The report is available from the CLI and the Node.js SDK.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set colorTarget=cmyk --report report.json
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { colorTarget: 'cmyk' },
  report: true,
});
for (const img of result.report.images) {
  console.log(img.page, img.name, img.before.colorSpace, '->', img.after?.colorSpace);
}
```

:::

```text
1 Im0 DeviceRGB -> DeviceRGB
```

An image still in `DeviceRGB` was not converted because its CMYK version would have been larger. An image stored as JPEG 2000 (`codec` `JPXDecode`) can show `none` as its colour space, because JPEG 2000 carries the colour space inside the image data.

## 5. Make a grayscale copy

There are two ways to get rid of colour:

| Option | Converts |
|---|---|
| `grayscale: true` | images only; text and vector colours stay |
| `colorTarget: 'gray'` | images, text and vector colours |

Use `grayscale` when coloured headings and charts should stay coloured but photos may go gray. Use `colorTarget: 'gray'` for a true black and white document. When both are set, `colorTarget` wins.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set colorTarget=gray
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', options: { colorTarget: 'gray' } });
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"colorTarget":"gray"}' \
  'http://localhost:8080/api/compress'
```

:::

Both are ignored when `lossless` is on.

## 6. Handle colour profiles and output intents

An ICC profile is a small file inside the PDF that describes exactly what its colours mean. An output intent tells a printer which profile the whole document was prepared for.

| Option | Default | What it does |
|---|---|---|
| `flattenIcc` | `true` in `low`, `medium` and `high`; `false` in `lossless` | Replaces embedded ICC profiles of 2 KB or more on page content with the matching plain colour space (gray, RGB or CMYK). Saves the profile bytes; colours can shift slightly where the profile differed from the standard one. No effect when `lossless` is on. |
| `removeOutputIntents` | `false` in every preset | Removes the output intents. |

For print, keep the profiles: set `flattenIcc: false`. Leave `removeOutputIntents` off; a print shop may rely on the output intent, and PDF/A and PDF/X require it.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --preset low --set flattenIcc=false
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'low',
  options: { flattenIcc: false },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'preset=low' \
  --url-query 'options={"flattenIcc":false}' \
  'http://localhost:8080/api/compress'
```

:::

The colour conversion in step 3 uses Hyper's own built-in profiles: sRGB and a generic CMYK profile. You cannot supply your own profile, and Hyper does not add or change an output intent to match the conversion.

## 7. Keep PDF/A colour

PDF/A, the archiving version of PDF, fixes how colour is described. When you compress a PDF/A file with `preserveConformance: true`, Hyper keeps it conformant:

- `colorTarget` is ignored, with the warning `colorTarget ignored to preserve PDF/A conformance`.
- `flattenIcc` and `removeOutputIntents` are switched off, so profiles and output intents stay.
- Everything else in this guide, such as resolution and JPEG quality, still applies.

::: code-group

```bash [CLI]
hyper archive.pdf output.pdf --set preserveConformance=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'archive.pdf',
  savePath: 'output.pdf',
  options: { preserveConformance: true },
});
console.log(result.pdfaPreserved, result.warnings);
```

```bash [HTTP]
curl -o output.pdf --data-binary @archive.pdf \
  'http://localhost:8080/api/compress?preservePdfa=true'
```

:::

Without `preserveConformance`, colour conversion runs and Hyper removes the PDF/A claim from the file, because the file may no longer meet the standard. See [PDF/A archives](/guides/pdfa).

## Recipes

| Goal | Options |
|---|---|
| Print-ready photos, smaller file | preset `low`, `maxDpi: 300`, `imageQuality: 85`, `jpegSubsampling: '444'`, `flattenIcc: false` |
| Office print | preset `low` |
| Screen only, smallest | preset `high` |
| Black and white handout | `colorTarget: 'gray'` |
| Colour charts, gray photos | `grayscale: true` |
| Everything in CMYK where it does not grow the file | `colorTarget: 'cmyk'`, then check with the report |

## Troubleshooting

**`colorTarget cmyk not applied` warning.** The converted file was not smaller than your input, so you got the original. Combine the conversion with a smaller `maxDpi` or `imageQuality`, or accept the original.

**Photos are still RGB after `colorTarget: 'cmyk'`.** Their CMYK version was larger, so Hyper kept them. Hyper does not force a conversion that grows the file; use a prepress tool if every image must be CMYK.

**Colours look slightly different after compressing.** `flattenIcc` replaced an embedded profile with the plain colour space. Set `flattenIcc: false`.

**Fine coloured lines or small coloured text look blurred.** Set `jpegSubsampling: '444'`.

**`colorTarget` did nothing and there is no warning.** `lossless` is on, which turns colour conversion off.

## Next steps

- [Choose the right settings](/guides/choosing-settings): every preset and how to override it.
- [PDF/A archives](/guides/pdfa): compress archive files and keep them conformant.
- [Compression report](/report): check every image before and after.
- [Options reference](/options): every option with its range and default.
