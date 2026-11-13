---
title: WebAssembly
description: Reference for the hyper-compress-wasm package, which runs Hyper Compress in the browser and on any Node.js 22+ platform.
---

# WebAssembly

The `hyper-compress-wasm` package is Hyper Compress compiled to WebAssembly. It takes the PDF as bytes and returns the compressed bytes, with no native files to install, so it runs in any modern browser and on any platform Node.js 22 or newer runs on.

```bash
npm install hyper-compress-wasm
```

```js
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input);

await writeFile('output.pdf', result.data);
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

Save it as `compress.mjs` and run `node compress.mjs`. The package is ESM only.

Use it where the [native Node.js SDK](/node-sdk) does not install (Intel macOS, Alpine Linux, FreeBSD) and in the browser. On the native platforms, prefer the Node.js SDK: it adds a report, cancellation, timeouts and process isolation. [Platforms and surfaces](/platforms) compares them.

## compress(input, opts?)

```ts
function compress(input: Uint8Array, opts?: HyperWasmOptions): Promise<HyperWasmResult>
```

Compresses one PDF in memory. The first call loads the engine module, which takes a moment; later calls reuse it. A Node.js `Buffer` works as `input`.

### Parameters

`input` is the PDF as a `Uint8Array`. `opts` is a `HyperWasmOptions` object:

| Name | Type | Default | Description |
|---|---|---|---|
| `preset` | `'low' \| 'medium' \| 'high' \| 'lossless'` | `'medium'` | The starting set of [options](/options). An unknown name means `'medium'`. |
| `options` | `Partial<HyperCompressOptions>` | `{}` | Overrides on top of the preset. Out-of-range numbers are clamped, wrong types and unknown choices become the option's fallback default, unknown keys are ignored. |
| `password` | `string \| null` | `null` | Password for an encrypted input. Empty means none. Only the text before the first line break is used. |
| `targetSizeBytes` | `number` | none | Aim for an output at or below this many bytes. Rounded down; 0 or less means no target. |

### Returns

`Promise<HyperWasmResult>`:

| Name | Type | Description |
|---|---|---|
| `data` | `Uint8Array` | The output PDF. When Hyper keeps your original, this can be the same `Uint8Array` you passed in, not a copy. |
| `originalSize` | `number` | Input size in bytes. |
| `compressedSize` | `number` | Output size in bytes. Never larger than `originalSize`. |
| `signed` | `boolean` | `true` when the input is digitally signed and was returned unchanged, byte for byte. Also `true` when you set `removeSignatures` but the compressed file was not smaller, so the signed original came back. |
| `signaturesRemoved` | `boolean` | `true` when `removeSignatures` removed signatures and the compressed file was used. |
| `pdfa` | `PdfaLevel \| null` | The PDF/A level the input claims, for example `{ part: 2, conformance: 'b' }`, or `null`. |
| `pdfaOutcome` | `'none' \| 'preserved' \| 'claim-withdrawn'` | What happened to a PDF/A claim. See below. |
| `metTarget` | `boolean \| null` | Whether the output is at or below `targetSizeBytes`. `null` when no target was set. |
| `warnings` | `string[]` | Everything Hyper skipped and why. See [Warnings](/warnings). |

With a password, a compressed `data` is decrypted. When Hyper keeps the original (nothing was smaller, or the file is signed), `data` is your original bytes, still encrypted.

### pdfaOutcome

PDF/A is the archiving version of PDF. A PDF/A file carries a claim in its metadata that it meets the standard.

| Value | Meaning |
|---|---|
| `none` | The input does not claim PDF/A. |
| `preserved` | The output still carries the claim and meets it: `preserveConformance` was on, the input is signed, or the original was returned. That includes the rare case where the claim could not be removed from the compressed file; you then also get a warning. |
| `claim-withdrawn` | Hyper changed the file without `preserveConformance`, so it removed the PDF/A claim from the metadata. |

See [PDF/A archives](/guides/pdfa).

### Example

```js
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, {
  preset: 'high',
  password: 'hunter2',
  targetSizeBytes: 1024 * 1024,
});

await writeFile('output.pdf', result.data);
console.log(result.metTarget, result.warnings);
```

### Errors

`compress` rejects with a `HyperError`:

| Code | Message | When |
|---|---|---|
| `decrypt_failed` | `wrong or missing password` | The password is wrong. |
| `decrypt_failed` | `the PDF is encrypted and needs a password` | The PDF needs a password and none was given. A file with only an owner password compresses without one. |
| `engine_error` | `the input is not a PDF: no %PDF- header in its first 1024 bytes` | The input is not a PDF. |
| `engine_error` | `wasm module failed to load: <reason>` | The engine module could not be loaded. The next call tries again. |
| `engine_error` | the underlying error's message | Anything else went wrong inside the engine, for example running out of memory. |

See [Errors](/errors) for every surface.

## verifyPassword(input, password)

```ts
function verifyPassword(input: Uint8Array, password: string): Promise<boolean>
```

Checks a password without compressing. It runs the same decryption as `compress`, so `true` means `compress` accepts the same password. Returns `true` for a PDF that is not encrypted, and `false` for input that is not a PDF.

```js
import { readFile } from 'node:fs/promises';
import { verifyPassword } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
console.log(await verifyPassword(input, 'hunter2'));
```

Rejects with `engine_error` when the engine module cannot be loaded.

## createEngine(load)

```ts
function createEngine(load: () => Promise<HyperWasmModule>): HyperWasmEngine

interface HyperWasmEngine {
  init(): Promise<void>;
  compress(input: Uint8Array, opts?: HyperWasmOptions): Promise<HyperWasmResult>;
  verifyPassword(input: Uint8Array, password: string): Promise<boolean>;
}
```

Creates an engine from a module loader you supply. The package's own `compress` and `verifyPassword` are one such engine, built with the bundled module. Use `createEngine` when you host the engine files yourself, which is how you run Hyper in a browser.

- `load` is called the first time the engine is used, and its module is shared by every later call on that engine.
- If `load` fails, the call rejects with `engine_error` and the next call calls `load` again.
- `init()` loads the module now, so the first compression does not wait. It rejects with `engine_error` if loading fails.

## In the browser

The package's entry point is meant for Node.js and bundlers, and it exports only `.` (the root). To run Hyper in a browser page or worker, copy these folders from `node_modules/hyper-compress-wasm/` to your site, keeping their layout:

```text
engine/hyper-compress.js
engine/hyper-compress.wasm
lib/wasm/sdk.js
lib/wasm/glue.js
lib/sdk/node/*.js
```

Then load them from a module worker, so compression does not freeze the page:

```js
// hyper-worker.js, started with new Worker('./hyper-worker.js', { type: 'module' })
import createModule from './engine/hyper-compress.js';
import { createEngine } from './lib/wasm/sdk.js';

const engine = createEngine(() => createModule());

self.onmessage = async (e) => {
  try {
    const result = await engine.compress(new Uint8Array(e.data.bytes), e.data.opts);
    self.postMessage({ ok: true, result }, [result.data.buffer]);
  } catch (err) {
    self.postMessage({ ok: false, code: err.code, message: err.message });
  }
};

engine.init().catch(() => {});
```

```js
// main page
const worker = new Worker('./hyper-worker.js', { type: 'module' });
worker.onmessage = (e) => {
  if (!e.data.ok) return console.error(e.data.code, e.data.message);
  const blob = new Blob([e.data.result.data], { type: 'application/pdf' });
  console.log('compressed to', e.data.result.compressedSize, 'bytes', URL.createObjectURL(blob));
};

document.querySelector('input[type=file]').addEventListener('change', async (e) => {
  const bytes = await e.target.files[0].arrayBuffer();
  worker.postMessage({ bytes, opts: { preset: 'medium' } }, [bytes]);
});
```

`engine/hyper-compress.js` finds `hyper-compress.wasm` next to itself, so keep the two files together. Serve `.wasm` files with the `application/wasm` content type. The file never leaves the visitor's machine. [hyper.bentopdf.com](https://hyper.bentopdf.com) works this way; its source is in the repository's [`web/`](https://github.com/alam00000/bentopdf-hyper-compress/tree/main/web) folder, with the worker in [`web/worker.js`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/web/worker.js).

## Worker threads in Node.js

Compression runs synchronously once the module is loaded, so a large file blocks the event loop for as long as it takes. In a server, run it in a worker thread:

```js
// worker.mjs
import { parentPort, workerData } from 'node:worker_threads';
import { compress } from 'hyper-compress-wasm';

const result = await compress(new Uint8Array(workerData.input), workerData.opts);
parentPort.postMessage(result, [result.data.buffer]);
```

```js
// main.mjs
import { Worker } from 'node:worker_threads';
import { readFile, writeFile } from 'node:fs/promises';

const input = await readFile('input.pdf');
const worker = new Worker(new URL('./worker.mjs', import.meta.url), {
  workerData: { input, opts: { preset: 'medium' } },
});
worker.on('message', async (result) => {
  await writeFile('output.pdf', result.data);
  console.log('compressed to', result.compressedSize, 'bytes');
});
worker.on('error', (err) => console.error(err));
```

This keeps your server responsive. A crash in the engine ends the worker thread, which you see as an `error` event, not your process. To stop a run that takes too long, call `worker.terminate()`: that is the only way to interrupt a WebAssembly compression.

## Other exports

```js
import {
  HYPER_PRESETS,
  normalizeHyperOptions,
  buildHyperTokens,
  HyperError,
  createEngine,
} from 'hyper-compress-wasm';
```

| Export | Description |
|---|---|
| `HYPER_PRESETS` | The four presets as complete option objects. Same as in the [Node.js SDK](/node-sdk#hyper-presets). |
| `normalizeHyperOptions(raw)` | Checks and completes an options object. Same as in the [Node.js SDK](/node-sdk#normalizehyperoptions-raw). |
| `buildHyperTokens(options)` | The engine's `id=value` tokens for complete options. Same as in the [Node.js SDK](/node-sdk#buildhypertokens-options). |
| `HyperError` | The error class, with `code` and `message`. |
| `createEngine(load)` | See [createEngine](#createengine-load). |

There is no `resolveOptions` and no `HYPER_OPTION_DOCS` in this package.

Types:

| Type | Description |
|---|---|
| `HyperWasmOptions` | The `opts` parameter of `compress`. |
| `HyperWasmResult` | The result of `compress`. |
| `HyperWasmEngine` | What `createEngine` returns. |
| `HyperWasmModule` | The loaded engine module that `load` resolves to. |
| `HyperCompressOptions` | Every option, all required. See [Options](/options). |
| `CompressLevel` | `'low' \| 'medium' \| 'high' \| 'lossless'` |
| `PdfaLevel` | `{ part: number; conformance: string }`, `conformance` in lower case. |
| `HyperErrorCode` | `'decrypt_failed' \| 'engine_error' \| 'timeout' \| 'cancelled'`. This package only produces the first two. |

## What it does not do

Compared with the [Node.js SDK](/node-sdk):

| Missing | What happens |
|---|---|
| Linearization (fast web view) | `linearize` is ignored, with the warning `linearize is not available in the WebAssembly build`. |
| The [compression report](/report) | There is no `report` option. |
| Cancellation and timeouts | A run cannot be interrupted except by ending its worker. |
| Process isolation | The engine runs inside your process or worker. |
| Some warnings | The rasterize refusal and the "not applied" warnings for `colorTarget` and `linearize` are native only. See [Warnings](/warnings). |
| Large files | Input, working copies and output share one memory space of at most 4 GB. |
| Multiple threads | Each compression runs on one thread. |

Compressed output is otherwise the same: the same presets, options and size target search, signed files returned unchanged, and output never larger than the input.

## Troubleshooting

**Out of memory on very large files.** The engine holds the input, its working copies and the output in at most 4 GB of memory. For files in the hundreds of megabytes, use the [Node.js SDK](/node-sdk), which works on files on disk.

**The event loop stalls during compression.** Expected; use the worker thread pattern above.

**`engine_error: wasm module failed to load` in the browser.** Check that `hyper-compress.wasm` is served next to `hyper-compress.js` with the `application/wasm` content type, and that the page loads the worker as a module (`{ type: 'module' }`).

**You need cancellation or hard time limits.** Run each compression in its own worker and terminate it, or use the [Node.js SDK](/node-sdk).

## Next steps

- [Options](/options): every option, range and default.
- [Warnings](/warnings): every warning string.
- [Errors](/errors): every error, across all surfaces.
- [Running in production](/guides/production): limits and untrusted uploads.
