# hyper-compress

High fidelity, content preserving PDF compression for Node.js. 

Full documentation lives at [hyper.bentopdf.com/docs](https://hyper.bentopdf.com/docs/); benchmarks, methodology and the source live in the [GitHub repository](https://github.com/alam00000/bentopdf-hyper-compress).

## Install

```bash
npm install hyper-compress
```

Node 22 or newer, ESM only. The native engine binaries arrive automatically through a platform package (`hyper-compress-darwin-arm64`, `-linux-x64` or `-win32-x64`); npm installs the one matching your machine. To use binaries you built yourself, set `HYPER_DRV` (worker) and `HYPER_QPDF` (qpdf) to their paths and they take precedence.

On platforms without a native package, or when you cannot ship native binaries at all, use [`hyper-compress-wasm`](https://www.npmjs.com/package/hyper-compress-wasm) instead: the same engine compiled to WebAssembly, producing identical output about 4x slower, with a buffer-based API that runs anywhere Node runs.

## Quick start

```js
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'medium',
});

console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

The package also installs a `hyper` CLI:

```bash
npx hyper input.pdf output.pdf --preset high
npx hyper scan.pdf out.pdf --target-size 2MB
npx hyper locked.pdf out.pdf --password secret
npx hyper in.pdf out.pdf --set grayscale=true --set imageQuality=40
npx hyper in.pdf out.pdf --brotli
npx hyper --help        # every flag, with examples
npx hyper --version     # the installed version
```

Exit codes: 0 on success, 1 on any error (the message goes to stderr), 2 on bad usage. `--set` accepts any option from the table below by its camelCase name.

## API

### compress(input)

Compresses one PDF. Returns a `Promise<CompressResult>`.

```ts
interface CompressInput {
  sourcePath: string;                              // path to the input PDF (required)
  savePath?: string;                               // output path; defaults to '<input>-compressed.pdf' beside the source
  preset?: 'low' | 'medium' | 'high' | 'lossless'; // default 'medium'
  options?: Partial<HyperCompressOptions>;         // overrides applied on top of the preset
  password?: string | null;                        // password for encrypted input
  targetSizeBytes?: number;                        // aim for a size; see metTarget on the result
  signal?: AbortSignal;                            // cancel the run
  timeoutMs?: number;                              // per-stage timeout, default 600000
}

interface CompressResult {
  outputPath: string;       // where the output was written
  originalSize: number;     // input size in bytes
  compressedSize: number;   // output size in bytes, never larger than originalSize
  signed: boolean;          // true when the input was digitally signed and returned untouched
  pdfa: PdfaLevel | null;   // the input's PDF/A claim, e.g. { part: 2, conformance: 'b' }
  pdfaPreserved: boolean;   // true when the output still carries a valid PDF/A claim
  metTarget: boolean | null; // whether targetSizeBytes was met; null when no target was set
  warnings: string[];       // options that were requested but skipped, and why
}
```

`warnings` reports every case where the engine did less than you asked for
instead of doing something unsafe: a rasterize request skipped because the
document has a text layer, or rasterize and Brotli dropped to keep a PDF/A
document conformant. An empty array means every requested option was applied.

Guarantees enforced by the engine, not left to the caller:

- The output is never larger than the input. When compression does not help, you get the original bytes.
- A digitally signed document is returned byte-for-byte untouched, because any rewrite would invalidate the signature.
- A PDF/A document either keeps its conformance (with `preserveConformance`) or has the claim removed. It is never left claiming a standard it no longer meets.
- Page rasterization is refused when any page contains text, including an invisible OCR layer, so it cannot destroy searchable text. The refusal is reported in `warnings`.
- With `targetSizeBytes`, the search pushes to quality 5 and 36 dpi to reach your target. If it is still unreachable, you get the smallest file the engine could produce, `metTarget: false`, and a warning saying how small that was.

Errors are thrown as `HyperError` with a typed `code`:

| Code | Meaning |
|---|---|
| `decrypt_failed` | wrong or missing password for an encrypted input |
| `engine_error` | the engine could not process the document |
| `timeout` | a stage exceeded `timeoutMs` |
| `cancelled` | the `AbortSignal` fired |

```js
import { compress, HyperError } from 'hyper-compress';

try {
  await compress({ sourcePath: 'locked.pdf', password: 'wrong' });
} catch (e) {
  if (e instanceof HyperError && e.code === 'decrypt_failed') {
    // ask the user for the right password
  }
}
```

The parser runs in a worker subprocess with a timeout, so a crash on hostile input kills the worker, not your process. To cancel a long run, pass an `AbortSignal` as `signal`; the run rejects with code `cancelled`.

### verifyPassword(sourcePath, password, timeoutMs?)

Checks a password against an encrypted PDF without compressing it. Returns `Promise<boolean>`. The optional `timeoutMs` defaults to 60000. Uses the same qpdf decrypt stage the engine itself runs, so a `true` here means `compress()` will accept the same password.

```js
import { verifyPassword } from 'hyper-compress';

if (await verifyPassword('locked.pdf', 'secret')) {
  await compress({ sourcePath: 'locked.pdf', savePath: 'out.pdf', password: 'secret' });
}
```

### Presets and option helpers

```js
import { HYPER_PRESETS, normalizeHyperOptions, resolveOptions, buildHyperTokens } from 'hyper-compress';
```

- `HYPER_PRESETS` is the record of the four presets as full `HyperCompressOptions` objects.
- `normalizeHyperOptions(raw)` clamps and completes a partial options object into a valid one.
- `resolveOptions(input)` returns the exact options a given `CompressInput` will run with.
- `buildHyperTokens(options)` returns the low-level engine tokens, useful for debugging what a run will do.

| Preset | Image quality | Max DPI | Notes |
|---|---|---|---|
| `low` | 80 | 200 | minimal visible change |
| `medium` | 50 | 150 | the default |
| `high` | 20 | 72 | smallest; also unembeds standard fonts |
| `lossless` | off | off | no image re-encoding, pixel-identical images |

## Options

Every field of `HyperCompressOptions`. Pass any subset as `options`; unspecified fields come from the preset.

### Images

| Option | Type | Default | What it does |
|---|---|---|---|
| `imageQuality` | number 20 to 100 | 80 | JPEG quality for re-encoded images. Lower is smaller with more visible artefacts. |
| `maxDpi` | number 0 to 600 | 150 | Images above this resolution are resampled down. 0 leaves resolution alone. |
| `forceDownsample` | boolean | false | Resample even when the image is only slightly above the limit. |
| `lossless` | boolean | false | Turns off quality, resolution and every other lossy image step. |
| `grayscale` | boolean | false | Convert images to grayscale. |
| `reduceColor` | boolean | false | Reduce colour complexity where it does not change appearance. |
| `clipImages` | boolean | false | Crop image data hidden outside the visible clip region. |
| `preferJpx` | boolean | false | Let JPEG 2000 compete with JPEG per image and keep the smaller one. |
| `removeAlternates` | boolean | false | Drop alternate image versions. |
| `flattenIcc` | boolean | false | Flatten ICC colour profiles. |

### Rasterization

For image-only documents such as scans and print-to-PDF output. The engine refuses to rasterize when any page contains text, including an invisible OCR layer, so this cannot destroy searchable text; the skip shows up in the result's `warnings`. Rasterization is also disabled while preserving PDF/A conformance, again reported in `warnings`.

| Option | Type | Default | What it does |
|---|---|---|---|
| `rasterizePages` | boolean | false | Replace each page with a single rendered image. Shrinks vector-heavy pages dramatically. |
| `rasterizeDpi` | number 36 to 600 | 150 | Render resolution. Higher keeps more detail. |
| `rasterizeQuality` | number 1 to 100 | 50 | JPEG quality for the rasterized pages. |

### Fonts

| Option | Type | Default | What it does |
|---|---|---|---|
| `subsetFonts` | boolean | false | Keep only the glyphs the document uses. Lossless by definition. |
| `removeStandardFonts` | boolean | false | Unembed the standard 14 PDF fonts; viewers supply them. |
| `unembedAliasedFonts` | boolean | false | Unembed metric-compatible clones of the standard fonts. Arial, Times New Roman and Courier New map to the built-in equivalents. Only applied when safe. |
| `mergeFonts` | boolean | false | Merge duplicate font programs and dictionaries. |

### Structure and interactivity

| Option | Type | Default | What it does |
|---|---|---|---|
| `removeAnnots` | boolean | false | Remove comments and other annotations. |
| `flattenForms` | boolean | false | Flatten form fields into page content. Form fields stop being fillable. |
| `flattenLinks` | boolean | false | Flatten link annotations. Links stop being clickable. |
| `removeStructTree` | boolean | false | Drop the structure tree. Removes tagging, which screen readers rely on. |
| `removeThreads` | boolean | false | Remove article threads. |

### Metadata and cleanup

| Option | Type | Default | What it does |
|---|---|---|---|
| `removeThumbnails` | boolean | false | Remove embedded page thumbnails. |
| `removeAppData` | boolean | false | Remove application-private data and piece info. |
| `removeSpiderInfo` | boolean | false | Remove web capture information. |
| `removeOutputIntents` | boolean | false | Remove output intents. |

### Output format

| Option | Type | Default | What it does |
|---|---|---|---|
| `preserveConformance` | boolean | false | Keep a PDF/A document conformant. Disables every step that would break the standard, including Brotli. |
| `brotli` | boolean | false | PDF 2.0 Brotli stream compression. Smaller files, but the reader must support it (MuPDF 1.26+, Ghostscript 10.06+, Firefox). Off while preserving PDF/A. |

## Environment variables

| Variable | Effect |
|---|---|
| `HYPER_DRV` | path to a custom `hpdf-worker` binary, overrides the platform package |
| `HYPER_QPDF` | path to a custom `qpdf` binary, overrides the platform package |

## Types

The package is fully typed, ships its own declarations, and is built with `exactOptionalPropertyTypes` and the rest of TypeScript's strictest settings, so misuse fails at compile time in strict projects.

```ts
import type {
  CompressInput,
  CompressResult,
  HyperCompressOptions,
  CompressLevel,
  HyperErrorCode,
  PdfaLevel,
} from 'hyper-compress';
```

## License

AGPL-3.0-only. For use in proprietary products, a commercial license is available; contact us at [contact@bentopdf.com](mailto:contact@bentopdf.com). Bundled third-party components keep their own licenses; see [NOTICE.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/NOTICE.md).
