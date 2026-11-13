---
title: Choose the right settings
description: Pick a preset, override single options on top of it from any surface, check what will run, and know what each important option costs and buys.
---

# Choose the right settings

Hyper Compress is driven by presets and options. In this guide you pick a preset, change a few options on top of it, check the exact settings Hyper will run with, and learn what the options that matter most cost and buy.

## Before you begin

- Hyper installed for at least one surface. See the [Quickstart](/getting-started).
- A PDF to try, saved as `input.pdf`.
- The [options reference](/options) open in another tab. This guide covers the important options; the reference lists all of them.

## 1. Start from a preset

A preset is a complete set of options. Four presets exist, and with no preset Hyper uses `medium`. Most options are the same in all four. These are the ones that differ:

| Option | `low` | `medium` | `high` | `lossless` |
|---|---|---|---|---|
| `imageQuality` | 80 | 50 | 20 | 100 (not used) |
| `maxDpi` | 200 | 150 | 72 | 0 (no limit) |
| `maxDpiMono` | 0 (no limit) | 0 (no limit) | 150 | 0 (no limit) |
| `lossless` | false | false | false | true |
| `clipImages` | false | true | true | false |
| `flattenIcc` | true | true | true | false |
| `preferJpx` | true | true | true | false |
| `contentPrecision` | 6 | 6 | 6 | 0 (exact) |
| `removeAlternates` | false | false | true | false |
| `removeStandardFonts` | false | false | true | false |
| `unembedAliasedFonts` | false | false | true | false |
| `removeThreads` | false | false | true | false |
| `removeSpiderInfo` | false | false | true | false |

Every preset subsets fonts (`subsetFonts`), merges duplicate fonts and resources (`mergeFonts`), stores few-colour images as palettes (`reduceColor`), and removes embedded thumbnails and application private data. No preset turns on `brotli`, `linearize`, `rasterizePages`, `mrc`, `colorTarget` or `preserveConformance`. The full table is in [Preset values](/options#preset-values).

Pick the preset by what the reader does with the file:

- `low`: printing and archiving, where images must stay close to the original.
- `medium`: sharing and email. The default.
- `high`: reading on screen, where the smallest file matters most. It also leaves the 14 standard fonts (and metric clones such as Arial) to the viewer instead of embedding them.
- `lossless`: every image pixel must stay the same. Only the way the file is stored changes.

## 2. Override options on top of the preset

Pass only the options you want to change. Everything you leave out keeps the preset's value.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --preset high --set imageQuality=35 --set maxDpiMono=300
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'high',
  options: { imageQuality: 35, maxDpiMono: 300 },
});
console.log(result.compressedSize, result.warnings);
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'preset=high' \
  --url-query 'options={"imageQuality":35,"maxDpiMono":300}' \
  'http://localhost:8080/api/compress'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, {
  preset: 'high',
  options: { imageQuality: 35, maxDpiMono: 300 },
});
await writeFile('output.pdf', result.data);
console.log(result.compressedSize, result.warnings);
```

:::

Each surface reads overrides a little differently:

- **CLI.** `--set key=value` repeats. `true` and `false` become booleans, anything that parses as a number becomes a number (including `1e3`, and `0x10`, `0o17` and `0b101` as in JavaScript), and everything else is passed as text (`--set colorTarget=cmyk`). A later `--set` for the same key wins. `--brotli` is short for `--set brotli=true` and `--mrc auto` for `--set mrc=auto`.
- **Node.js and WebAssembly.** `options` is a partial object of [options](/options), typed as `Partial<HyperCompressOptions>`.
- **HTTP.** `options` is a URL-encoded JSON object of at most 4096 characters. It works with any preset. `preset=custom` means "the `medium` preset plus these options" and requires `options`; without it you get `400 options_required`. The query shorthands `brotli=true` and `preservePdfa=true` set `brotli` and `preserveConformance` and win over the same fields in `options`. Any other value for them, such as `1`, counts as not set.

::: tip
`--url-query` needs curl 7.87 or newer and does the URL encoding for you. Do not add `-G`: it moves the PDF into the URL.
:::

## 3. Know how bad values are handled

A bad option never fails a run. Hyper corrects it and carries on, without a warning:

| You pass | Hyper uses |
|---|---|
| A number outside the allowed range | The nearest end of the range. Decimals are rounded. `maxDpi: 1000` becomes 600. |
| `contentPrecision` of 1 or 2 | 3, the smallest precision that rounds |
| A choice that is not one of the listed values | The choice's fallback: `colorTarget` `original`, `jpegSubsampling` `auto`, `speed` `balanced`, `annotations` `keep`, `minVersion` `none`, `mrc` `off` |
| A value of the wrong type, such as `"yes"` for a boolean or `"low"` for a number | A fixed fallback, **not the preset's value**: `false` for booleans (`true` for `mrcDetectPhotos`), and for numbers the value in the `default` field of `HYPER_OPTION_DOCS` (`imageQuality` 80, `maxDpi` 150, `contentPrecision` 0) |
| An option name Hyper does not know, such as `colourTarget` | Nothing. The name is ignored. |

The wrong-type rule catches people out. `--preset high --set subsetFonts=yes` turns font subsetting off, because `yes` is text and not `true`. `--set imageQuality=forty` runs at quality 80, not the preset's 20.

Preset names are checked strictly by the CLI (exit code 2) and the HTTP API (`400 bad_preset`). The Node.js SDK and the WebAssembly package do not reject a misspelt preset name: Node.js `compress` and `resolveOptions`, and WebAssembly `compress`, fall back to `medium`. In TypeScript the `CompressLevel` type catches the typo at compile time.

## 4. Check what will run

Resolve the preset and your overrides into the full set of options before you compress. This shows the effect of every correction above.

::: code-group

```js [Node.js]
import { resolveOptions } from 'hyper-compress';

const options = resolveOptions({
  sourcePath: 'input.pdf',
  preset: 'high',
  options: { imageQuality: 35, maxDpi: 1000, subsetFonts: 'yes' },
});
console.log(options.imageQuality, options.maxDpi, options.subsetFonts);
```

```js [WebAssembly]
import { HYPER_PRESETS, normalizeHyperOptions } from 'hyper-compress-wasm';

const options = normalizeHyperOptions({
  ...HYPER_PRESETS.high,
  imageQuality: 35,
  maxDpi: 1000,
  subsetFonts: 'yes',
});
console.log(options.imageQuality, options.maxDpi, options.subsetFonts);
```

:::

```text
35 600 false
```

`resolveOptions` takes the same object as `compress` and returns all 56 options. It reads no file, so `sourcePath` can be any string. The WebAssembly package has no `resolveOptions`; spreading the preset and normalizing, as above, is exactly what its `compress` does. The CLI and the HTTP API have no way to print resolved options.

Resolved options are what you asked for. Some can still change for a particular file: `preserveConformance` switches options off for a PDF/A input (see [PDF/A documents](/guides/pdfa)), and a size target lowers quality and resolution (see [Hit a file size target](/guides/size-targets)). Those changes are reported in `warnings` or `metTarget`.

## 5. Tune the options that matter

These options decide most of the size and most of the visible change. Defaults below are the `medium` values.

### imageQuality

JPEG quality for re-encoded images, 5 to 100, default 50. Lower is smaller, with more visible artefacts. Below about 20 they are obvious, which is why no preset goes lower than 20. A [size target](/guides/size-targets) can go down to 5. Ignored when `lossless` is on.

### maxDpi and the per-class limits

Images above the limit are resampled down. This usually saves more than quality does, because a 300 dpi photo shown on screen carries four times the pixels of a 150 dpi one.

| Option | Applies to | `medium` |
|---|---|---|
| `maxDpi` | colour and grayscale images; 0 means no limit | 150 |
| `maxDpiColor`, `maxDpiGray` | one class only; 0 means "use `maxDpi`" | 0 |
| `maxDpiMono` | 1-bit (black and white) images such as text scans; 0 means no limit | 0 |
| `thresholdDpi` | only resample images above this resolution | 0 |
| `forceDownsample` | resample every image above the limit, even slightly above | false |

1-bit text loses legibility quickly, so `maxDpiMono` stays off except in `high` (150). 300 is a safe value for scans. By default a lossy image is resampled once it is above the limit, and a losslessly stored one only once it is above it by half, because resampling those buys little; `forceDownsample` removes that margin.

### lossless

Turns off every lossy image step: quality, resolution limits, colour conversion, `grayscale`, JPEG options, JPEG 2000, `mrc`, `jbig2Symbols`, `clipImages`, `contentPrecision` and ICC profile flattening (`flattenIcc`). Fonts are still subset and merged, cleanups still run, and images are only stored more efficiently. So `lossless: true` on top of any preset handles images the way `--preset lossless` does; the preset's font and cleanup settings stay.

### speed

How many encodings Hyper tries per image. `fast` encodes JPEG only, with no JPEG 2000 and no palette trials. `balanced` (the default) runs the trials the other options ask for. `quality` also tries JPEG 2000 (except in PDF/A) and lossy and lossless palettes for every image, and keeps the smallest. More trials take more time. Ignored when `lossless` is on.

### preferJpx

Lets JPEG 2000 compete with JPEG for each image and keeps the smaller result. On in every preset except `lossless`. Turn it off when the files go to a tool that cannot read JPEG 2000. It is also switched off when preserving PDF/A-1, which does not allow JPEG 2000.

### jpegSubsampling

How much colour detail re-encoded JPEG images keep. `auto` (the default) lets the encoder decide, which keeps full colour resolution. `420` halves colour resolution in both directions and is the smallest. `422` halves it horizontally, and `444` keeps it. Colour edges, such as red text in a photo, show the loss first.

### subsetFonts and mergeFonts

Both are on in every preset and both are lossless. `subsetFonts` keeps only the characters the document uses; text looks and searches the same. `mergeFonts` stores each duplicate font program, font dictionary and resource once, which helps most on documents merged from many files. There is rarely a reason to turn either off.

### contentPrecision

How many significant digits Hyper keeps for numbers when it rewrites page content compactly, 0 to 8, default 6. 0 keeps every value exactly. 3 to 8 round coordinates to that many digits (always keeping at least 3 decimals, so no value moves by more than 0.0005 units) and colours to 3 decimals. Ignored when `lossless` is on.

### brotli

Compresses the file's internal streams with Brotli, a PDF 2.0 feature, and marks the file as PDF 2.0. Files get smaller, but **the reader must support it**: MuPDF 1.26 and newer, Ghostscript 10.06 and newer, and Firefox. Readers without support cannot show the content. Use it only when you control the reader. It is off while preserving PDF/A.

### linearize

Writes the file for fast web view, so a browser can show the first page before the whole file has downloaded. It adds a little size. It is skipped for signed documents (which come back untouched), skipped while preserving PDF/A, and not available in the WebAssembly build (`linearize is not available in the WebAssembly build`). If Hyper could not make the file smaller, it returns the original unlinearized, with the warning `linearize not applied: the optimized file was not smaller than the original, so the original was kept`.

## 6. Decide by need

| If you need | Set |
|---|---|
| The smallest file for screen reading | `--preset high` |
| Every image pixel unchanged | `--preset lossless` |
| Smaller images, but keep resolution | lower `imageQuality`, for example 35 |
| Sharp print output | `--preset low`, or `maxDpi` 300 |
| Readable scanned text at a smaller size | `maxDpiMono` 300, or see [Scanned documents](/guides/scanned-documents) |
| A file under a fixed size | `targetSizeBytes`; see [Hit a file size target](/guides/size-targets) |
| Faster runs on large batches | `speed` `fast` |
| The last few percent, and time does not matter | `speed` `quality` |
| Output for a tool without JPEG 2000 support | `preferJpx` `false` |
| Exact page geometry and colours | `contentPrecision` 0 |
| First page visible while downloading | `linearize` `true` |
| A smaller file for a reader you control | `brotli` `true` |
| A PDF/A file that stays PDF/A | `preserveConformance` `true`; see [PDF/A documents](/guides/pdfa) |
| One colour model for print | `colorTarget`; see [Colour and print](/guides/colour-and-print) |

## Troubleshooting

**An option seems to do nothing.** Check the spelling against the [options reference](/options); unknown names are ignored. Then check the value's type with `resolveOptions`: in the CLI, `--set subsetFonts=yes` is text and turns the option off.

**`400 bad_options` from the HTTP API.** The `options` value is not valid JSON, is not an object, or is longer than 4096 characters. Let curl encode it with `--url-query`.

**The file did not get smaller.** Hyper returned your original bytes because it could not do better with these settings. Try `--preset high`, or a [size target](/guides/size-targets).

## Next steps

- [Options reference](/options): every option, range and preset value.
- [Hit a file size target](/guides/size-targets): let Hyper find the quality for you.
- [Scanned documents](/guides/scanned-documents): MRC, JBIG2 and rasterizing for scans.
- [Warnings](/warnings): what each warning means and what to do.
