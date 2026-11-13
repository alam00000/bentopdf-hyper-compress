# Platforms and surfaces

Hyper ships one engine through six surfaces. This page lists where each one runs and what each one can do, so you can pick the right one before you start.

## Where each surface runs

| Surface | Package or download | Runs on |
|---|---|---|
| Browser | [hyper.bentopdf.com](https://hyper.bentopdf.com) | any modern browser; files never leave the device |
| CLI | `npm install -g hyper-compress` | macOS Apple silicon, Linux x64, Linux ARM64, Windows x64 (Node.js 22+) |
| Node SDK | `npm install hyper-compress` | macOS Apple silicon, Linux x64, Linux ARM64, Windows x64 (Node.js 22+) |
| WebAssembly SDK | `npm install hyper-compress-wasm` | anywhere Node.js 22+ runs, including Intel Macs, Alpine and FreeBSD |
| HTTP API and web interface | `ghcr.io/alam00000/bentopdf-hyper-compress` | Docker, `linux/amd64` and `linux/arm64` |
| C library | `libhypercompress` on each [release](https://github.com/alam00000/bentopdf-hyper-compress/releases) | prebuilt for macOS Apple silicon; other platforms build it from source |

Each [GitHub release](https://github.com/alam00000/bentopdf-hyper-compress/releases) attaches these files, with a `SHA256SUMS` file to verify them:

| File | Contents |
|---|---|
| `hyper-compress-<version>-macos-arm64.tar.gz` | native engine binaries for macOS Apple silicon |
| `hyper-compress-<version>-linux-x64.tar.gz` | native engine binaries for Linux x64 |
| `hyper-compress-<version>-linux-arm64.tar.gz` | native engine binaries for Linux ARM64 |
| `hyper-compress-<version>-windows-x64.tar.gz` | native engine binaries for Windows x64 |
| `hyper-compress-<version>-wasm.tar.gz` | the WebAssembly engine |
| `libhypercompress-<version>-macos-arm64.tar.gz` | the C library for macOS Apple silicon |

The native npm package pulls its binaries from a platform package (`hyper-compress-darwin-arm64`, `hyper-compress-linux-x64`, `hyper-compress-linux-arm64` or `hyper-compress-win32-x64`). npm installs only the one that matches your machine, and there are no postinstall scripts.

The Linux builds need glibc, so they run on Debian, Ubuntu, Fedora, Amazon Linux and similar, but not on Alpine; use the WebAssembly SDK there.

## What each surface can do

All surfaces share the same presets, the same [options](/options), the same size target search and the same output for the same settings. The differences are in what wraps the engine:

| Feature | CLI | Node SDK | WebAssembly SDK | HTTP API | C library |
|---|---|---|---|---|---|
| Presets and every option | yes | yes | yes | yes | as raw id and value pairs |
| Output never larger than the input | yes | yes | yes | yes | no, compare sizes yourself |
| Size target (`targetSizeBytes`) | yes | yes | yes | yes | no |
| Encrypted input | yes | yes | yes | yes | yes |
| Password check without compressing | no | `verifyPassword` | `verifyPassword` | `/api/verify-password` | no |
| Signed documents returned untouched | yes | yes | yes | yes | yes |
| `removeSignatures` | yes | yes | yes | yes | yes |
| `preserveConformance` for PDF/A | yes | yes | yes | yes | yes, as id 64 |
| `linearize` (fast web view) | yes | yes | no, reported as a warning | yes | no |
| [Compression report](/report) | `--report` | `report` | no | no | no |
| Warnings for skipped options | stderr | `warnings` | `warnings` | `X-Warnings` header | no |
| Cancellation | Ctrl+C | `AbortSignal` | no | client disconnects do not cancel | no |
| Timeouts | default 10 minutes per stage | `timeoutMs`, `totalTimeoutMs` | no | `HYPER_TIMEOUT_MS` | no |
| Crash isolation | each file in a worker process | each file in a worker process | none in process; use a worker thread | each file in a worker process | none, runs in your process |

## Threads and memory

Inside one compression, the native engine uses up to 8 threads for image work, or as many as the machine has if fewer. The WebAssembly build is single threaded.

The native engine works on files on disk and handles inputs up to 2 GB. The WebAssembly build holds the input, its working memory and the output inside a 4 GB address space, so for files in the hundreds of megabytes the native surfaces are the better choice.
