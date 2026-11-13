---
title: Errors, timeouts and cancellation
description: Handle the four Hyper Compress error codes on every surface, tell errors from normal outcomes, bound how long a job runs, and cancel work in progress.
---

# Errors, timeouts and cancellation

Hyper Compress has four error codes, and every surface reports them in its own way. This guide shows how to catch each one, which results look unusual but are not errors, how to put a time limit on a job, and how to stop one that is running.

## Before you begin

- Install Hyper and compress one file first. See the [Quickstart](/getting-started).
- Time limits and cancellation are fullest in the Node.js SDK and the Rust crate. The HTTP API has a time limit; the CLI has a fixed one; the WebAssembly SDK and the C API have neither.

## The four error codes

| Code | Meaning | Retry? |
|---|---|---|
| `decrypt_failed` | The input is encrypted and the password is wrong, or it needs a password and you sent none. | No. Ask for the right password. Retrying with the same one fails the same way. |
| `engine_error` | Hyper could not run: the input file is missing or unreadable, the input is not a PDF, the output cannot be written, an engine binary is missing, or `sourcePath` is empty. The message says which. | No, not until you fix the cause in the message. |
| `timeout` | One stage ran longer than its stage limit, or the whole call ran longer than its total limit. | Yes, with a longer limit or lighter settings. |
| `cancelled` | You cancelled the call. | Only if you still want the result. |

A broken or unusual PDF is usually not an error. If the engine cannot process a file that starts like a PDF, Hyper falls back to repacking it or to your original bytes, and the call succeeds. See [Outcomes that are not errors](#outcomes-that-are-not-errors).

## 1. Catch errors on your surface

::: code-group

```bash [CLI]
hyper locked.pdf output.pdf --password hunter2
echo "exit code: $?"
```

```js [Node.js]
import { compress, HyperError } from 'hyper-compress';

try {
  await compress({ sourcePath: 'locked.pdf', savePath: 'output.pdf', password: 'hunter2' });
} catch (e) {
  if (!(e instanceof HyperError)) throw e;
  switch (e.code) {
    case 'decrypt_failed': console.error(e.message); break;
    case 'timeout':        console.error('took too long'); break;
    case 'cancelled':      console.error('cancelled'); break;
    default:               console.error('cannot run:', e.message);
  }
}
```

```bash [HTTP]
curl -s -o output.pdf -w '%{http_code}\n' --data-binary @locked.pdf \
  -H 'X-Password: hunter2' \
  'http://localhost:8080/api/compress'
cat output.pdf
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress, HyperError } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('locked.pdf'));
try {
  const result = await compress(input, { password: 'hunter2' });
  await writeFile('output.pdf', result.data);
} catch (e) {
  if (!(e instanceof HyperError)) throw e;
  console.error(e.code, e.message);
}
```

```rust [Rust]
use hyper_compress::{compress, Binaries, CompressInput, ErrorCode};

fn main() {
    let input = CompressInput {
        source_path: "locked.pdf".into(),
        save_path: Some("output.pdf".into()),
        password: Some("hunter2".into()),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };
    match compress(&input) {
        Ok(r) => println!("{} -> {} bytes", r.original_size, r.compressed_size),
        Err(e) if e.code == ErrorCode::DecryptFailed => eprintln!("{}", e.message),
        Err(e) => eprintln!("{}: {}", e.code.as_str(), e.message),
    }
}
```

```c [C]
int rc = hpdf_compress_file("locked.pdf", "output.pdf", NULL, NULL, 0, "hunter2");
if (rc == 1) fprintf(stderr, "failed: %s\n", hpdf_last_error());
```

:::

What each surface reports when `hunter2` is not the file's password. Leaving the password out of a file that needs one fails the same way, with the message `the PDF is encrypted and needs a password`. A file that has only an owner password (one that restricts editing or printing) still compresses without a password, and the output keeps that protection.

| Surface | What you get |
|---|---|
| CLI | `error: decrypt_failed` on stderr, exit code 1 |
| Node.js | `HyperError` with `code` `'decrypt_failed'` |
| HTTP | status 400, body `{"error":"decrypt_failed"}` |
| WebAssembly | `HyperError` with `code` `'decrypt_failed'` |
| Rust | `Err(HyperError)` with `code` `ErrorCode::DecryptFailed` |
| C | return code 1, and `hpdf_last_error()` describes it, for example `cannot open input (missing, corrupt, or wrong password)` |

### How each surface exposes the codes

**CLI.** Exit code 0 means a file was written, 1 means an error, 2 means bad usage (an unknown flag or a missing input path). On an error the CLI prints one line to stderr that starts with `error:`, for example `error: decrypt_failed`, `error: the PDF is encrypted and needs a password` or `error: the input is not a PDF: no %PDF- header in its first 1024 bytes`. Usage errors print the usage text after the `error:` line.

**Node.js.** `compress` rejects with a `HyperError`. `e.code` is one of the four codes and `e.message` adds detail, such as the platform name when no engine binary exists, or which stage ran out of time. `verifyPassword` uses the same codes: it returns `false` for a wrong password, and throws `engine_error` when the file is missing and `timeout` when the check runs past its limit (60 seconds unless you pass one).

**HTTP.** Errors come back as JSON with an `error` field and a status code:

| Status | `error` | When |
|---|---|---|
| 400 | `decrypt_failed` | the password does not open the file, or the file needs one and none was sent |
| 500 | `engine_error` | Hyper could not run |
| 500 | `cancelled` | the job was stopped |
| 504 | `timeout` | the job ran past `HYPER_TIMEOUT_MS` |

Send the password in `X-Password` when it is plain ASCII, or percent-encoded (as JavaScript `encodeURIComponent` does) in `X-Password-Encoded` for any other characters. The API also has request errors that never reach the engine, such as `400 not_a_pdf`, `413 too_large` and `429 busy`. The [HTTP API reference](/http-api) lists them all.

**WebAssembly.** `compress` rejects with a `HyperError` whose `code` is only ever `decrypt_failed` or `engine_error`. `engine_error` also covers input that is not a PDF and a WebAssembly module that failed to load; the next call tries to load it again. The WebAssembly SDK has no time limit and no cancellation, so `timeout` and `cancelled` never occur.

**Rust.** `compress` returns `Result<CompressResult, HyperError>`. `HyperError` has `code` (an `ErrorCode`: `DecryptFailed`, `EngineError`, `Timeout` or `Cancelled`, with `as_str()` for the string form) and `message`.

**C.** `hpdf_compress_file` returns `0` (compressed), `2` (signed input, copied unchanged) or `1` (failed). `hpdf_compress_buffer` returns `NULL` on failure and sets `*out_status` to `1`. After a failure, `hpdf_last_error()` returns one of: `cannot open input (missing, corrupt, or wrong password)`, `cannot parse input (corrupt or wrong password)`, `engine pass failed`, `cannot open output for writing`, `save failed`, `input larger than 2 GB is not supported`. The C API is the engine pass alone, so it has no `timeout` or `cancelled`, and it does not fall back to your original file.

## 2. Recognise outcomes that are not errors {#outcomes-that-are-not-errors}

These results succeed (exit code 0, HTTP 200, no exception). Check for them in your code if they matter to you.

| Outcome | How to spot it |
|---|---|
| No gain: Hyper could not make the file smaller and returned your original bytes | `compressedSize` equals `originalSize`; the CLI prints `0% smaller` |
| Signed input returned untouched | `signed: true`; CLI `[signed: original preserved]`; HTTP `X-Signed: true`; C return code 2 |
| Size target missed; you got the smallest file Hyper could make | `metTarget: false`; CLI `[target NOT met - best effort]`; HTTP `X-Met-Target: false` |
| An option was skipped because it was unsafe for this file | an entry in `warnings`; CLI `warning:` lines on stderr; HTTP `X-Warnings` header |
| A broken PDF the engine could not process | a valid result, usually your original bytes |

```js
import { compress } from 'hyper-compress';

const r = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', targetSizeBytes: 2 * 1024 * 1024 });
if (r.signed) console.log('signed: returned untouched');
if (r.compressedSize === r.originalSize) console.log('no gain: original kept');
if (r.metTarget === false) console.log(`target missed, best was ${r.compressedSize} bytes`);
for (const w of r.warnings) console.warn('skipped:', w);
```

Every warning text is listed in [Warnings](/warnings).

## 3. Put a time limit on a job

A compression runs as a series of stages, each in its own process: decryption (only with a password), the engine pass, and a repack. A size target runs the engine pass and repack again for each attempt, and a report adds two read passes.

Hyper has two kinds of limit. Both stop the work and fail the call with `timeout`:

| Limit | Default | Message when it runs out |
|---|---|---|
| Per stage (`timeoutMs`) | 600000 (10 minutes) | names the stage, for example `the engine pass ran longer than the 600000 ms stage limit` (other stages are `decryption` and `qpdf`) |
| Whole call (`totalTimeoutMs`) | none | `timeout` |

The per-stage limit catches one stuck step. The total limit bounds the whole call, which matters with a size target, where many stages run one after another.

::: code-group

```js [Node.js]
import { compress, HyperError } from 'hyper-compress';

try {
  await compress({
    sourcePath: 'input.pdf',
    savePath: 'output.pdf',
    totalTimeoutMs: 2 * 60 * 1000,
  });
} catch (e) {
  if (e instanceof HyperError && e.code === 'timeout') console.error('gave up after 2 minutes');
  else throw e;
}
```

```bash [HTTP]
docker run -d --name hyper -p 8080:8080 \
  -e HYPER_TIMEOUT_MS=120000 \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

```rust [Rust]
use std::time::Duration;
use hyper_compress::{compress, Binaries, CompressInput, ErrorCode};

fn main() {
    let input = CompressInput {
        source_path: "input.pdf".into(),
        save_path: Some("output.pdf".into()),
        total_timeout: Some(Duration::from_secs(120)),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };
    match compress(&input) {
        Ok(r) => println!("{} -> {} bytes", r.original_size, r.compressed_size),
        Err(e) if e.code == ErrorCode::Timeout => eprintln!("gave up after 2 minutes"),
        Err(e) => eprintln!("{}", e.message),
    }
}
```

:::

With a total limit of 2 minutes, a job that runs longer fails with:

```text
gave up after 2 minutes
```

How each surface sets the limits:

| Surface | Per stage | Whole call |
|---|---|---|
| Node.js | `timeoutMs` | `totalTimeoutMs` |
| Rust | `stage_timeout` | `total_timeout` |
| HTTP | `HYPER_TIMEOUT_MS` (default 600000) | `HYPER_TIMEOUT_MS`, the same value |
| CLI | fixed at 10 minutes; prints `error: ... stage limit` and exits 1 | none |
| WebAssembly | none | none |
| C | none | none |

Over HTTP the one setting is used for both, so a job that runs past `HYPER_TIMEOUT_MS` gets status 504 with `{"error":"timeout"}`. Allowed values are 1 second to 24 hours. See [Self-hosting](/self-hosting).

## 4. Cancel a running job

In Node.js, pass an `AbortSignal`. Aborting before the call starts rejects at once without running anything. Aborting while it runs stops the engine process that is working and rejects with `cancelled`.

::: code-group

```js [Node.js]
import { compress, HyperError } from 'hyper-compress';

const controller = new AbortController();
setTimeout(() => controller.abort(), 5000);

try {
  await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', signal: controller.signal });
} catch (e) {
  if (e instanceof HyperError && e.code === 'cancelled') console.log('cancelled after 5 seconds');
  else throw e;
}
```

```rust [Rust]
use std::thread;
use std::time::Duration;
use hyper_compress::{compress, Binaries, CancelToken, CompressInput, ErrorCode};

fn main() {
    let cancel = CancelToken::new();
    let input = CompressInput {
        source_path: "input.pdf".into(),
        save_path: Some("output.pdf".into()),
        cancel: cancel.clone(),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };
    let job = thread::spawn(move || compress(&input));
    thread::sleep(Duration::from_secs(5));
    cancel.cancel();
    match job.join().expect("compress thread panicked") {
        Err(e) if e.code == ErrorCode::Cancelled => println!("cancelled after 5 seconds"),
        Err(e) => eprintln!("{}", e.message),
        Ok(r) => println!("finished first: {} bytes", r.compressed_size),
    }
}
```

:::

```text
cancelled after 5 seconds
```

Hyper checks for cancellation every few milliseconds while a stage runs, so the call stops quickly even in the middle of a large file. No output file is written for a cancelled call.

Cancellation on the other surfaces:

| Surface | Cancellation |
|---|---|
| CLI | Ctrl+C stops the command. |
| HTTP | None. A client that disconnects does not stop its job; the job runs to the end or to `HYPER_TIMEOUT_MS`. |
| WebAssembly | None. To stop a run, run it in a worker thread and terminate the thread. |
| C | None. |

## How it works

- **The engine runs in its own process.** On the native surfaces (CLI, Node.js, HTTP, Rust), each stage is a separate process. If a malformed PDF crashes the engine, only that process ends; your application keeps running and Hyper falls back to the best file it has. If it hangs, the stage limit stops it and the call fails with `timeout`.
- **Corrupt input degrades, it does not throw.** A file that starts like a PDF but cannot be read comes back as your original bytes. Hyper's test suite checks this.
- **A failed call leaves no output.** The output is written to a temporary file beside the destination and renamed into place at the end, so a failed or cancelled call never leaves a half-written file. This also makes it safe to use the input path as the output path.

## Troubleshooting

**`decrypt_failed` with the right password.** Check it first with `verifyPassword` (Node.js and WebAssembly) or `POST /api/verify-password` (HTTP). The PDF may have a different user password from the one you have. Only the first line of a password is used. Over HTTP, send a password with non-ASCII characters in `X-Password-Encoded`.

**`engine_error: no hpdf-worker binary for <platform>`.** There is no native build for this platform. Use the [WebAssembly SDK](/wasm), or build from source and set `HYPER_DRV`. See [Platforms](/platforms).

**`engine_error: cannot read input.pdf: No such file or directory (os error 2)`.** The input path is wrong. If the input is fine, check that the folder of the output path exists.

**`engine_error: hpdf-worker not found at <path>`** (or `qpdf not found at <path>`). `HYPER_DRV` or `HYPER_QPDF` points at a file that does not exist. Fix the path or unset the variable.

**`engine_error: the input is not a PDF`.** The file has no `%PDF-` header in its first 1024 bytes. Over HTTP the same file gets `400 not_a_pdf`.

**`timeout` with `ran longer than the ... ms stage limit`.** One stage ran past `timeoutMs`. Raise `timeoutMs`, or use lighter settings (for example `speed: 'fast'`).

**HTTP returns 504.** The job ran past `HYPER_TIMEOUT_MS`. Raise it, or send lighter settings.

## Next steps

- [Errors](/errors): every error code and message in one place.
- [Warnings](/warnings): every warning Hyper can return.
- [Running in production](/guides/production): concurrency, queues and untrusted uploads.
- [Encrypted PDFs](/guides/encrypted-pdfs): passwords and how to check them.
