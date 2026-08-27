# CLI

The `hyper` command ships with the [`hyper-compress`](https://www.npmjs.com/package/hyper-compress) npm package and drives the native engine. It needs Node.js 22 or newer; see [Getting Started](/getting-started#what-you-need-first) if you have not installed it.

## Your first compression

```bash
npm install -g hyper-compress
hyper input.pdf output.pdf
```

Expected output:

```
output.pdf
  2593056 -> 1250960 bytes (52% smaller)
```

Reading it: the first line is the path the result was written to. The second line is input size, output size, and the saving. Two suffixes can appear on that line:

- `[signed: original preserved]` means the input was digitally signed, so the engine returned it byte for byte rather than break the signature.
- `[target met]` or `[target NOT met - best effort]` appears when you used `--target-size`.

If the engine skipped anything you asked for, it prints `warning:` lines to stderr explaining what and why, for example a rasterize request refused because the document has selectable text.

## Usage

```
hyper <input.pdf> <output.pdf> [--preset low|medium|high|lossless]
                               [--password PW] [--set key=value ...]
                               [--brotli] [--target-size N[KB|MB]]
```

Two flags answer questions rather than compress anything:

```bash
hyper --help      # the full option list with examples (also -h)
hyper --version   # the installed version (also -v)
```

Both print to stdout and exit 0, so `hyper --version` is safe to use in scripts.

## Flags, one by one

### --preset

```bash
hyper in.pdf out.pdf --preset high
```

`low`, `medium` (the default), `high` or `lossless`. `medium` is right for most documents; `high` is the smallest and fine for screen reading; `lossless` re-encodes no images at all. The [preset table](/getting-started#the-presets) compares them.

### --password

```bash
hyper locked.pdf out.pdf --password hunter2
```

For encrypted input. A wrong password fails with `error: decrypt_failed`; there is no partial output.

### --target-size

```bash
hyper scan.pdf out.pdf --target-size 2MB
```

Accepts `KB`, `MB` or a bare byte count. The engine searches for the highest image quality that still fits under your target, and if nothing fits it keeps pushing: quality down to 5, then resolution down through 72, 50 and 36 dpi.

If the target is reachable you get the best looking file that fits. If it is not, you get the smallest file the engine could produce, a `[target NOT met - best effort]` note, and a warning saying how small it managed to get. The command still exits 0, because compressing succeeded even though the target did not. For image-only documents that still miss, add `--set rasterizePages=true`, which can go dramatically further.

### --set

```bash
hyper in.pdf out.pdf --set grayscale=true --set imageQuality=40
```

Overrides any of the 28 engine options on top of the preset, by its camelCase name. `true`, `false` and numbers are parsed; the flag repeats. Every name, range and default is in the [options reference](/options). Two useful examples:

```bash
hyper scan.pdf out.pdf --set rasterizePages=true --set rasterizeDpi=100
hyper report.pdf out.pdf --set preserveConformance=true
```

The first flattens an image-only scan to rendered pages, which can be dramatic on bloated scans. The second keeps a PDF/A document conformant, automatically disabling anything that would break the standard.

### --brotli

```bash
hyper in.pdf out.pdf --brotli
```

Shorthand for `--set brotli=true`: PDF 2.0 Brotli stream compression. Smaller files, but the reader must support it (MuPDF 1.26 and newer, Ghostscript 10.06 and newer, Firefox). Off by default for exactly that reason.

### --help and --version

```bash
hyper --help      # or -h
hyper --version   # or -v
```

`--help` prints every flag with examples; `--version` prints just the version number, like `0.1.0`, which makes it easy to check in scripts or bug reports.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success, including "no gain" and "target not met" outcomes |
| 1 | any error; the message is on stderr |
| 2 | bad usage; the usage text is on stderr |

## Troubleshooting

**`hyper: command not found`.** Either install globally (`npm install -g hyper-compress`) or run it through npx from a folder where you installed it (`npx hyper ...`). If a global install still is not found, your npm global bin directory is not on PATH; `npm bin -g` shows where it is.

**`error: decrypt_failed`.** The input is encrypted and the password is wrong or missing. Pass `--password`.

**`error: no hpdf-worker binary for <platform>`.** There is no native engine build for your platform (for example Intel macOS or ARM Linux). Use the [WebAssembly package](/wasm), or build from source and point `HYPER_DRV` at your binary.

**The output says "no gain, original kept" or 0% smaller.** The document is already as small as this engine can make it at that preset; you received your original bytes back. Try `--preset high`, or for image-only scans, `--set rasterizePages=true`.

**The file barely shrank and a warning mentions rasterize.** You asked for `rasterizePages` on a document that has a text layer. The engine refuses rather than destroy searchable text; that refusal is the warning. See [guarantees](/guarantees#rasterization-cannot-destroy-text).

## Docker CLI mode

No Node.js at all? The self-hosted image doubles as the CLI:

```bash
docker run --rm -v "$PWD:/work" ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  in.pdf out.pdf --preset high
```

`-v "$PWD:/work"` mounts your current folder into the container so it can read the input and write the output there.
