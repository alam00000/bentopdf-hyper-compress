---
title: Platforms and surfaces
description: See where each way of running Hyper Compress works, what each one can do, and which release files and packages to use.
---

# Platforms and surfaces

Hyper Compress ships one engine through seven surfaces: the browser page, the CLI, the Node.js SDK, the WebAssembly SDK, the HTTP service, the Rust crate and the C library. This page shows where each one runs and what each one can do, so you can pick one before you start.

## Where each surface runs

| Surface | Get it | Runs on |
|---|---|---|
| Browser | [hyper.bentopdf.com](https://hyper.bentopdf.com) | any modern browser; files never leave the device |
| [CLI](/cli) | `npm install -g hyper-compress` | macOS Apple silicon, Linux x64, Linux ARM64, Windows x64, with Node.js 22 or newer |
| [Node.js SDK](/node-sdk) | `npm install hyper-compress` | macOS Apple silicon, Linux x64, Linux ARM64, Windows x64, with Node.js 22 or newer |
| [WebAssembly SDK](/wasm) | `npm install hyper-compress-wasm` | any platform with Node.js 22 or newer, including Intel Macs, Alpine and FreeBSD, and browsers |
| [HTTP API](/http-api) and web page | `ghcr.io/alam00000/bentopdf-hyper-compress` | Docker on `linux/amd64` and `linux/arm64` |
| [Rust crate](/rust) | `rust/hyper-compress` in the [repository](https://github.com/alam00000/bentopdf-hyper-compress) | macOS Apple silicon, Linux x64, Linux ARM64, Windows x64, next to the engine programs |
| [C library](/c-api) | `libhypercompress` on each [release](https://github.com/alam00000/bentopdf-hyper-compress/releases) | prebuilt for macOS Apple silicon; [build it](/building) for other platforms |

The Docker image runs the HTTP service and the CLI as Rust programs on Debian. It contains no Node.js.

## Native packages

The `hyper-compress` npm package gets its engine from one platform package. npm installs only the one that matches your machine, and there are no install scripts.

| Platform | npm package |
|---|---|
| macOS, Apple silicon | `hyper-compress-darwin-arm64` |
| Linux, x64 | `hyper-compress-linux-x64` |
| Linux, ARM64 | `hyper-compress-linux-arm64` |
| Windows, x64 | `hyper-compress-win32-x64` |

Each one holds the engine (`hpdf-worker`), `qpdf`, the Node.js addon (`hyper-compress.node`) and a page renderer (`hpdf-render`). Each Linux package is built on a machine with that CPU.

The Linux packages need glibc and are marked `"libc": ["glibc"]`. They run on Debian, Ubuntu, Fedora, Amazon Linux and similar distributions. On Alpine and other musl-based systems npm skips them, and the SDK then reports that there is no native engine for the platform. Use the [WebAssembly SDK](/wasm) there, and on Intel Macs, which have no native package.

## Release files

Each [GitHub release](https://github.com/alam00000/bentopdf-hyper-compress/releases) attaches these files and a `SHA256SUMS` file to check them:

| File | Contents |
|---|---|
| `hyper-compress-<version>-macos-arm64.tar.gz` | `hpdf-worker`, `hpdf-render`, `qpdf` and `hyper-compress.node` for macOS Apple silicon |
| `hyper-compress-<version>-linux-x64.tar.gz` | the same four files for Linux x64 |
| `hyper-compress-<version>-linux-arm64.tar.gz` | the same four files for Linux ARM64 |
| `hyper-compress-<version>-windows-x64.tar.gz` | the same four files for Windows x64, with qpdf's DLLs and the Microsoft C++ runtime DLLs |
| `hyper-compress-<version>-wasm.tar.gz` | the WebAssembly engine, `hyper-compress.js` and `hyper-compress.wasm` |
| `libhypercompress-<version>-macos-arm64.tar.gz` | the C library, `libhypercompress.dylib`, for macOS Apple silicon |

`<version>` is the release tag, for example `v1.1.4`. On Linux, check the files you downloaded with:

```bash
sha256sum -c SHA256SUMS --ignore-missing
```

The release also publishes the Docker image for `linux/amd64` and `linux/arm64` under one tag, plus `latest`. See [Self-hosting](/self-hosting#the-image).

## What each surface can do

All surfaces share the same presets, the same [options](/options), the same size target search and the same output for the same settings. They differ in what wraps the engine:

| Feature | CLI | Node.js SDK | WebAssembly SDK | HTTP API | Rust crate | C library |
|---|---|---|---|---|---|---|
| Presets and every option | yes | yes | yes | yes | yes | as raw id and value pairs |
| Output never larger than the input | yes | yes | yes | yes | yes | no, compare sizes yourself |
| Size target | `--target-size` | `targetSizeBytes` | `targetSizeBytes` | `targetSizeBytes` | `target_size_bytes` | no |
| Encrypted input | `--password` | `password` | `password` | `X-Password` | `password` | `password` |
| Password check without compressing | no | `verifyPassword` | `verifyPassword` | `/api/verify-password` | `verify_password` | no |
| Signed documents returned untouched | yes | yes | yes | yes | yes | yes |
| `removeSignatures` | yes | yes | yes | yes | yes | yes, id 20 bit `0x400000` |
| `preserveConformance` for PDF/A | yes | yes | yes | yes | yes | yes, as id 64 |
| `linearize` (fast web view) | yes | yes | no, reported as a warning | yes | yes | no |
| [Compression report](/report) | `--report` | `report` | no | no | `report` | no |
| [Warnings](/warnings) for skipped options | stderr | `warnings` | `warnings` | `X-Warnings` header | `warnings` | no |
| Cancellation | Ctrl+C | `AbortSignal` | no | a client disconnect does not cancel | `CancelToken` | no |
| Timeouts | 10 minutes per step | `timeoutMs`, `totalTimeoutMs` | no | `HYPER_TIMEOUT_MS` | `stage_timeout`, `total_timeout` | no |
| Crash isolation | each file in a worker process | each file in a worker process | none in process; use a worker thread | each file in a worker process | each file in a worker process | none, runs in your process |

## Threads and memory

Inside one compression, the native engine uses as many threads as the machine has cores, up to 8, for image work. The C library can set this with option id 68. The WebAssembly build uses one thread.

Every surface is safe to call from many threads at once. The CLI, Node.js SDK, Rust crate and HTTP service run each compression in its own engine process; the C API lets calls take turns; a WebAssembly instance runs one compression at a time. See [Safe to call from many threads at once](/guarantees#safe-to-call-from-many-threads-at-once).

The native engine reads and writes files on disk, so its memory use depends on the document's content, mostly its largest images, more than on the file size. The C library's `hpdf_compress_buffer` refuses inputs over 2 GB.

The WebAssembly build holds the input, its working memory and the output in a memory space of at most 4 GB. For files in the hundreds of megabytes, use a native surface.

## Next steps

- [Quickstart](/getting-started): compress your first PDF on any surface.
- [Self-hosting](/self-hosting): run the HTTP service with Docker or Kubernetes.
- [Building from source](/building): build the engine for a platform without a prebuilt package.
- [FAQ](/faq): common platform and install problems.
