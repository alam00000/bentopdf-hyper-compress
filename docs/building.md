---
title: Building from source
description: Build the Hyper Compress engine, the Rust programs, the Node.js addon, WebAssembly and the Docker image from source, and run the tests.
---

# Building from source

Build Hyper Compress from a checkout of the [repository](https://github.com/alam00000/bentopdf-hyper-compress). Most people never need to: every [release](https://github.com/alam00000/bentopdf-hyper-compress/releases) and the npm packages ship prebuilt programs. Build from source when you change the engine, package Hyper for a platform without a prebuilt package, or check the binaries yourself.

[`BUILDING.md`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/BUILDING.md) in the repository lists every engine patch and platform note. This page walks through the build in order.

## What you build

| Part | Made from | Output |
|---|---|---|
| JavaScript SDK, CLI and tests | `sdk/`, `cli/bin/`, `tests/` | `dist/` |
| Native engine | a pinned [PDFium](https://pdfium.googlesource.com/pdfium/) checkout plus `core/` | `cli/prebuilt/hpdf-worker`, `cli/prebuilt/hpdf-render` |
| qpdf | `cli/build-qpdf.sh` | `cli/prebuilt/qpdf` |
| Rust programs | `rust/` | the Node.js addon, the `hyper` CLI and `hyper-server` |
| C library (macOS) | `sdk/native/` | `sdk/native/lib/libhypercompress.dylib` |
| WebAssembly | `wasm/` and the same PDFium checkout | `wasm/out/hyper-compress.js`, `wasm/out/hyper-compress.wasm` |
| Docker image | `docker/Dockerfile` | the image, built for your machine's CPU |

## Prerequisites

Every platform needs:

- [Node.js](https://nodejs.org) 22 or newer.
- [Rust](https://rustup.rs) stable, installed with rustup.
- git and python3.
- Google's [depot_tools](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html) on your `PATH`, for the PDFium checkout.
- About 10 GB of free disk for the one-time PDFium checkout.

Then, by platform:

| Platform | Also install |
|---|---|
| macOS, Apple silicon | Xcode Command Line Tools (`xcode-select --install`). To rebuild qpdf: `brew install jpeg-turbo`. |
| Linux x64 | `build-essential cmake ninja-build pkg-config rsync python3 xz-utils lsb-release file libjpeg-dev zlib1g-dev` |
| Linux ARM64 | the same packages as Linux x64, plus LLVM 23 from [apt.llvm.org](https://apt.llvm.org), see below |
| Windows x64 | Visual Studio Build Tools and the Windows SDK; there is no build script, see [Windows](#windows) |
| WebAssembly | [Emscripten](https://emscripten.org) 4.0.22, active in your shell |

On Debian or Ubuntu:

```bash
sudo apt-get install -y build-essential cmake ninja-build pkg-config rsync \
  python3 xz-utils lsb-release file libjpeg-dev zlib1g-dev
```

On Linux ARM64, PDFium's own clang only runs on x64, so install LLVM 23:

```bash
curl -fsSL -o llvm.sh https://apt.llvm.org/llvm.sh
sudo bash llvm.sh 23
```

`docker/build-linux.sh` finds the newest `/usr/lib/llvm-*` by itself. To pick one, set `HYPER_LLVM`:

```bash
export HYPER_LLVM=/usr/lib/llvm-23
```

## 1. Build the JavaScript parts

```bash
npm ci
make check
```

`make check` runs the type check, the linter, the tests and a check that the version in `package.json` matches `sdk/native/hpdf.h`. It ends with a line like:

```text
version 1.1.4
```

Without the engine programs, the tests that need them are skipped and everything else runs, so this works on a fresh checkout.

## 2. Get the PDFium checkout

The engine compiles into a pinned PDFium checkout. The pin is `PDFIUM_PIN` in `core/build/apply-tree.sh`. You fetch the checkout once; it lives in `core/build/gsrc` and git ignores it.

On macOS, Linux x64 and Windows:

```bash
PIN="$(grep '^PDFIUM_PIN=' core/build/apply-tree.sh | cut -d= -f2)"
mkdir -p core/build/gsrc && cd core/build/gsrc
gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
gclient sync --revision "pdfium@$PIN" -D --no-history
```

On Linux ARM64, the checkout needs two extra settings so that `gclient` fetches the ARM64 build of `gn`:

```bash
PIN="$(grep '^PDFIUM_PIN=' core/build/apply-tree.sh | cut -d= -f2)"
mkdir -p core/build/gsrc && cd core/build/gsrc
GN="$(curl -fsSL "https://pdfium.googlesource.com/pdfium/+/$PIN/DEPS?format=TEXT" | base64 -d | grep -o "'gn_version': '[^']*'" | cut -d"'" -f4)"
printf '%s\n' 'solutions = [{"name": "pdfium", "url": "https://pdfium.googlesource.com/pdfium.git", "managed": False,' \
  "  \"custom_vars\": {\"reclient_package\": \"gn/gn/\", \"reclient_version\": \"$GN\"}}]" > .gclient
gclient sync --revision "pdfium@$PIN" -D --no-history
```

This is the same recipe the release and the Dockerfile use.

## 3. Build the native engine

Run these from the repository root.

::: code-group

```bash [macOS]
core/build/apply-tree.sh
core/build/build-native.sh
sdk/native/build-dylib.sh
```

```bash [Linux]
docker/build-jpegli-linux.sh
docker/build-linux.sh
cli/build-qpdf.sh
```

:::

On macOS, `make build` runs the same three commands.

What each script does:

| Script | What it does |
|---|---|
| `core/build/apply-tree.sh` | Copies the engine sources and patches into the PDFium checkout, builds the PDFium archive with `gn` and `ninja`, checks the `HyperCompress_*` exports and stages the archive in `core/prebuilt/`. macOS only. |
| `core/build/build-native.sh` | Links `cli/prebuilt/hpdf-worker` and `cli/prebuilt/hpdf-render` against the archive and jpegli. Run it alone when only the worker or the jpegli wrapper changed. macOS only. |
| `sdk/native/build-dylib.sh` | Builds the [C library](/c-api), `sdk/native/lib/libhypercompress.dylib`. macOS only. |
| `docker/build-jpegli-linux.sh` | Builds the jpegli JPEG encoder from libjxl v0.11.1 into `core/third_party/jpegli/linux-<cpu>/`. `build-linux.sh` runs it when the library is missing. |
| `docker/build-linux.sh` | Builds the PDFium archive and links `hpdf-worker` and `hpdf-render` for the machine's CPU (x64 or ARM64). On ARM64 it uses the LLVM from `HYPER_LLVM` or `/usr/lib/llvm-*`. |
| `cli/build-qpdf.sh` | Builds qpdf 12.3.2 with no outside library dependencies into `cli/prebuilt/qpdf`. |

The engine is built without PDFium's JavaScript engine (V8), which a compressor does not need. On macOS the repository already includes a prebuilt `cli/prebuilt/qpdf`; run `cli/build-qpdf.sh` only to rebuild it.

Linux builds the CPU it runs on. To build for ARM64, build on an ARM64 machine.

### Windows

There is no Windows build script. The `windows-x64` job in [`.github/workflows/release.yml`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/.github/workflows/release.yml) is the recipe: it builds the PDFium archive with `gn` and `ninja`, links `hpdf-worker.exe` and `hpdf-render.exe` with PDFium's `clang-cl` and `lld-link`, and downloads qpdf 12.3.2 for Windows with a pinned checksum.

## 4. Build the Rust programs

The Rust workspace in `rust/` holds four crates:

| Crate | Builds |
|---|---|
| `hyper-compress` | the core library, used by everything below and by [Rust programs](/rust) |
| `hyper-compress-node` | the Node.js addon |
| `hyper-compress-cli` | the `hyper` CLI |
| `hyper-compress-server` | `hyper-server`, the [HTTP service](/http-api) |

Build the Node.js addon and copy it next to the engine, where the Node.js SDK looks for it:

::: code-group

```bash [macOS]
cargo build --release --locked -p hyper-compress-node --manifest-path rust/Cargo.toml
cp rust/target/release/libhyper_compress_node.dylib cli/prebuilt/hyper-compress.node
```

```bash [Linux]
cargo build --release --locked -p hyper-compress-node --manifest-path rust/Cargo.toml
cp rust/target/release/libhyper_compress_node.so cli/prebuilt/hyper-compress.node
```

```bash [Windows]
cargo build --release --locked -p hyper-compress-node --manifest-path rust/Cargo.toml
cp rust/target/release/hyper_compress_node.dll cli/prebuilt/hyper-compress.node
```

:::

Build the CLI and the HTTP service:

```bash
cargo build --release --locked -p hyper-compress-cli -p hyper-compress-server \
  --manifest-path rust/Cargo.toml
```

This writes `rust/target/release/hyper` and `rust/target/release/hyper-server`. Both look for `hpdf-worker` and `qpdf` next to themselves, then in `cli/prebuilt` up to 8 folders above them, so they find the engine when run from the checkout. Set `HYPER_DRV` and `HYPER_QPDF` to point elsewhere.

Now the npm `hyper` command runs on your build:

```bash
npm run hyper -- input.pdf output.pdf
```

## 5. Build WebAssembly

With Emscripten active and the same PDFium checkout:

```bash
make wasm
```

This runs `wasm/build-pdfium-wasm.sh` and `wasm/build-wasm.sh` and writes `wasm/out/hyper-compress.js` and `wasm/out/hyper-compress.wasm`. The PDFium checkout keeps working for native builds.

Check that the WebAssembly build gives the same output as the native one:

```bash
npm run build
node wasm/verify.mjs input.pdf
```

It compresses the file with every preset on both builds and compares the bytes and the rendered pages. `make verify PDF=input.pdf` does the same.

## 6. Build the Docker image

From the repository root:

```bash
docker build -f docker/Dockerfile -t hyper-compress .
```

The Dockerfile does the whole native build inside the container: the PDFium checkout, the engine, and the Rust `hyper` and `hyper-server` programs. It builds for the CPU of the machine that runs Docker; on an ARM64 machine it installs LLVM 23 itself. The final image is `debian:trixie-slim` with Debian's qpdf. The first build downloads the full PDFium checkout and takes a long time.

Run it as in [Self-hosting](/self-hosting):

```bash
docker run -d -p 8080:8080 hyper-compress
```

## 7. Run the tests

| Command | What it runs |
|---|---|
| `make check` | type check, lint, the Node.js tests and the version check |
| `cargo test --workspace --manifest-path rust/Cargo.toml` | the Rust tests for all four crates |
| `make regress` | every PDF in `tests/regression/failset` with every preset; each file there once exposed a real bug |
| `make bench CORPUS=path/to/folder` | compresses a folder of PDFs and writes `bench/results.json` |
| `make fuzz` | builds the libFuzzer harness and fuzzes for 60 seconds; needs LLVM clang (`brew install llvm` on macOS) |
| `make smoke PDF=input.pdf` | one quick compression through the built SDK |

With the engine built, `make check` and `cargo test` also run the tests that use the real engine. `make regress` is the gate for engine bug fixes: a clean run reports zero failing pairs.

What runs in CI:

| Workflow | When | What |
|---|---|---|
| `ci.yml` | every push and pull request | `make check`, `cargo test`, `cargo clippy` and shell script checks, without the engine |
| `build.yml` | started by hand | builds the engine and runs `npm test` and `cargo test` against it on Linux x64, Linux ARM64, macOS and Windows; builds and tests the Docker image on x64 and ARM64 |
| `cflite_pr.yml` | pull requests that change `core/` or `fuzz/` | 5 minutes of fuzzing |
| `cflite_batch.yml` | every night | one hour of fuzzing |

## How releases are built

Releases are built in CI from a version tag, on real machines for each platform. [`RELEASE.md`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/RELEASE.md) describes the release command, the files each release attaches, the Docker images and the npm publish. [Platforms](/platforms#release-files) lists the release files.

## Troubleshooting

**`ERROR: tree at <commit>, pin is <commit>`.** Your PDFium checkout is at a different commit. Run `gclient sync --revision pdfium@<pin> -D` in `core/build/gsrc`.

**`PDFium's clang does not run on aarch64 and no LLVM was found`.** You are on Linux ARM64 without LLVM. Install LLVM 23 as shown in [Prerequisites](#prerequisites), or set `HYPER_LLVM` to its folder.

**`compiler-rt builtins missing`.** Install the compiler-rt package for your LLVM, for example `libclang-rt-23-dev`.

**gn fails with a `pyexpat` error.** Your `python3` is broken. Put a working one first on your `PATH`; on macOS, Xcode's `/usr/bin/python3` works.

**`static libjpeg-turbo not found`.** `cli/build-qpdf.sh` needs a static libjpeg: `apt-get install libjpeg-dev` on Linux, `brew install jpeg-turbo` on macOS.

**`emcc not on PATH`.** Activate Emscripten in this shell with `source <emsdk>/emsdk_env.sh` before `make wasm`.

## Next steps

- [Platforms](/platforms): which platforms have prebuilt packages.
- [Self-hosting](/self-hosting): run the Docker image you built.
- [C API](/c-api): embed the library you built.
- [Rust crate](/rust): use the core library from Rust.
