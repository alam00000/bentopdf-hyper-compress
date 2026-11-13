---
title: Rust crate
description: Reference for the hyper-compress Rust crate, the core that the Node.js SDK, the CLI and the HTTP service are built on.
---

# Rust crate

The `hyper-compress` crate is the core of Hyper Compress: decryption, the engine pass, repacking, the size target search, PDF/A handling and the report. The [Node.js SDK](/node-sdk), the [CLI](/cli) and the [HTTP service](/http-api) are all built on it. Use it to compress PDFs from a Rust program.

```toml
[dependencies]
hyper-compress = { git = "https://github.com/alam00000/bentopdf-hyper-compress" }
```

::: warning
The crate is not on crates.io yet, and it is not in any release tag (v1.1.4 and earlier predate it). Use it from the Git repository. Its API may change before it is published; pin a commit with `rev` to stay on a version you have tested.
:::

## Install

Add the dependency to your `Cargo.toml`:

```toml
[dependencies]
hyper-compress = { git = "https://github.com/alam00000/bentopdf-hyper-compress" }
serde_json = "1"
```

To pin a commit:

```toml
hyper-compress = { git = "https://github.com/alam00000/bentopdf-hyper-compress", rev = "<commit>" }
```

The crate lives in `rust/hyper-compress/`, a member of the Cargo workspace in `rust/`, not at the repository root. That works as is: for a Git dependency, Cargo searches the whole repository for a `Cargo.toml` whose package name is `hyper-compress`, and reads the workspace it belongs to. Its own dependencies are `serde`, `serde_json`, `base64` and `tempfile`.

`serde_json` is in the example above because options are passed as a `serde_json::Map`.

## Get the engine binaries

The crate does not contain the engine. It runs two programs:

| Program | Needed for |
|---|---|
| `hpdf-worker` | Every compression. Required. |
| `qpdf` | Passwords (required when you pass one), repacking, linearization, the report and PDF/A claim removal. Without it, compression still works but skips those steps. |

Get them in one of these ways:

- From a [GitHub release](https://github.com/alam00000/bentopdf-hyper-compress/releases): `hyper-compress-<version>-macos-arm64.tar.gz`, `-linux-x64`, `-linux-arm64` or `-windows-x64`. Each holds `hpdf-worker`, `qpdf`, `hpdf-render` and `hyper-compress.node` (the Windows one also holds the DLLs `qpdf` needs). You only need `hpdf-worker` and `qpdf`.
- From the npm platform package for your machine, for example `hyper-compress-linux-x64`, in its `bin/` folder.
- By [building the engine](/building), which puts them in `cli/prebuilt/`.

Binaries built from the same commit as the crate are the safest choice. A binary from an older release may not support every option.

Then tell the crate where they are with [`Binaries`](#binaries).

## Example

```rust
use std::path::PathBuf;

use hyper_compress::{compress, Binaries, CompressInput, Preset};
use serde_json::{json, Map, Value};

fn main() {
    let mut options = Map::new();
    options.insert("grayscale".into(), json!(true));

    let input = CompressInput {
        source_path: PathBuf::from("input.pdf"),
        save_path: Some(PathBuf::from("output.pdf")),
        preset: Some(Preset::Medium),
        options,
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };

    match compress(&input) {
        Ok(result) => {
            println!("{} -> {} bytes", result.original_size, result.compressed_size);
            for warning in &result.warnings {
                eprintln!("warning: {warning}");
            }
            let json: Value = serde_json::to_value(&result).unwrap();
            println!("{json}");
        }
        Err(e) => {
            eprintln!("error: {} ({})", e.message, e.code.as_str());
            std::process::exit(1);
        }
    }
}
```

Put `hpdf-worker` and `qpdf` next to your program, or set `HYPER_DRV` and `HYPER_QPDF`:

```bash
HYPER_DRV=/opt/hyper/hpdf-worker HYPER_QPDF=/opt/hyper/qpdf cargo run
```

```text
2593056 -> 1432404 bytes
{"compressedSize":1432404,"metTarget":null,"originalSize":2593056,"outputPath":"output.pdf","pdfa":null,"pdfaPreserved":false,"signaturesRemoved":false,"signed":false,"warnings":[]}
```

## compress

```rust
pub fn compress(input: &CompressInput) -> Result<CompressResult>
```

Compresses one PDF from a file to a file. It blocks the calling thread until the work is done and starts the engine as a separate process. In async code, call it from a blocking thread, for example `tokio::task::spawn_blocking`.

The output file is written at the end. When `compress` returns an error, no output file has been written, unless writing the report file failed after the output was written.

`save_path` can be the same as `source_path`. The result is written to a temporary file beside the output and renamed into place, and when Hyper keeps the original, the file is left as it is.

### CompressInput

```rust
#[derive(Clone, Default)]
pub struct CompressInput { /* fields below */ }
```

| Field | Type | Default | Description |
|---|---|---|---|
| `source_path` | `PathBuf` | empty | The input PDF. Empty returns `EngineError` (`sourcePath is required`). |
| `save_path` | `Option<PathBuf>` | `None` | Where to write the output. `None` or empty writes `<stem>-compressed.<ext>` beside the input, keeping its extension (`.pdf` if it has none). |
| `password` | `Option<String>` | `None` | Password for an encrypted input. `None` or empty means none. Only the text before the first line break is used. |
| `preset` | `Option<Preset>` | `None` | The starting [preset](#preset). `None` means `Preset::Medium`. |
| `options` | `Map<String, Value>` | empty | Overrides on top of the preset, keyed by camelCase [option](/options) name. See [Options](#options). |
| `target_size_bytes` | `Option<u64>` | `None` | Aim for an output at or below this many bytes. `Some(0)` means no target. |
| `report` | `ReportRequest` | `ReportRequest::None` | Whether to build a [report](/report). See [ReportRequest](#reportrequest). |
| `stage_timeout` | `Option<Duration>` | `None` | Limit for each separate process. A step that runs longer returns `Timeout`. `None` means `DEFAULT_STAGE_TIMEOUT` (10 minutes). Must be greater than zero. |
| `total_timeout` | `Option<Duration>` | `None` | Limit for the whole call. `None` or zero means no limit. |
| `cancel` | `CancelToken` | a new token | Cancels the call from another thread. See [Cancellation](#cancellation). |
| `binaries` | `Binaries` | `Binaries::default()` | Where to find `hpdf-worker` and `qpdf`. See [Binaries](#binaries). |

Build it with struct update syntax and `..Default::default()`, as in the example.

### CompressResult

```rust
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompressResult { /* fields below */ }
```

| Field | Type | Description |
|---|---|---|
| `output_path` | `PathBuf` | Where the output was written. |
| `original_size` | `u64` | Input size in bytes. |
| `compressed_size` | `u64` | Output size in bytes. Never larger than `original_size`. Equal means Hyper kept the original. |
| `signed` | `bool` | `true` when the input is digitally signed and was returned unchanged. |
| `signatures_removed` | `bool` | `true` when `removeSignatures` removed signatures and the compressed file was used. |
| `pdfa` | `Option<PdfaLevel>` | The PDF/A (archiving PDF) level the input claims, or `None`. |
| `pdfa_preserved` | `bool` | `true` when the output still carries the input's PDF/A claim and meets it. |
| `met_target` | `Option<bool>` | Whether the output is at or below `target_size_bytes`. `None` when no target was set. |
| `warnings` | `Vec<String>` | Everything Hyper skipped and why. See [Warnings](/warnings). |
| `report` | `Option<Report>` | The report, when one was requested and could be built. |

With a password, a compressed output is decrypted. When Hyper keeps the original (nothing was smaller, or the file is signed), you get your original bytes, still encrypted. A file with only an owner password compresses without `password`, and the output keeps that owner password. `signed` is also `true` when `removeSignatures` was set but the compressed file was not smaller, so the signed original came back.

#### JSON

`CompressResult` implements `serde::Serialize` with camelCase names, the same shape the Node.js SDK returns. `report` is left out when it is `None`. `output_path` is written as a string, so a path that is not valid UTF-8 cannot be serialized.

```json
{
  "outputPath": "output.pdf",
  "originalSize": 2593056,
  "compressedSize": 1252473,
  "signed": false,
  "signaturesRemoved": false,
  "pdfa": { "part": 2, "conformance": "b" },
  "pdfaPreserved": false,
  "metTarget": null,
  "warnings": []
}
```

### Errors

`compress` returns `Err(HyperError)`:

| `ErrorCode` | When |
|---|---|
| `DecryptFailed` | The password is wrong (`decrypt_failed`), or the PDF needs a password and none was given (`the PDF is encrypted and needs a password`). |
| `EngineError` | No `hpdf-worker` found, an explicit or environment path that does not exist (`hpdf-worker not found at <path>`, `qpdf not found at <path>`), `source_path` is empty, the input cannot be read (`cannot read <path>: <reason>`) or is not a PDF (`the input is not a PDF: no %PDF- header in its first 1024 bytes`), a password was given but no `qpdf` found, or the output or report cannot be written. |
| `Timeout` | One step ran past `stage_timeout` (`the engine pass ran longer than the <n> ms stage limit`, or `decryption ...`, `qpdf ...`), or the whole call ran past `total_timeout` (`timeout`). |
| `Cancelled` | `cancel` was cancelled. |

These are not errors: a file that cannot be made smaller (original returned), a signed input, a missed size target, and an option skipped for safety (a warning). See [Errors](/errors).

## Binaries

```rust
#[derive(Debug, Clone, Default)]
pub struct Binaries {
    pub worker: Option<PathBuf>,
    pub qpdf: Option<PathBuf>,
    pub search_dirs: Vec<PathBuf>,
}
```

| Field | Type | Default | Description |
|---|---|---|---|
| `worker` | `Option<PathBuf>` | `None` | Path to `hpdf-worker`. |
| `qpdf` | `Option<PathBuf>` | `None` | Path to `qpdf`. |
| `search_dirs` | `Vec<PathBuf>` | empty | Folders to look in. |

For each program, the crate uses the first match:

1. the explicit path (`worker` or `qpdf`), as given;
2. the environment variable (`HYPER_DRV` for `hpdf-worker`, `HYPER_QPDF` for `qpdf`), if set and not empty, as given;
3. the first folder in `search_dirs` that holds a file named `hpdf-worker` or `qpdf` (with `.exe` on Windows).

An explicit path or environment variable that points to a file that does not exist returns `EngineError` (`hpdf-worker not found at <path>` or `qpdf not found at <path>`). When no `qpdf` is found at all, compression still runs and skips the steps that need it.

```rust
pub fn beside_exe() -> Binaries
```

Returns a `Binaries` that searches the folder of the running program, then `cli/prebuilt/` in that folder and in each of its parent folders, up to 8 levels. This is what the CLI and the HTTP service use. `Binaries::default()` searches no folders, so only explicit paths and environment variables work.

```rust
use std::path::PathBuf;

use hyper_compress::{compress, Binaries, CompressInput};

fn main() {
    let input = CompressInput {
        source_path: PathBuf::from("input.pdf"),
        save_path: Some(PathBuf::from("output.pdf")),
        binaries: Binaries {
            worker: Some(PathBuf::from("/opt/hyper/hpdf-worker")),
            qpdf: Some(PathBuf::from("/opt/hyper/qpdf")),
            ..Default::default()
        },
        ..Default::default()
    };
    let result = compress(&input).expect("compress failed");
    println!("{} bytes", result.compressed_size);
}
```

When `hpdf-worker` is not found, the error message names your platform the way npm does, for example `no hpdf-worker binary for linux-x64: ...` or `darwin-arm64`.

## ReportRequest

```rust
#[derive(Debug, Clone, Default)]
pub enum ReportRequest {
    #[default]
    None,
    Return,
    File(PathBuf),
}
```

| Variant | Effect |
|---|---|
| `None` | No report. |
| `Return` | Put the report in `CompressResult::report`. |
| `File(path)` | Put it in `CompressResult::report` and also write it to `path` as indented JSON. |

A report adds two read passes with `qpdf`. If the PDF structure cannot be read (or `qpdf` is missing), `report` is `None` and the warning `report unavailable: the PDF structure could not be read` is added. The [compression report](/report) page describes every field.

## Preset

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Preset { Low, Medium, High, Lossless }
```

| Item | Description |
|---|---|
| `Preset::ALL` | `[Low, Medium, High, Lossless]` |
| `fn name(self) -> &'static str` | `"low"`, `"medium"`, `"high"` or `"lossless"`. |
| `fn parse(s: &str) -> Option<Preset>` | The preset with that exact name, or `None`. |
| `fn options(self) -> Options` | The preset as complete options. |

## Options

```rust
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Options { /* one public field per option */ }
```

Every [option](/options) as a public snake_case field: `image_quality: i64`, `color_target: String`, `grayscale: bool` and so on. Numbers are `i64`, switches `bool`, choices `String`. It serializes with the camelCase names used everywhere else.

| Method | Description |
|---|---|
| `fn normalize(raw: &Map<String, Value>) -> Options` | Builds complete options from a camelCase map. Numbers are rounded and clamped to their range; a wrong type or unknown choice becomes the option's fallback default; missing options get their fallback default; unknown keys are ignored; `removeAnnots: true` with `annotations: "keep"` becomes `"flatten"`. |
| `fn with(&self, overrides: &Map<String, Value>) -> Options` | These options with `overrides` applied on top, then normalized. |
| `fn to_map(&self) -> Map<String, Value>` | The options as a camelCase map. |
| `fn tokens(&self) -> Vec<String>` | The engine's `id=value` tokens for these options, the same as `buildHyperTokens` in the [Node.js SDK](/node-sdk#buildhypertokens-options). |

The fallback defaults are the `Default` column of the [options reference](/options). They are not the `medium` preset: `Options::normalize(&Map::new())` has `image_quality` 80. Start from a preset with `Preset::options()` or [`resolve_options`](#resolve-options).

Options has no `Default` implementation.

## resolve_options

```rust
pub fn resolve_options(preset: Option<Preset>, overrides: &Map<String, Value>) -> Options
```

The options a `compress` call starts from: the preset (`Medium` when `None`) with `overrides` on top. For a PDF/A input with `preserveConformance`, `compress` then switches off the options that would break the standard; this function does not see the file, so it does not show that step.

```rust
use hyper_compress::{resolve_options, Preset};
use serde_json::{json, Map};

fn main() {
    let mut overrides = Map::new();
    overrides.insert("imageQuality".into(), json!(500));
    let o = resolve_options(Some(Preset::High), &overrides);
    println!("{} {}", o.image_quality, o.max_dpi);
    println!("{:?}", o.tokens());
}
```

```text
100 72
["1=72", "59=150", "2=100", "3=4", "8=1", "18=1", "7=1", "63=10", "6=1", "10=1", "12=1", "9=1", "13=1", "14=1", "16=1", "17=1", "20=565442", "67=6"]
```

## verify_password

```rust
pub fn verify_password(source: &Path, password: &str, timeout: Option<Duration>, bins: &Binaries) -> Result<bool>
```

Checks a password without compressing, with the same decryption step `compress` uses. `timeout` defaults to 60 seconds.

Returns `Ok(true)` when the password opens the file, and for a PDF that is not encrypted. Returns `Ok(false)` for a wrong password and for a file that exists but is not a PDF. Returns `Err` with `EngineError` when the file does not exist (`cannot read <path>`) or `qpdf` cannot be found, and `Timeout` when the check runs past `timeout`.

```rust
use std::path::Path;

use hyper_compress::{verify_password, Binaries};

fn main() -> Result<(), hyper_compress::HyperError> {
    let ok = verify_password(Path::new("input.pdf"), "hunter2", None, &Binaries::beside_exe())?;
    println!("password accepted: {ok}");
    Ok(())
}
```

## CancelToken

```rust
#[derive(Clone, Default)]
pub struct CancelToken(/* private */);
```

| Method | Description |
|---|---|
| `fn new() -> CancelToken` | A token that is not cancelled. |
| `fn cancel(&self)` | Cancels every call that holds this token or a clone of it. |
| `fn is_cancelled(&self) -> bool` | Whether `cancel` was called. |

Clones share one flag, so you can keep a clone and cancel from another thread. A cancelled token stays cancelled: use a new one for each call.

### Cancellation

```rust
use std::path::PathBuf;
use std::thread;
use std::time::Duration;

use hyper_compress::{compress, Binaries, CancelToken, CompressInput, ErrorCode};

fn main() {
    let cancel = CancelToken::new();
    let input = CompressInput {
        source_path: PathBuf::from("input.pdf"),
        save_path: Some(PathBuf::from("output.pdf")),
        cancel: cancel.clone(),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };

    let worker = thread::spawn(move || compress(&input));
    thread::sleep(Duration::from_secs(5));
    cancel.cancel();

    match worker.join().unwrap() {
        Ok(r) => println!("finished first: {} bytes", r.compressed_size),
        Err(e) if e.code == ErrorCode::Cancelled => println!("cancelled"),
        Err(e) => eprintln!("error: {e}"),
    }
}
```

The running process is stopped within a few milliseconds of `cancel`, and `compress` returns `Cancelled`. No output file is written.

### Timeouts

| Field | Limits | When exceeded |
|---|---|---|
| `stage_timeout` (default 10 minutes) | Each separate process: decryption, the engine pass, each repack, each size target attempt, each report read. | The process is stopped and `compress` returns `Timeout`, with a message such as `the engine pass ran longer than the 600000 ms stage limit`. |
| `total_timeout` (default none) | The whole call. | `compress` returns `Timeout`. |

Set `total_timeout` when you need a hard bound. A size target can run the engine up to 11 times. The constant `hyper_compress::run::DEFAULT_STAGE_TIMEOUT` holds the 10 minute default.

## HyperError and ErrorCode

```rust
#[derive(Debug, Clone)]
pub struct HyperError {
    pub code: ErrorCode,
    pub message: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorCode { DecryptFailed, EngineError, Timeout, Cancelled }

pub type Result<T> = std::result::Result<T, HyperError>;
```

| Item | Description |
|---|---|
| `HyperError::new(code, message)` | An error with a message. |
| `HyperError::code(code)` | An error whose message is the code's name. |
| `Display` | Writes `message`. |
| `std::error::Error` | Implemented, so `?` works into `Box<dyn Error>` and `anyhow`. |
| `From<std::io::Error>` | Becomes `EngineError` with the I/O error's text. |
| `ErrorCode::as_str(self)` | `"decrypt_failed"`, `"engine_error"`, `"timeout"` or `"cancelled"`, the same codes as the other surfaces. |

```rust
use std::path::PathBuf;
use std::time::Duration;

use hyper_compress::{compress, Binaries, CompressInput, ErrorCode};

fn main() {
    let input = CompressInput {
        source_path: PathBuf::from("input.pdf"),
        save_path: Some(PathBuf::from("output.pdf")),
        password: Some("hunter2".into()),
        total_timeout: Some(Duration::from_secs(120)),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };

    match compress(&input) {
        Ok(r) => println!("{} bytes", r.compressed_size),
        Err(e) => match e.code {
            ErrorCode::DecryptFailed => eprintln!("wrong password"),
            ErrorCode::Timeout | ErrorCode::Cancelled => eprintln!("stopped: {}", e.code.as_str()),
            ErrorCode::EngineError => eprintln!("failed: {e}"),
        },
    }
}
```

## PdfaLevel

```rust
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct PdfaLevel {
    pub part: u32,
    pub conformance: String,
}
```

The PDF/A level an input claims. `part` is the PDF/A part (1 to 4). `conformance` is the level letter in lower case (`"a"`, `"b"`, `"u"` and so on), or empty when there is none, as in PDF/A-4. `label()` returns the display name, for example `PDF/A-2b`.

## Report

`Report` and its parts are in `hyper_compress::report`. All derive `Debug`, `Clone` and `Serialize`, and serialize to the JSON on [Compression report](/report).

| Type | Fields |
|---|---|
| `Report` | `original_size: u64`, `compressed_size: u64`, `fonts: Vec<FontReport>`, `images: Vec<ImageReport>`, `cleanup: Vec<CleanupReport>` |
| `FontReport` | `name: String`, `kind: String` (serialized as `type`), `action: &'static str`, `before: FontState`, `after: Option<FontState>` |
| `FontState` | `count: u64`, `embedded: bool`, `subset: bool`, `bytes: u64` |
| `ImageReport` | `page: u64`, `name: String`, `dpi: Option<u64>`, `before: ImageState`, `after: Option<ImageState>` |
| `ImageState` | `width: u64`, `height: u64`, `color_space: String` (serialized as `colorSpace`), `codec: String`, `bytes: u64` |
| `CleanupReport` | `item: String`, `before: u64`, `after: u64` |

## What the crate exports

The crate root re-exports the items on this page:

```rust
pub use engine::{compress, resolve_options, verify_password, Binaries, CompressInput, CompressResult, ReportRequest};
pub use error::{ErrorCode, HyperError, Result};
pub use options::Options;
pub use pdfa::PdfaLevel;
pub use presets::Preset;
pub use run::CancelToken;
```

The modules `engine`, `error`, `options`, `pdfa`, `presets`, `report`, `run` and `target` are public too. Items in them that this page does not list are internal helpers and may change without notice.

## Troubleshooting

**`no hpdf-worker binary for <platform>`.** Set `Binaries::worker`, set `HYPER_DRV`, or put `hpdf-worker` beside your program and use `Binaries::beside_exe()`.

**`hpdf-worker not found at <path>`.** The explicit path or `HYPER_DRV` points to a file that does not exist. Fix it, or leave both unset and use `search_dirs`.

**`report unavailable: the PDF structure could not be read`.** `qpdf` was not found. Set `Binaries::qpdf` or `HYPER_QPDF`.

**`compress` blocks your async runtime.** It is synchronous. Run it with `tokio::task::spawn_blocking` or on your own thread.

## Next steps

- [Options](/options): every option name, range and default.
- [Compression report](/report): the fields of `Report`.
- [Warnings](/warnings): every warning string.
- [Errors](/errors): every error, across all surfaces.
- [Building from source](/building): build `hpdf-worker` and `qpdf` yourself.
