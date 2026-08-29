# Getting Started

Hyper Compress can be used in a few different ways, but the engine underneath is always the same. Whether you're using the CLI, Node SDK, WebAssembly build, or something else, you'll get the same presets, the same [28 options](/options), and the same result from the same settings.

Pick the way you want to use Hyper, and this guide will help you compress your first PDF.

## The fastest way: your browser

If you just want to compress a PDF, you do not need to install anything.

1. Open [hyper.bentopdf.com](https://hyper.bentopdf.com).
2. Drop your PDF onto the page, or click to choose it.
3. Pick a compression level and press Compress.
4. Save the result.

The engine runs as WebAssembly inside your browser. Your file is never uploaded anywhere; you can load the page, disconnect from the internet, and it still works.

## From the terminal

### What you need first

Node.js 22 or newer. Check what you have:

```bash
node --version
```

If that prints `v22.0.0` or higher you are ready. If it prints something lower, or `command not found`, install the current LTS from [nodejs.org](https://nodejs.org) and open a new terminal.

Supported platforms for the native engine: macOS on Apple silicon, Linux x64, Windows x64. On anything else (an Intel Mac, a Raspberry Pi), use the [WebAssembly package](/wasm) instead.

### Install and run

```bash
npm install -g hyper-compress
hyper input.pdf
```

You should see something like:

```
input-compressed.pdf
  2593056 -> 1250960 bytes (52% smaller)
```

That is the whole workflow. The first line is where the result was written, next to your input as `<input>-compressed.pdf`; the second is the input size, the output size, and the saving. To choose the output name yourself, pass it as a second argument: `hyper input.pdf smaller.pdf`.

Prefer not to install globally? `npx hyper input.pdf` works from any folder after `npm install hyper-compress` in that folder.

Common variations:

```bash
hyper input.pdf out.pdf --preset high        # smallest output
hyper scan.pdf out.pdf --target-size 2MB     # aim for a size
hyper locked.pdf out.pdf --password hunter2  # encrypted input
```

Every flag is documented on the [CLI page](/cli), including what to do when something goes wrong.

## From Node.js code

Same install, used as a library:

```js
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  preset: 'medium',
});

console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

Save that as `compress.mjs` next to an `input.pdf` and run `node compress.mjs`. The [Node SDK page](/node-sdk) walks through passwords, target sizes, cancellation, batching and error handling.

## As a service for your team

If you have Docker installed, one command gives you a private compression service with a web page and an API:

```bash
docker run -d -p 8080:8080 ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

Open `http://localhost:8080` for the interface, or POST a PDF to `/api/compress`. Everything runs on your machine. Setup, tuning and TLS are covered in [Self-Hosting](/self-hosting), the API in [HTTP API](/http-api).

## The presets

Every surface uses the same four presets. Pick by what the document is for:

| Preset | Image quality | Max DPI | Use for |
|---|---|---|---|
| `low` | 80 | 200 | archival copies and minimal visible change |
| `medium` | 50 | 150 | the default; sharing and email |
| `high` | 20 | 72 | smallest output and screen reading |
| `lossless` | off | off | no image re-encoding, pixel-identical images |

A preset is just a complete options object; start from one and override any field. The [options reference](/options) lists all 28 options and exactly what each preset sets.

## Three things to know before you start

1. **The output is never larger than the input.** Your PDF will never come back bigger. If Hyper can't make it smaller, you get the original file back instead. 
2. **Some documents come back unchanged on purpose.** A digitally signed PDF is returned untouched, because compressing it would break the signature. The result tells you when this happened.
3. **Hyper tells you when it skips something.** Results carry a `warnings` list (the CLI prints them) explaining anything that was skipped for safety, like rasterizing a document that still has selectable text.

The full behavioral contract is on the [guarantees](/guarantees) page.



## Where next

- All flags and troubleshooting: [CLI](/cli)
- The full programming interface: [Node SDK](/node-sdk)
- Running without native binaries: [WebAssembly](/wasm)
- Your own service: [Self-Hosting](/self-hosting) and the [HTTP API](/http-api)
- Every option with defaults and ranges: [Options reference](/options)
- What was measured against Ghostscript, MuPDF and qpdf: [Benchmarks](/benchmarks)
