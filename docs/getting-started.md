---
title: Quickstart
description: Compress your first PDF with Hyper Compress in a few minutes, from the terminal, Node.js, an HTTP service or the browser.
---

# Quickstart

Compress your first PDF with Hyper Compress. By the end of this page you will have a smaller PDF, know how to read the result, and know which way of running Hyper fits your project.

Every way of running Hyper uses the same engine, the same presets and the same [options](/options), so the same settings give the same file everywhere.

## Before you begin

- A PDF to try. Any file works; a scanned document or a report with photos shows the biggest savings.
- For the terminal or Node.js: [Node.js](https://nodejs.org) 22 or newer on macOS (Apple silicon), Linux (x64 or ARM64) or Windows (x64). Check with `node --version`.
- For the HTTP service: [Docker](https://docs.docker.com/get-docker/).
- For the browser: nothing.

::: tip
Just want a smaller file right now? Open [hyper.bentopdf.com](https://hyper.bentopdf.com) and drop your PDF on the page. It runs entirely in your browser; your file is never uploaded.
:::

## 1. Install

::: code-group

```bash [CLI]
npm install -g hyper-compress
```

```bash [Node.js]
npm install hyper-compress
```

```bash [Docker]
docker run -d --name hyper -p 8080:8080 \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

```bash [WebAssembly]
npm install hyper-compress-wasm
```

:::

The npm package installs the engine for your platform automatically, with no download scripts. The Docker image starts a private compression service with a web page and an [HTTP API](/http-api) on port 8080.

## 2. Compress a PDF

::: code-group

```bash [CLI]
hyper input.pdf output.pdf
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
});

console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  'http://localhost:8080/api/compress'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input);

await writeFile('output.pdf', result.data);
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

:::

With no preset, Hyper uses `medium`: images at quality 50 and at most 150 dpi, fonts subset, duplicates merged. That suits documents you share and email.

From the CLI you see:

```text
output.pdf
  2593056 -> 1250960 bytes (52% smaller)
```

The first line is where the result was written. The second is the input size, the output size and the saving. Leave out `output.pdf` and the CLI writes `input-compressed.pdf` beside the input.

## 3. Read the result

Hyper never fails silently and never makes a file bigger. Three things in the result tell you what happened:

| What you see | What it means |
|---|---|
| Output size equals input size | Hyper could not make this file smaller, so it returned your original bytes. This is an outcome, not an error. |
| `signed: true`, or `[signed: original preserved]` in the CLI | The PDF has a digital signature. Hyper returned it untouched so the signature stays valid. See [Signed documents](/guides/signed-documents). |
| A `warning:` line, or an entry in `warnings` | Hyper skipped something you asked for because it was unsafe for this file, and says why. See [Warnings](/warnings). |

In Node.js all three are on the result object:

```js
console.log(result.signed);    // true if the input was signed and returned untouched
console.log(result.warnings);  // [] when every requested option was applied
```

## 4. Choose a preset

Pick a preset by what the document is for:

| Preset | Images | Use it for |
|---|---|---|
| `low` | quality 80, up to 200 dpi | archiving and printing; minimal visible change |
| `medium` | quality 50, up to 150 dpi | the default; sharing and email |
| `high` | quality 20, up to 72 dpi; standard fonts left to the viewer | the smallest file, for reading on screen |
| `lossless` | images are not re-encoded | files where every pixel must stay the same |

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --preset high
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', preset: 'high' });
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  'http://localhost:8080/api/compress?preset=high'
```

:::

A preset is a complete set of [options](/options). Override any of them on top of a preset; [Choose the right settings](/guides/choosing-settings) shows how.

## Which way should you run Hyper?

| You want to | Use | Runs on |
|---|---|---|
| Compress files by hand or in shell scripts | [CLI](/cli) | macOS (Apple silicon), Linux x64 and ARM64, Windows x64 |
| Compress from a Node.js app or server | [Node.js SDK](/node-sdk) | same as the CLI |
| Give other services or teams an API | [HTTP API](/http-api) on [Docker](/self-hosting) | any Docker host, x64 or ARM64 |
| Compress in the browser, or where the native engine does not install | [WebAssembly](/wasm) | any browser, any Node.js 22+ platform |
| Embed Hyper in a Rust program | [Rust crate](/rust) | the native platforms above |
| Embed Hyper from C, C++ or another language | [C API](/c-api) | prebuilt for macOS Apple silicon, builds elsewhere |

[Platforms and surfaces](/platforms) compares exactly what each one supports.

## Next steps

- [Choose the right settings](/guides/choosing-settings): presets, overrides and what each option costs.
- [Hit a file size target](/guides/size-targets): ask for "under 2 MB" and let Hyper search.
- [Running in production](/guides/production): batches, concurrency and untrusted uploads.
- [Guarantees](/guarantees): what Hyper promises never to do to your files.
