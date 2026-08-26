# hyper-compress-wasm

[Hyper Compress](https://github.com/alam00000/bentopdf-hyper-compress) is a high fidelity, content preserving PDF compression engine which maintains PDF conformance.

## Native or WebAssembly

The same engine ships two ways. [`hyper-compress`](https://www.npmjs.com/package/hyper-compress) contains prebuilt native binaries.


Pick native on a supported platform, especially for servers compressing untrusted uploads at volume. Pick wasm for portability (Intel macOS, ARM Linux, FreeBSD, Alpine), when you cannot ship native binaries, or when you work with buffers rather than files.

## Install

```bash
npm install hyper-compress-wasm
```

Node 22 or newer, ESM only. No postinstall scripts, no downloads: the WebAssembly module ships inside the package.

## Quick start

```js
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { preset: 'medium' });

await writeFile('output.pdf', result.data);
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

## API

### compress(input, opts?)

Compresses one PDF held in a `Uint8Array`. Returns a `Promise<HyperWasmResult>`.

```ts
interface HyperWasmOptions {
  preset?: 'low' | 'medium' | 'high' | 'lossless'; // default 'medium'
  options?: Partial<HyperCompressOptions>;         // overrides applied on top of the preset
  password?: string | null;                        // password for encrypted input
  targetSizeBytes?: number;                        // aim for a size; see metTarget on the result
}

interface HyperWasmResult {
  data: Uint8Array;          // the output PDF, never larger than the input
  originalSize: number;
  compressedSize: number;
  signed: boolean;           // true when the input was digitally signed and returned untouched
  pdfa: PdfaLevel | null;    // the input's PDF/A claim, e.g. { part: 2, conformance: 'b' }
  pdfaOutcome: 'none' | 'preserved' | 'claim-withdrawn';
  metTarget: boolean | null; // whether targetSizeBytes was met; null when no target was set
  warnings: string[];        // options requested but skipped, e.g. rasterize dropped to keep PDF/A
}
```


Errors are thrown as `HyperError` with a typed `code`: `decrypt_failed` for a wrong or missing password, `engine_error` for anything else.

`HyperCompressOptions` and its presets are identical to the native package; every option is documented in the [`hyper-compress` README](https://www.npmjs.com/package/hyper-compress).

### verifyPassword(input, password)

Checks a password against an encrypted PDF without compressing it. Returns `Promise<boolean>`. A `true` here means `compress()` will accept the same password.

### Helpers

`HYPER_PRESETS`, `normalizeHyperOptions` and `buildHyperTokens` are re-exported unchanged from the native SDK.

## Blocking and worker threads

Compression runs synchronously inside your process once the module is loaded; a large document can hold the event loop for several seconds. Its recommended to run it inside a `worker_thread`:

```js
// worker.js
import { parentPort, workerData } from 'node:worker_threads';
import { compress } from 'hyper-compress-wasm';

const result = await compress(new Uint8Array(workerData.input), workerData.opts);
parentPort.postMessage(result, [result.data.buffer]);
```

This also restores crash isolation: if hostile input takes down the engine, it takes down the worker thread, not your server. If you want that managed for you, with subprocess isolation, timeouts and `AbortSignal` cancellation built in, use the native package.

## License

AGPL-3.0-only. For use in proprietary products, a commercial license is available through [BentoPDF](https://bentopdf.com/licensing.html). Bundled third-party components keep their own licenses; see [NOTICE.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/NOTICE.md).
