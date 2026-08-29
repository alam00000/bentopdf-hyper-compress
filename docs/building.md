# Building from Source

The complete build documentation, including the patch inventory and every platform note, lives in [BUILDING.md](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/BUILDING.md) in the repository. This page is the orientation.

Most people never need this: prebuilt binaries ship with [every release](https://github.com/alam00000/bentopdf-hyper-compress/releases) and through npm. Build from source when you are changing the engine, packaging for an unsupported platform, or verifying the binaries yourself.

## What you need

- Node.js 22 or newer for everything JavaScript
- a C++ toolchain (Xcode Command Line Tools on macOS, build-essential plus cmake and ninja on Linux) for the native engine
- Google's depot_tools on PATH, python3, and roughly 10 GB of disk for the one-time PDFium checkout
- Emscripten, only if you build the WebAssembly module

## The JavaScript parts

Node 22 and nothing else:

```bash
npm ci
make check      # typecheck, lint, unit tests, version lock
```

Without built native workers the integration tests skip and everything else still runs, so this works on a bare checkout.

## The native engine

The engine compiles into a pinned Google PDFium checkout as extra translation units. The bootstrap is a one-time, roughly 10 GB `gclient` checkout; after that:

```bash
core/build/apply-tree.sh      # patch the tree, build the archive
core/build/build-native.sh    # link the worker binaries
```

`apply-tree.sh` copies the engine sources and full-file patches into the checkout, runs `gn` and `ninja`, verifies the exported symbols and stages the archive. The build is non-V8 (a compressor needs no JS engine), which roughly quarters the archive size.

## WebAssembly

Needs Emscripten and the same pinned checkout:

```bash
make wasm
```

`node wasm/verify.mjs some.pdf` then asserts byte and pixel parity between the wasm and native builds. This parity is why the browser tool, the wasm npm package and the native engine can honestly claim identical output.

## Testing

```bash
make check      # typecheck, lint, unit and integration tests
make regress    # the regression failset, every preset
make bench CORPUS=path/to/dir
make fuzz       # libFuzzer harness
```

`make regress` is the gate for bug fixes: every PDF in the failset once exposed a real bug, and a clean run reports zero failing pairs. Fuzzing runs through ClusterFuzzLite on every pull request that touches the engine.
