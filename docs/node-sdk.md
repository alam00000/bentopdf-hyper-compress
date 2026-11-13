---
title: Node.js SDK
description: Reference for the hyper-compress npm package, with every function, parameter, result field, error and type.
---

# Node.js SDK

The `hyper-compress` package compresses PDFs from Node.js with Hyper Compress. It is a thin TypeScript layer over Hyper's Rust core, which is loaded as a native addon and runs the engine in a separate process.

```bash
npm install hyper-compress
```

```js
import { compress } from 'hyper-compress';

const result = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf' });
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

The package is ESM only and needs Node.js 22 or newer. Save samples as `.mjs`, or set `"type": "module"` in your `package.json`.

## Install and platforms

`npm install hyper-compress` also installs one platform package, the one that matches your machine:

| Platform package | Platform |
|---|---|
| `hyper-compress-darwin-arm64` | macOS on Apple silicon |
| `hyper-compress-linux-x64` | Linux x64 with glibc (npm skips it on musl systems such as Alpine) |
| `hyper-compress-linux-arm64` | Linux ARM64 with glibc (npm skips it on musl systems such as Alpine) |
| `hyper-compress-win32-x64` | Windows x64 |

Each platform package holds four files in its `bin/` folder:

| File | What it is |
|---|---|
| `hyper-compress.node` | The native addon: Hyper's Rust core (decryption, the engine pass, repacking, the size target search, PDF/A handling, the report). |
| `hpdf-worker` | The compression engine. Runs in its own process for every compression, so a broken file cannot crash your app. |
| `hpdf-render` | A small tool that renders the first page of a PDF to an image, for checking results by eye. The SDK does not use it. |
| `qpdf` | Used for decryption, repacking, linearization, the report and PDF/A claim removal. |

There are no install scripts and no downloads. On other platforms (Intel macOS, Alpine Linux, FreeBSD), use the [WebAssembly package](/wasm), or [build the engine](/building) and point the environment variables below at it.

### How Hyper finds the engine

For each file, the SDK uses the first match:

| File | 1. Environment variable | 2. | 3. Repository checkout | 4. Platform package |
|---|---|---|---|---|
| `hyper-compress.node` | `HYPER_ADDON` | `hyper-compress.node` in the folder of `HYPER_DRV`, if that file exists | `cli/prebuilt/` | `bin/` |
| `hpdf-worker` | `HYPER_DRV` | | `cli/prebuilt/` | `bin/` |
| `qpdf` | `HYPER_QPDF` | | `cli/prebuilt/` | `bin/` |

- An environment variable is used as given. If `HYPER_DRV` or `HYPER_QPDF` points to a file that does not exist, `compress` throws `engine_error` (`hpdf-worker not found at <path>` or `qpdf not found at <path>`). A wrong `HYPER_ADDON` throws the Node.js loader's error.
- `cli/prebuilt/` is searched in the package folder and up to 8 parent folders, and counts only if it holds `hpdf-worker`.
- On Windows the engine files end in `.exe`.

If no addon or no `hpdf-worker` is found, `compress` throws `engine_error` with a message that names your platform, for example `linux-x64`. When no `qpdf` is found at all, compression still runs but skips the steps that need it; see [Troubleshooting](#troubleshooting).

## compress(input)

```ts
function compress(input: CompressInput): Promise<CompressResult>
```

Compresses one PDF from a file path to a file path. The work runs off the main thread, so the event loop stays free.

### Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `sourcePath` | `string` | required | Path to the input PDF. Empty or missing throws `engine_error` (`sourcePath is required`). |
| `savePath` | `string` | `<name>-compressed.<ext>` beside the input | Where to write the output. The input's extension is kept, or `.pdf` is used if it has none. It can be the input path: the result is written to a temporary file and renamed into place, and when Hyper keeps the original the file is left as it is. |
| `preset` | `'low' \| 'medium' \| 'high' \| 'lossless'` | `'medium'` | The starting set of [options](/options). An unknown name means `'medium'`. |
| `options` | `Partial<HyperCompressOptions>` | `{}` | Overrides on top of the preset. See [Options handling](#options-handling). |
| `password` | `string \| null` | `null` | Password for an encrypted input. Empty means none. Only the text before the first line break is used. |
| `targetSizeBytes` | `number` | none | Aim for an output at or below this many bytes. Rounded down; 0 or less means no target. |
| `report` | `boolean \| string` | `false` | `true` adds a [report](/report) to the result. A path also writes it to that file as JSON. |
| `signal` | `AbortSignal` | none | Cancels the call. See [Cancellation and timeouts](#cancellation-and-timeouts). |
| `timeoutMs` | `number` | `600000` (10 minutes) | Limit for each step that runs a separate process. A step that runs longer fails the call with `timeout`. 0 or less means the default. |
| `totalTimeoutMs` | `number` | no limit | Limit for the whole call. 0 or less means no limit. |

### Returns

`Promise<CompressResult>`:

| Name | Type | Description |
|---|---|---|
| `outputPath` | `string` | Where the output was written: `savePath` as given, or the default path. |
| `originalSize` | `number` | Input size in bytes. |
| `compressedSize` | `number` | Output size in bytes. Never larger than `originalSize`. Equal means Hyper kept the original. |
| `signed` | `boolean` | `true` when the input is digitally signed and was returned unchanged, byte for byte, so the signature stays valid. Also `true` when you set `removeSignatures` but the compressed file was not smaller, so the signed original came back. |
| `signaturesRemoved` | `boolean` | `true` when `removeSignatures` removed signatures and the compressed file was used. |
| `pdfa` | `PdfaLevel \| null` | The PDF/A (archiving PDF) level the input claims, for example `{ part: 2, conformance: 'b' }`, or `null`. |
| `pdfaPreserved` | `boolean` | `true` when the output still carries the input's PDF/A claim and meets it: `preserveConformance` was on, or the original was returned. |
| `metTarget` | `boolean \| null` | Whether the output is at or below `targetSizeBytes`. `null` when no target was set. |
| `warnings` | `string[]` | Everything Hyper skipped and why. Empty means every requested option was applied. See [Warnings](/warnings). |
| `report` | `HyperReport` | Only present when `report` was requested and could be built. See [Compression report](/report). |

With a password, a compressed output is decrypted. When Hyper keeps the original (nothing was smaller, or the file is signed), you get your original bytes, still encrypted. A file with only an owner password compresses without `password`, and the output keeps that owner password.

### Example

```js
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'high',
  options: { grayscale: true },
  targetSizeBytes: 1024 * 1024,
});

const pct = Math.round((1 - result.compressedSize / result.originalSize) * 100);
console.log(`${result.outputPath}: ${pct}% smaller, target met: ${result.metTarget}`);
for (const warning of result.warnings) console.warn('warning:', warning);
```

```text
output.pdf: 66% smaller, target met: true
```

### Errors

`compress` rejects with a [`HyperError`](#hypererror):

| Code | When |
|---|---|
| `decrypt_failed` | The password is wrong, or the PDF needs a password and none was given (`the PDF is encrypted and needs a password`). |
| `engine_error` | No engine for this platform, an engine path that does not exist, `sourcePath` is empty, the input cannot be read or is not a PDF, `password` is set but no `qpdf` was found, or the output or report file cannot be written. |
| `timeout` | One step ran longer than `timeoutMs`, or the whole call ran longer than `totalTimeoutMs`. |
| `cancelled` | `signal` was aborted. |

When the call fails, no output file is written. See [Errors](/errors) for every message.

### What does not throw

These are outcomes, reported in the result, not errors:

- The file could not be made smaller: you get the original bytes and `compressedSize === originalSize`.
- The input is signed: `signed: true`, original returned.
- The size target was missed: `metTarget: false`, the smallest file Hyper made, and a warning.
- An option was unsafe for this file: a warning.

## verifyPassword(sourcePath, password, timeoutMs?)

```ts
function verifyPassword(sourcePath: string, password: string, timeoutMs?: number): Promise<boolean>
```

Checks a password without compressing anything. It runs the same decryption step as `compress`, so `true` means `compress` accepts the same password.

| Name | Type | Default | Description |
|---|---|---|---|
| `sourcePath` | `string` | required | Path to the PDF. |
| `password` | `string` | required | The password to check. Only the text before the first line break is used. |
| `timeoutMs` | `number` | `60000` | Limit for the check. 0 or less means the default. |

Returns `true` when the password opens the file, and also for a PDF that is not encrypted. Returns `false` for a wrong password and for a file that exists but is not a PDF.

```js
import { compress, verifyPassword } from 'hyper-compress';

const password = 'hunter2';
if (await verifyPassword('input.pdf', password)) {
  await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', password });
} else {
  console.log('wrong password');
}
```

Throws `engine_error` when the file does not exist (`cannot read <path>`) or the addon or `qpdf` cannot be found, and `timeout` when the check runs past `timeoutMs` (`decryption ran longer than the <n> ms stage limit`).

## resolveOptions(input)

```ts
function resolveOptions(input: CompressInput): HyperCompressOptions
```

Returns the complete options a `compress` call starts from: the preset (default `medium`) with `input.options` on top, checked and completed. Only `preset` and `options` are read, so `sourcePath` can be any string. Runs in JavaScript, without loading the engine.

```js
import { resolveOptions } from 'hyper-compress';

const o = resolveOptions({ sourcePath: '', preset: 'high', options: { imageQuality: 500 } });
console.log(o.imageQuality, o.maxDpi);
```

```text
100 72
```

For a PDF/A input with `preserveConformance`, `compress` then switches off the options that would break the standard. `resolveOptions` does not know the file, so it does not show that step.

## normalizeHyperOptions(raw)

```ts
function normalizeHyperOptions(raw: unknown): HyperCompressOptions
```

Checks and completes an options object without a preset:

- numbers are rounded and clamped to the option's range;
- a value of the wrong type, or an unknown choice, becomes the option's fallback default;
- missing options get their fallback default;
- unknown keys are dropped;
- `removeAnnots: true` with `annotations: 'keep'` becomes `annotations: 'flatten'`.

The fallback defaults are the `default` values in [`HYPER_OPTION_DOCS`](#hyper-option-docs) and the `Default` column of the [options reference](/options). They are not the `medium` preset: `normalizeHyperOptions({})` has `imageQuality` 80 and `reduceColor` off. To start from a preset, use [`resolveOptions`](#resolveoptions-input) or spread a preset first:

```js
import { HYPER_PRESETS, normalizeHyperOptions } from 'hyper-compress';

const o = normalizeHyperOptions({ ...HYPER_PRESETS.medium, grayscale: true });
```

## buildHyperTokens(options)

```ts
function buildHyperTokens(options: HyperCompressOptions): string[]
```

Translates complete options into the engine's `id=value` tokens, the same tokens `compress` passes to `hpdf-worker`. Pass the output of `normalizeHyperOptions` or `resolveOptions`. Use it to debug, or to drive the [C API](/c-api) and the worker binary yourself.

```js
import { HYPER_PRESETS, buildHyperTokens } from 'hyper-compress';

console.log(buildHyperTokens(HYPER_PRESETS.medium).join(' '));
```

```text
1=150 2=50 3=4 8=1 18=1 7=1 63=10 6=1 10=1 13=1 14=1 16=1 17=1 20=8322 67=6
```

## HYPER_PRESETS

```ts
const HYPER_PRESETS: Record<CompressLevel, HyperCompressOptions>
```

The four presets as complete option objects, keyed `low`, `medium`, `high` and `lossless`. Every key of `HyperCompressOptions` is present. The values are listed per option in the [options reference](/options).

## HYPER_OPTION_DOCS

```ts
const HYPER_OPTION_DOCS: Record<keyof HyperCompressOptions, HyperOptionDoc>
```

Machine-readable documentation for every option, the source of the [options reference](/options). Use it to build a settings screen.

| Field of `HyperOptionDoc` | Type | Description |
|---|---|---|
| `group` | `HyperOptionGroup` | `'Images'`, `'Rasterization'`, `'Fonts'`, `'Structure and interactivity'`, `'Metadata and cleanup'` or `'Output format'`. |
| `type` | `'boolean' \| 'number' \| 'enum'` | The kind of value. |
| `values` | `readonly string[]` | For `enum`: the allowed values. |
| `min`, `max` | `number` | For `number`: the allowed range. |
| `default` | `boolean \| number \| string` | The fallback default used by `normalizeHyperOptions`. |
| `description` | `string` | What the option does, in plain words. |

```js
import { HYPER_OPTION_DOCS } from 'hyper-compress';

const { min, max, description } = HYPER_OPTION_DOCS.imageQuality;
console.log(min, max, description);
```

## HyperError

```ts
class HyperError extends Error {
  readonly code: HyperErrorCode;
  constructor(code: HyperErrorCode, message?: string);
}
```

Every error the SDK creates is a `HyperError`. `name` is `'HyperError'`. `message` adds detail when there is any, and is the code itself otherwise.

| `code` | Meaning |
|---|---|
| `decrypt_failed` | Wrong password, or a password is needed and none was given. |
| `engine_error` | No engine for this platform, an engine path that does not exist, an empty `sourcePath`, input that is not a PDF, a file that cannot be read or written, or another failure inside the engine. |
| `timeout` | The whole call ran past `totalTimeoutMs`. |
| `cancelled` | The `AbortSignal` fired. |

```js
import { compress, HyperError } from 'hyper-compress';

try {
  await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', password: 'hunter2' });
} catch (e) {
  if (e instanceof HyperError) {
    console.error(e.code, e.message);
  } else {
    throw e;
  }
}
```

An addon file that exists but cannot be loaded (for example one built for another platform) throws the Node.js loader's own error, not a `HyperError`.

## Cancellation and timeouts

### Cancel with an AbortSignal

```js
import { compress } from 'hyper-compress';

const controller = new AbortController();
setTimeout(() => controller.abort(), 5000);

try {
  await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', signal: controller.signal });
} catch (e) {
  if (e.code === 'cancelled') console.log('stopped after 5 seconds');
  else throw e;
}
```

- An already aborted signal rejects at once, before any work.
- Aborting during a run stops the running process within a few milliseconds, and the call rejects. No output file is written.
- If the abort reason is a `HyperError`, the call rejects with that error. Otherwise it rejects with `HyperError('cancelled')`. So `controller.abort(new HyperError('timeout', 'took too long'))` gives you `code: 'timeout'`, while `AbortSignal.timeout(5000)` gives `code: 'cancelled'`.

### Timeouts

| Setting | Limits | When exceeded |
|---|---|---|
| `timeoutMs` (default 10 minutes) | Each separate process: decryption, the engine pass, each repack, each size target attempt, each report read. | The process is stopped and the call rejects with `timeout`, with a message such as `the engine pass ran longer than the 600000 ms stage limit` (or `decryption ...`, `qpdf ...`). |
| `totalTimeoutMs` (default none) | The whole call, including every size target attempt. | The call rejects with `timeout`. |

Use `totalTimeoutMs` when you need a hard upper bound on how long a call takes. A size target can run the engine up to 11 times (the first pass, up to 7 quality steps and 3 resolution steps), each with its own `timeoutMs`. See [Errors, timeouts and cancellation](/guides/errors-and-cancellation).

## Parallel use

Each `compress` and `verifyPassword` call runs on its own thread, not on Node.js's libuv thread pool, so `UV_THREADPOOL_SIZE` does not limit them and they do not block your file system calls. Each call also starts its own engine process, which uses up to 8 threads, so the number of CPU cores is the real limit. Run about one call per core:

```js
import { readdir } from 'node:fs/promises';
import { availableParallelism } from 'node:os';
import { compress } from 'hyper-compress';

const files = (await readdir('.')).filter((name) => name.endsWith('.pdf') && !name.includes('-compressed'));
const limit = availableParallelism();
for (let i = 0; i < files.length; i += limit) {
  const batch = files.slice(i, i + limit);
  const results = await Promise.all(batch.map((name) => compress({ sourcePath: name })));
  for (const r of results) console.log(r.outputPath, r.compressedSize);
}
```

Calls do not share state, and each uses its own temporary folder in the system temp directory, so the temp directory needs room for a few copies of each file. [Running in production](/guides/production) covers sizing.

## Options handling

`options` is applied on top of the preset in the Rust core, with the same rules as [`normalizeHyperOptions`](#normalizehyperoptions-raw):

- a number outside the range is clamped (`imageQuality: 500` runs as 100);
- a value of the wrong type or an unknown choice becomes the fallback default, which may differ from the preset's value;
- unknown keys are ignored.

A bad option never fails a run. Call [`resolveOptions`](#resolveoptions-input) to see exactly what will run.

## Types

```ts
import type {
  CompressInput,
  CompressResult,
  CompressLevel,
  HyperCompressOptions,
  HyperErrorCode,
  HyperOptionDoc,
  HyperOptionGroup,
  PdfaLevel,
  HyperReport,
  HyperFontReport,
  HyperImageReport,
  HyperImageState,
  HyperCleanupReport,
} from 'hyper-compress';
```

| Type | Definition |
|---|---|
| `CompressInput` | The parameter of [`compress`](#compress-input). |
| `CompressResult` | The result of [`compress`](#returns). |
| `CompressLevel` | `'low' \| 'medium' \| 'high' \| 'lossless'` |
| `HyperCompressOptions` | Every option, all required. Fields and ranges are in the [options reference](/options). |
| `HyperErrorCode` | `'decrypt_failed' \| 'engine_error' \| 'timeout' \| 'cancelled'` |
| `HyperOptionDoc` | One entry of [`HYPER_OPTION_DOCS`](#hyper-option-docs). |
| `HyperOptionGroup` | The `group` names listed under [`HYPER_OPTION_DOCS`](#hyper-option-docs). |
| `PdfaLevel` | `{ part: number; conformance: string }`. `conformance` is lower case, and empty for levels without a letter, such as PDF/A-4. |
| `HyperReport` | The [compression report](/report): sizes, `fonts`, `images` and `cleanup`. |
| `HyperFontReport` | One entry of `HyperReport['fonts']`. |
| `HyperImageReport` | One entry of `HyperReport['images']`. |
| `HyperImageState` | The `before` and `after` of an image entry. |
| `HyperCleanupReport` | One entry of `HyperReport['cleanup']`. |

The package ships its own declarations and is built with TypeScript's strictest settings, including `exactOptionalPropertyTypes`.

## Recipes

| Task | Guide |
|---|---|
| Compress encrypted files | [Encrypted PDFs](/guides/encrypted-pdfs) |
| Hit a size limit | [Hit a file size target](/guides/size-targets) |
| Keep signatures valid, or remove them | [Signed documents](/guides/signed-documents) |
| Keep PDF/A conformant | [PDF/A archives](/guides/pdfa) |
| Shrink scans | [Scanned documents](/guides/scanned-documents) |
| Handle errors and limits in a service | [Errors, timeouts and cancellation](/guides/errors-and-cancellation) |

## Troubleshooting

**`SyntaxError: Cannot use import statement outside a module`.** Node.js treats your file as CommonJS. Name it `.mjs`, or add `"type": "module"` to your `package.json`.

**`HyperError: no hpdf-worker binary for <platform>`.** No platform package for your machine was installed. Check that you are on a supported platform and that `npm install` did not skip optional dependencies (`--omit=optional` or `--no-optional`). Otherwise use the [WebAssembly package](/wasm), or [build the engine](/building) and set `HYPER_DRV`.

**`HyperError: no qpdf binary for <platform>` when you pass a password.** `qpdf` was not found. Set `HYPER_QPDF` to a `qpdf` program.

**`report unavailable: the PDF structure could not be read`, and files compress less than expected.** No `qpdf` was found. Without it, Hyper skips the repack and cannot read the file for the report.

**`HyperError: hpdf-worker not found at <path>` or `qpdf not found at <path>`.** `HYPER_DRV` or `HYPER_QPDF` points to a file that does not exist. Fix the path, or unset the variable.

**`HyperError: the engine pass ran longer than the 600000 ms stage limit`.** One step took longer than `timeoutMs`. Raise `timeoutMs` for very large files, or use a lighter preset.

**`decrypt_failed` with what you believe is the right password.** Check with `verifyPassword`. If that returns `false` too, the file may have a different password than the one you have.

**It works locally but fails in Docker or CI.** Native packages exist for `linux/amd64` and `linux/arm64` with glibc. On Alpine (musl) npm skips them and you get `no hpdf-worker binary`; use the WebAssembly package there. Node.js in the image must be 22 or newer.

## Next steps

- [Options](/options): every option, range and default.
- [Compression report](/report): what `report: true` returns.
- [Warnings](/warnings): every warning string.
- [Errors](/errors): every error, across all surfaces.
- [Rust crate](/rust): the same core without Node.js.
