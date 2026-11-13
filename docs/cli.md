---
title: CLI
description: Every flag, output line, warning and exit code of the hyper command, from npm or from the Docker image.
---

# CLI

The `hyper` command compresses one PDF per run with Hyper Compress. It takes an input path, an optional output path and flags, prints where the result went and how much smaller it is, and exits with a code your scripts can check.

```bash
npm install -g hyper-compress
hyper input.pdf output.pdf
```

```text
output.pdf
  2593056 -> 1250960 bytes (52% smaller)
```

## Install

There are two builds of the command. They take the same flags and print the same output.

| Build | Install | Needs |
|---|---|---|
| npm `hyper` | `npm install -g hyper-compress` | Node.js 22 or newer on macOS (Apple silicon), Linux (x64 or ARM64) or Windows (x64) |
| Standalone `hyper` binary in the Docker image | `docker pull ghcr.io/alam00000/bentopdf-hyper-compress:latest` | Docker, on any x64 or ARM64 host. See [Docker CLI mode](#docker-cli-mode). |

The npm package installs the engine for your platform through a platform package (`hyper-compress-darwin-arm64`, `hyper-compress-linux-x64`, `hyper-compress-linux-arm64` or `hyper-compress-win32-x64`), with no download scripts. [Node.js SDK](/node-sdk#how-hyper-finds-the-engine) explains where the command looks for the engine files.

To run the npm command without a global install:

```bash
npx -p hyper-compress hyper input.pdf output.pdf
```

Check the install:

```bash
hyper --version
```

```text
1.1.4
```

## Synopsis

```text
hyper <input.pdf> [output.pdf] [--preset low|medium|high|lossless]
                               [--password PW] [--set key=value ...]
                               [--brotli] [--mrc off|auto|force]
                               [--target-size N[KB|MB]] [--report FILE]
hyper --help
hyper --version
```

- The first argument that does not start with `-` is the input. The second is the output. Any further plain arguments are ignored.
- Flags can come before, between or after the paths.
- A flag given twice keeps its last value.
- Leave out the output path and Hyper writes `<name>-compressed.<ext>` in the input's folder, keeping the input's extension (`.pdf` if it has none). `hyper report.pdf` writes `report-compressed.pdf`.
- The output can be the input path. Hyper writes the result to a temporary file beside it and then renames it into place, so the input is only replaced by a complete file. When Hyper keeps the original, the file is left as it is.

## Flags

| Name | Type | Default | Description |
|---|---|---|---|
| `--preset <name>` | `low`, `medium`, `high`, `lossless` | `medium` | The starting set of [options](/options). Any other value is a usage error. |
| `--password <pw>` | text | none | Password for an encrypted input. A compressed output is written decrypted. |
| `--target-size <size>` | size | none | Aim for an output at or below this size. See [Size units](#size-units). |
| `--set <key=value>` | `key=value` | none | Override one [option](/options) on top of the preset. Repeat for more. |
| `--brotli` | switch | off | Same as `--set brotli=true`. |
| `--mrc <mode>` | `off`, `auto`, `force` | `off` | Same as `--set mrc=<mode>`. Any other value is a usage error. |
| `--report <file>` | path | none | Write a JSON [compression report](/report) to this file. |
| `-h`, `--help` | switch | | Print the full help to stdout and exit 0. |
| `-v`, `-V`, `--version` | switch | | Print the version number to stdout and exit 0. |

Any other argument that starts with `-` is a usage error (`unknown flag <flag>`).

### --preset

```bash
hyper input.pdf output.pdf --preset high
```

| Preset | Images | Use it for |
|---|---|---|
| `low` | quality 80, up to 200 dpi | archiving and printing; minimal visible change |
| `medium` | quality 50, up to 150 dpi | the default; sharing and email |
| `high` | quality 20, up to 72 dpi; standard fonts left to the viewer | the smallest file, for reading on screen |
| `lossless` | images are not re-encoded | files where every pixel must stay the same |

[Choose the right settings](/guides/choosing-settings) compares them in detail.

### --password

```bash
hyper input.pdf output.pdf --password hunter2
```

A wrong password fails with `error: decrypt_failed` and exit code 1, and no output is written. A PDF that needs a password to open, run without `--password`, fails with `error: the PDF is encrypted and needs a password`. Only the text before the first line break of the password is used.

What you get back:

- When Hyper compresses the file, the output is decrypted.
- When nothing is smaller than your file, or the file is signed, you get your original bytes, still encrypted.
- A file with only an owner password (it opens without a password but restricts printing or editing) compresses without `--password`, and the output keeps that owner password.

See [Encrypted PDFs](/guides/encrypted-pdfs).

### --target-size

```bash
hyper input.pdf output.pdf --target-size 1MB
```

```text
output.pdf
  2593056 -> 1023827 bytes (61% smaller) [target met]
```

Hyper searches for the highest image quality that fits under the target. If nothing fits, it keeps going: quality down to 5, then resolution down through 72, 50 and 36 dpi. If the target is still out of reach, you get the smallest file it made, `[target NOT met - best effort]`, and a warning with the size it reached. The command still exits 0.

Two cases skip the search, each with its own warning when the target is missed:

- With `--set preserveConformance=true` on a PDF/A file (the archiving version of PDF), Hyper compresses once and reports whether that met the target.
- A signed file is returned untouched. Add `--set removeSignatures=true` to compress it.

See [Hit a file size target](/guides/size-targets).

### --set

```bash
hyper input.pdf output.pdf --set grayscale=true --set imageQuality=40
```

Each `--set` overrides one option, by its camelCase name from the [options reference](/options). A later `--set` for the same key wins, and so does a later `--brotli` or `--mrc` against an earlier `--set`, and the other way round.

How the value is read:

| You write | Hyper receives |
|---|---|
| `true` or `false` | a boolean |
| a decimal number (`40`, `1.7`, `-2`) | a number |
| anything else, including an empty value | text |

Then each option is checked:

- A number outside the option's range is clamped to the nearest end. `imageQuality=500` becomes 100.
- A value of the wrong type (`grayscale=yes`, `imageQuality=abc`) or an unknown choice (`colorTarget=purple`) is replaced by the option's fallback default, the `Default` column in the [options reference](/options). That is not always the preset's value: `--set reduceColor=1` turns `reduceColor` off.
- An unknown name (`--set imageQualty=40`) is ignored without an error. Names are case sensitive.
- `removeAnnots=true` with `annotations` left at `keep` means `annotations=flatten`.

Numbers are read the way JavaScript's `Number` reads them, in both builds: `0x10`, `0o17`, `0b101` and `1e3` are numbers too. Number-like choices work as numbers: `--set minVersion=1.7`, `--set minVersion=2.0` and `--set jpegSubsampling=420` all select the matching choice.

More examples:

```bash
hyper input.pdf output.pdf --set colorTarget=cmyk
hyper input.pdf output.pdf --set maxDpiColor=150 --set maxDpiGray=150 --set maxDpiMono=300
hyper input.pdf output.pdf --set annotations=remove --set removeActions=true --set removeMetadata=true
hyper input.pdf output.pdf --set linearize=true --set minVersion=1.7
hyper input.pdf output.pdf --set rasterizePages=true --set rasterizeDpi=100
hyper input.pdf output.pdf --set preserveConformance=true
```

### --brotli

```bash
hyper input.pdf output.pdf --brotli
```

Compresses the file's internal data with Brotli, a PDF 2.0 feature. The file gets smaller, but the reader must support it (MuPDF 1.26 and newer, Ghostscript 10.06 and newer, Firefox), and many cannot open it yet. Off by default for that reason. Ignored, with a warning, when `preserveConformance` keeps a PDF/A file conformant.

### --mrc

```bash
hyper input.pdf output.pdf --mrc auto
hyper input.pdf output.pdf --mrc auto --set mrcBackgroundDpi=70 --set mrcBackgroundQuality=25
```

Mixed raster content (MRC) for scanned documents: each scanned page is split into a sharp black-and-white text layer and low-resolution colour layers, which is usually much smaller than one full-resolution picture of the page. `auto` only touches pages that look like scans; `force` tries every page. Not applied with `--preset lossless`. See [Scanned documents](/guides/scanned-documents).

### --report

```bash
hyper input.pdf output.pdf --report report.json
```

Writes a JSON report of every font, every page image and every cleanup, before and after. It adds two read passes, so leave it off when you do not need it. If the PDF structure cannot be read, the compression still succeeds, no file is written, and you get the warning `report unavailable: the PDF structure could not be read`. The format is in [Compression report](/report).

### --help and --version

```bash
hyper --help
hyper --version
```

`--help` prints every flag with examples. `--version` prints the version number alone, for example `1.1.4`. Both print to stdout and exit 0. A usage error anywhere on the line still wins: `hyper --help --bogus` exits 2.

## Output

On success the command prints two lines to stdout:

```text
output.pdf
  2593056 -> 1250960 bytes (52% smaller)
```

The first line is the output path, as you gave it or as Hyper built it. The second is the input size, the output size and the saving, rounded to a whole percent. The output is never larger than the input, so the saving is never negative.

Suffixes can follow on the second line:

| Suffix | When |
|---|---|
| ` [signed: original preserved]` | The input is digitally signed, so Hyper returned it unchanged, byte for byte, to keep the signature valid. Also shown when you set `removeSignatures` but the compressed file was not smaller, so the signed original came back. See [Signed documents](/guides/signed-documents). |
| ` [target met]` | You used `--target-size` and the output is at or below it. |
| ` [target NOT met - best effort]` | You used `--target-size` and even the smallest result is above it. |

`0% smaller` with equal sizes means Hyper could not make the file smaller and wrote your original bytes. It is an outcome, not an error.

## Warnings

When Hyper skips something you asked for, it prints one `warning:` line per skipped item to stderr, before the result lines, and still exits 0:

```text
warning: rasterize skipped: document has a text layer
output.pdf
  2593056 -> 1252473 bytes (52% smaller)
```

Every warning and what to do about it is in [Warnings](/warnings).

## Exit codes

| Code | Meaning | Output |
|---|---|---|
| `0` | Success. Includes "no gain" (original kept), signed inputs and missed size targets. Also `--help` and `--version`. | Result on stdout, warnings on stderr |
| `1` | The compression failed. | `error: <message>` on stderr; no output file |
| `2` | Bad usage. | `error: <message>`, the usage text and `run "hyper --help" for the full option list` on stderr |

Usage messages (exit 2):

| Message | Cause |
|---|---|
| `an input PDF is required` | No input path. |
| `--preset expects one of low, medium, high, lossless` | Missing or unknown preset. |
| `--mrc expects off, auto or force` | Missing or unknown mode. |
| `--target-size expects a size like 2MB, 500KB or a byte count` | The size cannot be read. |
| `--target-size must be greater than zero` | The size rounds down to 0 bytes. |
| `--report expects a file path` | No path after `--report`. |
| `--set expects key=value` | No `=`, or nothing before it. |
| `unknown flag <flag>` | An argument starting with `-` that is not a flag. |

Failure messages (exit 1):

| Message | Cause |
|---|---|
| `error: decrypt_failed` | The password is wrong. |
| `error: the PDF is encrypted and needs a password` | The input needs a password and you gave none. Add `--password`. |
| `error: the input is not a PDF: no %PDF- header in its first 1024 bytes` | The input is not a PDF. |
| `error: cannot read <path>: <reason>` | The input cannot be read, for example `No such file or directory (os error 2)`. |
| `error: no hpdf-worker binary for <platform>: ...` | No engine for this platform, for example `linux-x64` on Alpine. See [Troubleshooting](#troubleshooting). |
| `error: no hyper-compress.node binary for <platform>: ...` | npm command only: the native addon is missing. |
| `error: hpdf-worker not found at <path>` | `HYPER_DRV` points to a file that does not exist. |
| `error: qpdf not found at <path>` | `HYPER_QPDF` points to a file that does not exist. |
| `error: the engine pass ran longer than the 600000 ms stage limit` | One step ran past the 10 minute limit. The same message starts with `decryption` or `qpdf` for those steps. |
| `error: <reason>` | The output or report could not be written, for example `No such file or directory (os error 2)` when the output folder does not exist. The wording comes from your operating system. |

The full list across every surface is in [Errors](/errors).

## Size units

`--target-size` takes a number with an optional unit, in any letter case:

| Unit | Bytes | Example | Result |
|---|---|---|---|
| none or `B` | 1 | `500000` | 500,000 bytes |
| `K` or `KB` | 1,024 | `500KB` | 512,000 bytes |
| `M` or `MB` | 1,048,576 | `2MB` | 2,097,152 bytes |

Decimals work (`1.5MB` is 1,572,864 bytes) and the result is rounded down to whole bytes. There is no `GB`. Negative numbers, `.5MB` and `5.MB` are rejected.

## Defaults

With no flags, `hyper input.pdf` runs:

- the `medium` preset: images at quality 50 and at most 150 dpi, fonts subset, duplicates merged;
- no password, no size target, no report;
- each step (decryption, the engine pass, each repack) limited to 10 minutes. A slower step is stopped and the command fails with `error: ... ran longer than the 600000 ms stage limit`;
- the output written as `<name>-compressed.<ext>` beside the input.

## Docker CLI mode

The Docker image is also the CLI, with no Node.js needed. Pass the paths and flags after the image name:

```bash
docker run --rm -v "$PWD:/work" ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf --preset high
```

- `-v "$PWD:/work"` mounts your current folder at `/work`, the container's working folder, so relative paths are read and written there.
- The container runs as the user `hyper` (uid 1000). The mounted folder must be writable by uid 1000. If it is not, run as yourself with `--user "$(id -u):$(id -g)"`.
- With no arguments, or with `serve` as the first argument, the image starts the [HTTP service](/self-hosting) instead.
- `--version` prints the version of the standalone binary.

```bash
docker run --rm ghcr.io/alam00000/bentopdf-hyper-compress:latest --help
```

## Troubleshooting

**`hyper: command not found`.** Install globally with `npm install -g hyper-compress`, or use `npx -p hyper-compress hyper`. If a global install is still not found, npm's global bin folder is not on your `PATH`; `npm prefix -g` shows where npm installs.

**`error: no hpdf-worker binary for <platform>`.** There is no native engine for your platform, for example an Intel Mac or Alpine Linux (npm skips the Linux packages on musl systems). Use [Docker CLI mode](#docker-cli-mode), the [WebAssembly package](/wasm), or [build the engine](/building) and set `HYPER_DRV` to it.

**The output is `0% smaller`.** Hyper could not make the file smaller with these settings and returned your original. Try `--preset high`, a [size target](/guides/size-targets), or for image-only scans `--mrc auto`.

**A warning mentions rasterize.** You set `rasterizePages` on a document that has real text. Hyper refuses rather than turn the text into a picture. Set `rasterizeText=true` as well if you want that anyway.

**`error: hpdf-worker not found at <path>`.** `HYPER_DRV` points to a file that does not exist. Fix the path, or unset it to use the installed engine.

**`error: ... ran longer than the 600000 ms stage limit`.** A step took more than 10 minutes. Very large scans can; try a lighter preset, or use the [Node.js SDK](/node-sdk) where the limit is `timeoutMs`.

**`error: Permission denied (os error 13)` in Docker.** The mounted folder is not writable by uid 1000. Add `--user "$(id -u):$(id -g)"`.

## Next steps

- [Options](/options): every option `--set` accepts, with ranges and defaults.
- [Choose the right settings](/guides/choosing-settings): which preset and overrides fit which document.
- [Warnings](/warnings): every warning line and what to do.
- [Errors](/errors): every error message and exit code.
- [Node.js SDK](/node-sdk): the same engine from your own code.
