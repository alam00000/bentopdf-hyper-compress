---
title: FAQ and troubleshooting
description: Answers to the questions people ask most about Hyper Compress, from files that do not shrink to install errors and HTTP status codes.
---

# FAQ and troubleshooting

Short answers to the questions people ask most about Hyper Compress, each with a link to the full story.

## Results

### Why is my file not smaller?

Hyper never returns a file larger than your input. When it cannot make a file smaller, it returns your original bytes, and the output size equals the input size. This is an outcome, not an error.

It happens most with files that are already compressed well, or that are mostly text. To get more out of a file:

- Use a stronger preset: `--preset high`.
- Ask for a size and let Hyper search: `--target-size 2MB`.
- For scans, try mixed raster content: `--mrc auto`.

[Choose the right settings](/guides/choosing-settings), [Hit a file size target](/guides/size-targets) and [Scanned documents](/guides/scanned-documents) explain each one.

### Why did my signed PDF not change?

Changing any byte of a digitally signed PDF breaks its signature, so Hyper returns signed files untouched. The result says `signed: true`, the CLI prints `[signed: original preserved]` and the HTTP API sends `X-Signed: true`.

If you want a smaller file more than a signed one, set `removeSignatures`. The visible signature stays on the page, but the file is no longer signed. See [Signed documents](/guides/signed-documents).

### Why is text still selectable after I asked to rasterize pages?

`rasterizePages` turns pages into images only when the document has no text layer, so it never destroys searchable text by accident. When the document has text, Hyper skips rasterizing and adds the warning `rasterize skipped: document has a text layer`.

To rasterize anyway, also set `rasterizeText`. Text then stops being selectable and searchable, and links stop working.

```bash
hyper input.pdf output.pdf --set rasterizePages=true --set rasterizeText=true
```

See [Options](/options) and [Warnings](/warnings).

### Why does my PDF reader say PDF 1.7 when I turned on Brotli?

Hyper marks a Brotli file as PDF 2.0 in the document catalog, the file's root object: it sets the catalog's `Version` entry to `2.0` and adds the Brotli extension entry. It does not change the version in the file's first line (the header), so tools that only read the header still show the old version, such as 1.7.

The PDF format says the newer of the two versions applies, so the file is PDF 2.0. To change the header as well, set `minVersion` to `2.0`:

```bash
hyper input.pdf output.pdf --brotli --set minVersion=2.0
```

Brotli streams need a reader that supports them. See [Options](/options).

## Privacy and licensing

### Does Hyper upload my files?

No. At [hyper.bentopdf.com](https://hyper.bentopdf.com) the engine runs in your browser as WebAssembly, and your file never leaves your device. The CLI, the SDKs, the Rust crate and the C library run on your own machine. The Docker service runs on your server and deletes each file when it has sent the response. See [Self-hosting](/self-hosting).

### Can I use Hyper commercially?

Yes. Hyper is free under the AGPL-3.0 license if your product publishes its source code under the same terms. For closed-source products and services there is a commercial license. See [Licensing](/licensing).

## Install and platforms

### Which platforms does Hyper run on?

The native packages run on macOS (Apple silicon), Linux x64 and ARM64 (with glibc), and Windows x64. The WebAssembly package runs anywhere Node.js 22 runs, and in browsers. The Docker image runs on `linux/amd64` and `linux/arm64`. See [Platforms](/platforms).

### `hyper: command not found`

The CLI is in the npm package `hyper-compress`, and its command is `hyper`. Install it globally:

```bash
npm install -g hyper-compress
```

If the command is still not found, npm's global folder is not on your `PATH`. Find the folder with `npm prefix -g` and add its `bin` folder (on Windows, the folder itself) to your `PATH`. Or run it without installing:

```bash
npx -p hyper-compress hyper input.pdf output.pdf
```

See [CLI](/cli).

### `no hpdf-worker binary for <platform>` or `no hyper-compress.node binary for <platform>`

There is no native engine for this machine. The message names your platform, for example `darwin-x64`. It has these causes:

- Your platform has no native package. Native packages exist for `darwin-arm64`, `linux-x64`, `linux-arm64` and `win32-x64`. Use the [WebAssembly SDK](/wasm) (`npm install hyper-compress-wasm`) instead.
- Your Linux uses musl, as Alpine does. npm skips the glibc-only platform packages there. Use the WebAssembly SDK.
- npm left out the platform package. They are optional dependencies, so `npm install --omit=optional` drops them. Install without that flag.
- You built the engine yourself. Set `HYPER_DRV` to your `hpdf-worker` and `HYPER_ADDON` to your `hyper-compress.node`. See [Building from source](/building).

### `hpdf-worker not found at <path>` or `qpdf not found at <path>`

`HYPER_DRV` or `HYPER_QPDF` points at a file that does not exist. Fix the path, or unset the variable so Hyper finds its own programs.

### `the input is not a PDF: no %PDF- header in its first 1024 bytes`

The file is not a PDF, or it is damaged at the start. The CLI, the SDKs and the Rust crate fail with `engine_error` and this message; the HTTP service answers `400` `not_a_pdf`. Check that the file opens in a PDF reader.

### `the PDF is encrypted and needs a password`

The PDF needs a password to open and you did not send one, so Hyper fails with `decrypt_failed`. Pass the password with `--password` in the CLI, `password` in the SDKs, or the `X-Password` header over HTTP (`X-Password-Encoded` for a password with non-ASCII characters). A PDF with only an owner password, which limits printing or editing but opens without one, compresses without a password. See [Encrypted PDFs](/guides/encrypted-pdfs).

### Does it work on Alpine or other musl-based Linux?

Not with the native packages: they need glibc, so npm skips them on musl systems and the SDK reports `no hyper-compress.node binary` or `no hpdf-worker binary` for the platform. Use the [WebAssembly SDK](/wasm), or a glibc-based image such as `node:22-slim` for your container.

### Does it work on an Intel Mac?

Not with the native packages: the macOS package is for Apple silicon only. Use the [WebAssembly SDK](/wasm), which runs on any Mac with Node.js 22, or the [Docker image](/self-hosting).

## Docker and the HTTP service

### `Permission denied` when Docker writes to my folder

The image runs as the user `hyper` with uid 1000. If your user on the host has another uid, the container cannot write to the folder you mounted. Run the container as your own user:

```bash
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/work \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf
```

See [Self-hosting: CLI mode](/self-hosting#cli-mode).

### The service returns 429, 504 or 413

| Status | Cause | Fix |
|---|---|---|
| `429` `busy` | All slots are busy and the queue is full. | Retry after the `Retry-After` delay (1 second). Raise `HYPER_QUEUE` for bursts or `HYPER_CONCURRENCY` for throughput. |
| `504` `timeout` | A job, or one step of it, ran longer than `HYPER_TIMEOUT_MS` (10 minutes by default). | Raise `HYPER_TIMEOUT_MS`, and your proxy's timeout with it. |
| `413` `too_large` | The upload is larger than `HYPER_MAX_UPLOAD_MB` (500 MB by default). The body includes `maxBytes`. | Raise `HYPER_MAX_UPLOAD_MB`, and your proxy's body limit with it. |

See [HTTP API: Errors](/http-api#errors) and [Self-hosting](/self-hosting#environment-variables).

### Out of memory on very large files

Memory use grows with the largest images in a document, not only with the file size.

- Native surfaces (CLI, Node.js SDK, Rust crate, Docker): give the process or container more memory, and run fewer jobs at once (`HYPER_CONCURRENCY` in Docker).
- WebAssembly: the engine has at most 4 GB of memory for the input, its work and the output. For files in the hundreds of megabytes, use a native surface.

See [Platforms: Threads and memory](/platforms#threads-and-memory).

## Getting help

### How do I report a bug?

Open an issue on [GitHub](https://github.com/alam00000/bentopdf-hyper-compress/issues/new/choose) with the bug report form. Include:

- The PDF that shows the problem. If you cannot share it, make a smaller file that still shows it. Files you attach may be added to the public test set.
- The exact command or call, with the preset and any options.
- The output, error or warnings.
- The version: `hyper --version`, the npm package version or the Docker image tag.
- Your operating system.

Report security problems privately, as described in [`SECURITY.md`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/SECURITY.md), not as public issues.
