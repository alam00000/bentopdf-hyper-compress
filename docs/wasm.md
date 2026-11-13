# WebAssembly

```bash
npm install hyper-compress-wasm
```

The engine compiled to WebAssembly: byte-identical output to the native build, no native binaries, no postinstall downloads. It runs on any platform Node 22 or newer runs, which makes it the answer whenever the native package cannot install: Intel Macs, Alpine containers, FreeBSD.


## A complete first program

The API is buffer based. Save as `compress.mjs`:

```js
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { preset: 'medium' });

await writeFile('output.pdf', result.data);
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

Run with `node compress.mjs`. The first call loads the WebAssembly module (a few hundred milliseconds); it stays loaded for the life of the process, so subsequent compressions start instantly.

## compress(input, opts?)

```ts
interface HyperWasmOptions {
  preset?: 'low' | 'medium' | 'high' | 'lossless'; // default 'medium'
  options?: Partial<HyperCompressOptions>;         // overrides on top of the preset
  password?: string | null;                        // for encrypted input
  targetSizeBytes?: number;                        // aim for a size; see metTarget
}

interface HyperWasmResult {
  data: Uint8Array;              // the output PDF, never larger than the input
  originalSize: number;
  compressedSize: number;
  signed: boolean;               // input was digitally signed and returned untouched
  signaturesRemoved: boolean;  // removeSignatures stripped signatures so the document could be compressed
  pdfa: PdfaLevel | null;        // the input's PDF/A claim
  pdfaOutcome: 'none' | 'preserved' | 'claim-withdrawn';
  metTarget: boolean | null;     // null when no target was set
  warnings: string[];            // options requested but skipped, and why
}
```

The [guarantees](/guarantees) are identical to the native package: output never larger than input, signed documents untouched, PDF/A never left lying, and a size target searched to the engine's floor with the smallest achievable file returned on a miss. The [options](/options) are the same. Errors are thrown as `HyperError` with `code` `decrypt_failed` (wrong or missing password) or `engine_error` (anything else, including a module that failed to load).

`pdfaOutcome` tells you what happened to a PDF/A input:

| Value | Meaning |
|---|---|
| `none` | the input does not claim PDF/A |
| `preserved` | the output still meets the input's PDF/A level, because `preserveConformance` was on, the file is signed, or the original was returned |
| `claim-withdrawn` | the output was changed without `preserveConformance`, so its PDF/A claim was removed from the metadata |

### What the WebAssembly build does not do

Compared with the [native SDK](/node-sdk), three things are missing:

- `linearize` is ignored, with the warning `linearize is not available in the WebAssembly build`.
- There is no [compression report](/report).
- There is no cancellation and no timeout; see below.

The full comparison is on [platforms and surfaces](/platforms#what-each-surface-can-do).

## verifyPassword(input, password)

```js
import { verifyPassword } from 'hyper-compress-wasm';

const ok = await verifyPassword(bytes, 'hunter2');
```

Returns a `Promise<boolean>` without compressing. `true` means `compress()` will accept the same password. An unencrypted PDF returns `true` for any password.

## Loading the module yourself

The default exports load the bundled module the first time you call them. When you host the engine files yourself, as a browser build does, use `createEngine` with your own copy of `engine/hyper-compress.js`. This is what [`web/worker.js`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/web/worker.js) does:

```js
import createModule from './engine/hyper-compress.js';   // copied from the package
import { createEngine } from './lib/wasm/sdk.js';       // copied from the package

const engine = createEngine(() => createModule());
await engine.init();                       // load now instead of on first use
const result = await engine.compress(bytes, { preset: 'high' });
```

`createEngine(load)` takes a function that returns the module and gives back `{ init, compress, verifyPassword }`. The module loads once and is shared by every call on that engine; if loading fails, the next call tries again.

## Other exports

```js
import {
  HYPER_PRESETS,         // the four presets as full option objects
  normalizeHyperOptions, // clamp and complete a partial options object
  buildHyperTokens,      // the low-level engine tokens, for debugging
  HyperError,
} from 'hyper-compress-wasm';
```

Types: `HyperWasmEngine`, `HyperWasmModule`, `HyperWasmOptions`, `HyperWasmResult`, `HyperCompressOptions`, `CompressLevel`, `PdfaLevel` and `HyperErrorCode`.

## Blocking, and how to avoid it

Compression runs synchronously inside your process once the module is loaded; a large document can hold the Node event loop for several seconds. Fine in a script; in a server, run it inside a worker thread:

```js
// worker.js
import { parentPort, workerData } from 'node:worker_threads';
import { compress } from 'hyper-compress-wasm';

const result = await compress(new Uint8Array(workerData.input), workerData.opts);
parentPort.postMessage(result, [result.data.buffer]);
```

```js
// main.js
import { Worker } from 'node:worker_threads';
import { readFile } from 'node:fs/promises';

const input = await readFile('input.pdf');
const worker = new Worker('./worker.js', { workerData: { input, opts: { preset: 'medium' } } });
worker.on('message', (result) => {
  console.log('compressed to', result.compressedSize, 'bytes');
});
```

This keeps your server responsive and restores crash isolation: hostile input takes down the worker thread, not the process. If you want isolation, timeouts and cancellation managed for you, that is exactly what the [native package](/node-sdk) does with subprocesses.

## In the browser

The same engine powers [hyper.bentopdf.com](https://hyper.bentopdf.com), running in a Web Worker so visitors' files never leave their machines. The [repository's `web/` directory](https://github.com/alam00000/bentopdf-hyper-compress/tree/main/web) is the reference integration; this npm package's entry point targets Node, so for a browser build load `engine/hyper-compress.js` and the files under `lib/` from your own worker the way `web/worker.js` does.

## Troubleshooting

**Out of memory on very large files.** The wasm engine holds input, working set and output inside a 4 GB address space. For files in the hundreds of megabytes, prefer the native package, which streams through files on disk.

**The event loop stalls during compression.** Expected in-process; use the worker thread pattern above.

**You need `AbortSignal` cancellation or hard timeouts.** Those exist only in the native package; wasm compression cannot be preempted mid-run.
