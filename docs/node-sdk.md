# Node SDK

The programming interface behind the CLI. Node.js 22 or newer, ESM only.

```bash
npm install hyper-compress
```

The native engine binaries arrive automatically through a platform package (`hyper-compress-darwin-arm64`, `-linux-x64`, `-linux-arm64` or `-win32-x64`); npm installs the one matching your machine. There are no postinstall scripts. On platforms without a native package, use the [WebAssembly package](/wasm).

### Where the engine binaries come from

The SDK's pipeline (decryption, the engine pass, repacking, the size target search, PDF/A handling and the report) is written in Rust and loaded as a native addon, `hyper-compress.node`. It runs two binaries: `hpdf-worker` (the compression engine, in its own process so a bad file cannot crash yours) and `qpdf` (decryption, repacking and linearization). For each of the three files it takes the first that exists:

1. the path in an environment variable: `HYPER_ADDON` for the addon, `HYPER_DRV` for `hpdf-worker`, `HYPER_QPDF` for `qpdf`; when only `HYPER_DRV` is set, the addon is also looked for in the same folder
2. a `cli/prebuilt/` folder in the package or any of the 8 folders above it (a repository checkout)
3. the `bin/` folder of the platform package for your platform and CPU

If none exists, `compress` throws `engine_error` with a message naming your platform.

## A complete first program

Save this as `compress.mjs` next to any `input.pdf`:

```js
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'medium',
});

const pct = Math.round((1 - result.compressedSize / result.originalSize) * 100);
console.log(`${result.originalSize} -> ${result.compressedSize} bytes (${pct}% smaller)`);
for (const w of result.warnings) console.warn('skipped:', w);
```

Run it:

```bash
node compress.mjs
```

The `.mjs` extension matters: this package is ESM only. In a project, setting `"type": "module"` in your `package.json` lets you use plain `.js` files.

## compress(input)

Compresses one PDF. Returns a `Promise<CompressResult>` and never writes a partial file.

```ts
interface CompressInput {
  sourcePath: string;                              // path to the input PDF (required)
  savePath?: string;                               // default '<input>-compressed.pdf' beside the source
  preset?: 'low' | 'medium' | 'high' | 'lossless'; // default 'medium'
  options?: Partial<HyperCompressOptions>;         // overrides applied on top of the preset
  password?: string | null;                        // password for encrypted input
  targetSizeBytes?: number;                        // aim for a size; see metTarget
  report?: boolean | string;                       // true, or a path to also write it as JSON
  signal?: AbortSignal;                            // cancel the run
  timeoutMs?: number;                              // per-stage timeout, default 600000 (10 minutes)
  totalTimeoutMs?: number;                         // limit for the whole call; no limit by default
}

interface CompressResult {
  outputPath: string;
  originalSize: number;
  compressedSize: number;        // never larger than originalSize
  signed: boolean;               // input was digitally signed and returned untouched
  signaturesRemoved: boolean;  // removeSignatures stripped signatures so the document could be compressed
  pdfa: PdfaLevel | null;        // the input's PDF/A claim, e.g. { part: 2, conformance: 'b' }
  pdfaPreserved: boolean;        // output still carries a valid PDF/A claim
  metTarget: boolean | null;     // whether targetSizeBytes was met; null when no target was set
  warnings: string[];            // options requested but skipped, and why
  report?: HyperReport;          // present when report was requested
}
```

A compression runs in stages: decrypt (only with a password), the engine pass, and a repack. Each stage is a separate process with its own `timeoutMs`. A size target runs the engine and repack again for every attempt, so with a target, `totalTimeoutMs` is the setting that bounds the whole call.

When `savePath` is left out, the output goes beside the input as `<name>-compressed.pdf`. If the input file has no extension, `.pdf` is added.

Three result fields deserve special attention:

- `compressedSize` can equal `originalSize`. That means compression could not help and you received the original bytes; it is an outcome, not an error.
- `signed: true` means the input carries a digital signature and was deliberately returned untouched, because any rewrite would invalidate it.
- `warnings` lists everything the engine skipped for safety instead of doing something destructive. Empty means every requested option was applied. Details on the [guarantees](/guarantees) page.

## Recipes

### Encrypted PDFs

Check the password first when it comes from a user, then compress:

```js
import { compress, verifyPassword } from 'hyper-compress';

if (await verifyPassword('locked.pdf', password)) {
  await compress({ sourcePath: 'locked.pdf', savePath: 'out.pdf', password });
} else {
  // ask again; compress() with this password would throw decrypt_failed
}
```

`verifyPassword(sourcePath, password, timeoutMs?)` returns a `Promise<boolean>` without compressing anything; the optional timeout defaults to 60000. It uses the same decrypt stage the engine runs, so `true` is a promise that `compress()` will accept the same password.

### Hitting a size limit

```js
const r = await compress({
  sourcePath: 'scan.pdf',
  savePath: 'out.pdf',
  targetSizeBytes: 1024 * 1024,
});
if (r.metTarget === false) {
  console.log(`best achievable was ${r.compressedSize} bytes`);
}
```

The search finds the highest image quality that fits under your target, and if nothing fits it keeps going: quality down to 5, then resolution down through 72, 50 and 36 dpi. `metTarget` is `null` when you set no target, `true` when the target was met, and `false` when even the floor could not reach it. On a miss you still get the smallest file the engine could produce, plus a warning saying how small that was.

### Cancelling a long run

```js
const controller = new AbortController();
setTimeout(() => controller.abort(), 5000);

try {
  await compress({ sourcePath: 'huge.pdf', signal: controller.signal });
} catch (e) {
  if (e.code === 'cancelled') console.log('gave up after 5s');
}
```

### A folder of PDFs

```js
import { readdir } from 'node:fs/promises';
import { compress } from 'hyper-compress';

for (const name of await readdir('.')) {
  if (!name.endsWith('.pdf')) continue;
  const r = await compress({ sourcePath: name, preset: 'medium' });
  console.log(name, '->', r.outputPath, r.compressedSize);
}
```

Sequential is usually right: each compression already uses the machine well. If you parallelize, keep it to a few at a time.

### Custom settings

```js
await compress({
  sourcePath: 'scan.pdf',
  savePath: 'out.pdf',
  options: { rasterizePages: true, rasterizeDpi: 100, grayscale: true },
});
```

`options` is a partial override on top of the preset; anything you do not set comes from the preset. Every field is in the [options reference](/options). Values out of range are clamped to the nearest allowed value, unknown values for a choice fall back to its default, and unknown fields are ignored, so a bad option never fails a run. `resolveOptions` shows exactly what will run.

### Seeing what changed

```js
const r = await compress({ sourcePath: 'in.pdf', savePath: 'out.pdf', report: 'report.json' });
for (const img of r.report.images) {
  console.log(img.page, img.name, img.before.bytes, '->', img.after?.bytes);
}
```

The [compression report](/report) lists every font, page image and cleanup, before and after.

## Errors

Everything throws `HyperError` with a typed `code`:

| Code | Meaning |
|---|---|
| `decrypt_failed` | wrong or missing password for an encrypted input |
| `engine_error` | the engine could not process the document, or no engine binary exists for this platform |
| `timeout` | a stage exceeded `timeoutMs`, or the whole call exceeded `totalTimeoutMs` |
| `cancelled` | the `AbortSignal` fired |

`e.message` adds detail where there is any, for example the platform name when no binary exists. `engine_error` is also thrown when `sourcePath` is missing.

```js
import { compress, HyperError } from 'hyper-compress';

try {
  await compress({ sourcePath: 'in.pdf' });
} catch (e) {
  if (e instanceof HyperError) {
    console.error(e.code, e.message);
  } else {
    throw e;
  }
}
```

The parser runs in a worker subprocess with a hard timeout, so a crash or hang on hostile input kills the worker, not your process. That makes this SDK safe for servers processing untrusted uploads.

## Helpers

```js
import {
  HYPER_PRESETS,         // the four presets as full option objects, keyed low, medium, high, lossless
  normalizeHyperOptions, // clamp and complete a partial options object
  resolveOptions,        // the exact options a CompressInput will run with
  buildHyperTokens,      // the low-level engine tokens, for debugging
  HYPER_OPTION_DOCS,     // machine-readable docs for every option
} from 'hyper-compress';
```

`HYPER_OPTION_DOCS` is the structured record this site's [options reference](/options) is generated from: group, type, range, default and description for every option, useful for building your own settings UI.

## Types

Fully typed, ships its own declarations, built with `exactOptionalPropertyTypes` and the rest of TypeScript's strictest settings, so misuse fails at compile time in strict projects.

```ts
import type {
  CompressInput, CompressResult, HyperCompressOptions,
  CompressLevel, HyperErrorCode, PdfaLevel,
  HyperOptionDoc, HyperOptionGroup,
} from 'hyper-compress';
```

`PdfaLevel` is `{ part: number; conformance: string }`, for example `{ part: 2, conformance: 'b' }`. `conformance` is always lower case, and empty for levels without one, such as plain PDF/A-4. `HyperOptionDoc` is one entry of `HYPER_OPTION_DOCS`: `group`, `type` (`boolean`, `number` or `enum`), `values` for enums, `min` and `max` for numbers, `default` and `description`.

## Troubleshooting

**`SyntaxError: Cannot use import statement outside a module`.** Your file is being treated as CommonJS. Name it `.mjs`, or add `"type": "module"` to your package.json.

**`HyperError: no hpdf-worker binary for <platform>`.** No native build exists for this platform. Install [`hyper-compress-wasm`](/wasm) instead, or build from source and set `HYPER_DRV`.

**`decrypt_failed` with what you believe is the right password.** Confirm with `verifyPassword` first; if that also returns false, the PDF may use an owner password different from the user password you have.

**Everything works locally but fails in Docker or CI.** Check the container platform matches a supported one (`linux/amd64` and `linux/arm64` with glibc have native binaries; Alpine needs the wasm package) and that Node inside the image is 22 or newer.
