---
title: Errors
description: Every error code, message, exit code and HTTP status Hyper Compress can return, on every surface, with what to do.
---

# Errors

This page lists every error Hyper Compress can return, on every surface, and what to do about each. An error means you got no output file. When Hyper skips an option instead, that is a [warning](/warnings), not an error.

For patterns that handle errors, timeouts and cancellation in an app or service, see [Errors, timeouts and cancellation](/guides/errors-and-cancellation).

## Error codes

The CLI, the Node.js SDK, the Rust crate, the HTTP API and WebAssembly share four codes:

| Code | Meaning | What to do |
|---|---|---|
| `decrypt_failed` | The password is wrong, or the PDF needs a password and none was given. | Ask for the password. Check it first with `verifyPassword`. See [Encrypted PDFs](/guides/encrypted-pdfs). |
| `engine_error` | Hyper could not run: no engine for this platform, an engine path that does not exist, an empty input path, input that is not a PDF, a file that cannot be read or written, a missing `qpdf` when a password is given, or a failure inside the engine. | Read the message. It names the platform, the path or the file problem. See [Troubleshooting](#troubleshooting). |
| `timeout` | One step ran past the per-step limit (`timeoutMs`, `stage_timeout`, or the HTTP service's `HYPER_TIMEOUT_MS`; 10 minutes by default), or the whole call ran past its total limit (`totalTimeoutMs`, `total_timeout`, `HYPER_TIMEOUT_MS`). | Raise the limit, or use a lighter preset. Large scans with a size target take longest. |
| `cancelled` | You cancelled the call (`AbortSignal` in Node.js, `CancelToken` in Rust). | Nothing; this is the result you asked for. |

These are not errors, and return a result instead:

| Situation | What you get |
|---|---|
| Hyper could not make the file smaller | Your original bytes, output size equal to input size. |
| The input is digitally signed | Your original bytes, `signed: true`. |
| A size target was missed | The smallest file Hyper made, `metTarget: false`, a warning. |
| An option was unsafe for this file | A [warning](/warnings). |
| A PDF with only an owner password (it opens without one) and no password given | A compressed file that keeps the owner password. |

## Node.js SDK

Every error is a `HyperError` with `code` set to one of the [error codes](#error-codes). See [Node.js SDK](/node-sdk#hypererror).

| Code | Message | Cause |
|---|---|---|
| `engine_error` | `sourcePath is required` | `sourcePath` is empty or missing. |
| `engine_error` | `no hpdf-worker binary for <platform>: use a platform with a native package (darwin-arm64, linux-x64, linux-arm64, win32-x64), set HYPER_DRV to a binary you built (see BUILDING.md), or switch to the hyper-compress-wasm package` | No engine found. `<platform>` is for example `linux-x64` or `darwin-arm64`. |
| `engine_error` | `no hyper-compress.node binary for <platform>: ...` (same text, naming `HYPER_ADDON`) | No native addon found. |
| `engine_error` | `no qpdf binary for <platform>: ...` (same text, naming `HYPER_QPDF`) | A password was given and `qpdf` was not found. Also from `verifyPassword`. |
| `engine_error` | `hpdf-worker not found at <path>` | `HYPER_DRV` points to a file that does not exist. |
| `engine_error` | `qpdf not found at <path>` | `HYPER_QPDF` points to a file that does not exist. |
| `engine_error` | `cannot read <path>: <reason>` | The input cannot be read, for example `No such file or directory (os error 2)`. From `verifyPassword`: `cannot read <path>`. |
| `engine_error` | `the input is not a PDF: no %PDF- header in its first 1024 bytes` | The input is not a PDF. |
| `engine_error` | An operating system message, for example `Permission denied (os error 13)` | The output, report or a temporary file cannot be written. |
| `decrypt_failed` | `decrypt_failed` | Wrong password. |
| `decrypt_failed` | `the PDF is encrypted and needs a password` | The PDF needs a password and `password` was not given. |
| `timeout` | `the engine pass ran longer than the <n> ms stage limit` (also `decryption ...` and `qpdf ...`) | One step ran past `timeoutMs`. |
| `timeout` | `timeout` | The call ran past `totalTimeoutMs`. Or the `AbortSignal` reason was a `HyperError` with this code. |
| `cancelled` | `cancelled` | The `AbortSignal` fired. If the abort reason is a `HyperError`, you get that error instead. |

An addon that exists but cannot be loaded (for example, built for another platform) throws Node.js's own loader error, not a `HyperError`.

`verifyPassword` returns `false` rather than an error for a wrong password and for a file that exists but is not a PDF. It throws `engine_error` for a missing file and `timeout` for a check that runs too long.

## CLI

| Exit code | Meaning | stderr |
|---|---|---|
| `0` | Success, including original kept, signed input and missed target. | Warnings, if any |
| `1` | The compression failed. | `error: <message>`, where the message is one of the [Node.js SDK messages](#node-js-sdk) (from the Rust core). |
| `2` | Bad usage. | `error: <message>`, the usage text, and `run "hyper --help" for the full option list` |

Usage messages (exit `2`):

| Message | What to do |
|---|---|
| `an input PDF is required` | Pass an input path. |
| `--preset expects one of low, medium, high, lossless` | Use one of those names. |
| `--mrc expects off, auto or force` | Use one of those modes. |
| `--target-size expects a size like 2MB, 500KB or a byte count` | Use a number with `B`, `K`, `KB`, `M` or `MB`. |
| `--target-size must be greater than zero` | Use a size of at least 1 byte. |
| `--report expects a file path` | Put a path after `--report`. |
| `--set expects key=value` | Write `--set name=value`. |
| `unknown flag <flag>` | Check the spelling; `hyper --help` lists every flag. |

The CLI has no total time limit and no cancellation, but each step is limited to 10 minutes, so it can fail with `error: the engine pass ran longer than the 600000 ms stage limit`. See [CLI](/cli#exit-codes).

## Rust crate

`compress` and `verify_password` return `Err(HyperError)` with `code: ErrorCode` and `message: String`:

| `ErrorCode` | `as_str()` | Messages |
|---|---|---|
| `DecryptFailed` | `decrypt_failed` | `decrypt_failed`, `the PDF is encrypted and needs a password` |
| `EngineError` | `engine_error` | `sourcePath is required`, `no hpdf-worker binary for <platform>: ...`, `no qpdf binary for <platform>: ...`, `hpdf-worker not found at <path>`, `qpdf not found at <path>`, `cannot read <path>: <reason>`, `the input is not a PDF: no %PDF- header in its first 1024 bytes`, or an operating system message |
| `Timeout` | `timeout` | `the engine pass ran longer than the <n> ms stage limit` (also `decryption ...`, `qpdf ...`), or `timeout` for `total_timeout` |
| `Cancelled` | `cancelled` | `cancelled` |

`<platform>` is spelled the way npm spells it: `darwin-arm64`, `linux-x64`, `linux-arm64` or `win32-x64`. See [Rust crate](/rust#hypererror-and-errorcode).

## WebAssembly

| Code | Message | Cause |
|---|---|---|
| `decrypt_failed` | `wrong or missing password` | The password is wrong. |
| `decrypt_failed` | `the PDF is encrypted and needs a password` | The PDF needs a password and none was given. |
| `engine_error` | `the input is not a PDF: no %PDF- header in its first 1024 bytes` | The input is not a PDF. |
| `engine_error` | `wasm module failed to load: <reason>` | The engine files could not be loaded. The next call tries again. |
| `engine_error` | The underlying error's message | Something failed inside the engine, for example running out of memory. |

The WebAssembly package never produces `timeout` or `cancelled`. See [WebAssembly](/wasm#errors).

## HTTP API

Errors are JSON bodies with an `error` code, sometimes with extra fields. See [HTTP API](/http-api).

```json
{ "error": "too_large", "maxBytes": 524288000 }
```

| Status | `error` | Extra fields | Cause | What to do |
|---|---|---|---|---|
| 400 | `bad_preset` | `presets`: `["low", "medium", "high", "lossless", "custom"]` | `preset` is not one of those names. | Use a listed preset. |
| 400 | `options_required` | | `preset=custom` without an `options` parameter. | Send `options` with `custom`. |
| 400 | `bad_options` | | `options` is not a JSON object, or is longer than 4096 characters. | Send a URL-encoded JSON object. |
| 400 | `bad_target_size` | | `targetSizeBytes` is not a number greater than 0. | Send a positive byte count. |
| 400 | `not_a_pdf` | | The body is shorter than 5 bytes or does not start with `%PDF`. | Send the PDF bytes as the raw body, not as a form upload. |
| 400 | `decrypt_failed` | | The password (`X-Password` or `X-Password-Encoded`) is wrong, or the PDF needs a password and none was sent. | Send the right password. Check it with `/api/verify-password`. Send non-ASCII passwords in `X-Password-Encoded`. |
| 401 | `unauthorized` | | `HYPER_API_TOKEN` is set and the request has no matching `Authorization: Bearer` or `X-Api-Token` header. | Send the token. |
| 404 | `not_found` | | Unknown path or method, or a missing static file. | Check the URL and method. |
| 413 | `too_large` | `maxBytes`: the limit in bytes | The body is larger than `HYPER_MAX_UPLOAD_MB`. | Send a smaller file, or raise the limit. |
| 429 | `busy` | `detail`: `"queue full, retry later"`; header `Retry-After: 1` | Every slot is busy and the queue (`HYPER_QUEUE`) is full. | Retry after the `Retry-After` delay, with backoff. |
| 500 | `engine_error` | | The engine failed, or no engine binary was found. | Check the service logs and the file. |
| 500 | `cancelled` | | Mapped for completeness. The service does not cancel requests, so this does not occur. | |
| 500 | `internal` | | A temporary file could not be written or read, or `/api/verify-password` failed for any reason. | Check disk space and the temp folder; retry. |
| 504 | `timeout` | | One step, or the whole request, ran past `HYPER_TIMEOUT_MS` (default 10 minutes). | Raise `HYPER_TIMEOUT_MS`, or send lighter settings. |

`/api/verify-password` answers `200 {"valid": false}` for a wrong password, not an error. It can return `401`, `413`, `429` and `500 internal`.

Checks run in this order: authentication, `preset`, `targetSizeBytes`, `options`, the queue (`busy`), the body size, then `not_a_pdf`. So a request with a bad preset gets `bad_preset` even when the queue is full. See [Self-hosting](/self-hosting) for the environment variables.

## C API

`hpdf_compress_file` returns, and `hpdf_compress_buffer` sets `*out_status` to:

| Code | Meaning |
|---|---|
| `0` | Compressed. The output is written or returned. |
| `2` | The input is signed. The output is an unchanged copy. |
| `1` | Failed. `hpdf_compress_buffer` returns `NULL`. Read `hpdf_last_error()`. |

`hpdf_last_error()` returns an empty string after a success, or one of:

| Message | Function | What to do |
|---|---|---|
| `cannot open input (missing, corrupt, or wrong password)` | `hpdf_compress_file` | Check the path and the password. |
| `cannot parse input (corrupt or wrong password)` | `hpdf_compress_buffer` | Check the bytes and the password. |
| `engine pass failed` | both | The engine could not process this file. Keep the original. |
| `cannot open output for writing` | `hpdf_compress_file` | Check that the output folder exists and is writable. |
| `save failed` | both | Writing the result failed. Check disk space. |
| `input larger than 2 GB is not supported` | `hpdf_compress_buffer` | Use `hpdf_compress_file`, or the worker binary. |

The C API never makes the "never larger" comparison: compare sizes yourself and keep the original when it is smaller. See [C API](/c-api).

### Worker exit codes

`hpdf-worker`, the engine program the native surfaces run, exits with:

| Exit code | Meaning |
|---|---|
| `0` | Success, including a signed input written unchanged. |
| `1` | The engine pass failed. |
| `2` | Wrong arguments. |
| `3` | The input could not be opened (missing, corrupt or encrypted). |
| `4` | The output could not be written. |

When the SDK, CLI, crate or HTTP service runs the worker, a non-zero exit is not an error for you: Hyper falls back to a plain repack or to your original bytes. The exceptions: a worker that runs past the step limit gives `timeout`, and a failed pass on a file that needs a password you did not give gives `decrypt_failed`.

## Troubleshooting

**`no hpdf-worker binary for <platform>`.** There is no native engine for this platform (for example Intel macOS, or Alpine Linux where npm skips the glibc packages). Use the [WebAssembly package](/wasm) or the [Docker image](/self-hosting), or [build the engine](/building) and set `HYPER_DRV`.

**`decrypt_failed` with the right password.** The file may have more than one password. Try the other one. Only the text before the first line break of a password is used.

**`timeout` on large files.** Each step has a 10 minute limit by default, and a size target runs the engine up to 11 times. Raise the limits, drop the target, or pick a preset closer to the size you need.

**`hpdf-worker not found at <path>` or `qpdf not found at <path>`.** `HYPER_DRV`, `HYPER_QPDF` or an explicit path in the Rust crate points to a file that does not exist. Fix the path or unset it.

**`busy` from the HTTP API.** Raise `HYPER_CONCURRENCY` if the machine has spare cores, raise `HYPER_QUEUE` to let more requests wait, or retry with backoff. See [Running in production](/guides/production).

**Output equals input with no error.** Not an error: Hyper could not make the file smaller, or the input was signed.

## Next steps

- [Errors, timeouts and cancellation](/guides/errors-and-cancellation): handling these in your code.
- [Warnings](/warnings): what Hyper skipped, when it succeeded.
- [Running in production](/guides/production): limits, queues and untrusted uploads.
