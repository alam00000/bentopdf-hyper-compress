# HTTP API

The [self-hosted Docker image](/self-hosting) serves a web interface at `/` and this API. Everything runs on your server; nothing leaves it. If you have not started the container yet, that is one command:

```bash
docker run -d -p 8080:8080 ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

## Your first request

Send the PDF as the raw request body; the compressed PDF comes back as the response body.

```bash
curl -o out.pdf --data-binary @in.pdf \
  'http://localhost:8080/api/compress?preset=high'
```

Check what happened without opening the file:

```bash
curl -s -o out.pdf -D - --data-binary @in.pdf \
  'http://localhost:8080/api/compress?preset=high' | grep -i '^x-'
```

```
x-original-size: 2593056
x-compressed-size: 913568
x-signed: false
x-pdfa:
x-met-target:
x-warnings:
```

`--data-binary` matters: plain `-d` would mangle the bytes.

## POST /api/compress

### Query parameters

| Parameter | Meaning |
|---|---|
| `preset` | `low`, `medium` (default), `high`, `lossless` or `custom` |
| `options` | JSON object of [engine options](/options), at most 4096 bytes, applied on top of the preset. Required when `preset=custom`; fields you leave unset fall back to the `medium` baseline. |
| `targetSizeBytes` | search toward a size, down to quality 5 and 36 dpi; on a miss you get the smallest achievable file and `X-Met-Target: false` |
| `brotli=true` | PDF 2.0 Brotli stream compression (opt in; the reader must support it) |
| `preservePdfa=true` | keep a PDF/A input conformant; anything that would break the standard is skipped and reported in `X-Warnings` |

Custom settings, URL-encoded by curl itself:

```bash
curl -o out.pdf --data-binary @in.pdf -G -X POST \
  'http://localhost:8080/api/compress' \
  --data-urlencode 'preset=custom' \
  --data-urlencode 'options={"imageQuality":35,"grayscale":true}'
```

### Headers

| Header | Direction | Meaning |
|---|---|---|
| `X-Password` | request | password for encrypted input |
| `X-Original-Size` | response | input size in bytes |
| `X-Compressed-Size` | response | output size in bytes; never larger than the input |
| `X-Signed` | response | `true` when a signed input was returned untouched |
| `X-Pdfa` | response | the input's PDF/A claim, like `2b`, or empty |
| `X-Met-Target` | response | `true` or `false` when a target was set, else empty |
| `X-Warnings` | response | URL-encoded JSON array of options skipped for safety, else empty |

Decode warnings in one line: `decodeURIComponent(header)` then `JSON.parse`.

### Errors

Errors are JSON bodies with an `error` code and the matching HTTP status:

| Status | Code | Meaning | What to do |
|---|---|---|---|
| 400 | `not_a_pdf` | body does not start with a PDF header | send the file with `--data-binary` |
| 400 | `bad_preset` | unknown preset | the response lists valid ones |
| 400 | `bad_options` | options JSON invalid or over 4096 bytes | fix the JSON |
| 400 | `options_required` | `preset=custom` without `options` | add the options parameter |
| 400 | `decrypt_failed` | wrong or missing password | send `X-Password` |
| 413 | `too_large` | body exceeds the upload limit | raise `HYPER_MAX_UPLOAD_MB` |
| 429 | `busy` | queue full | retry with backoff, or raise `HYPER_QUEUE` |
| 504 | `timeout` | job exceeded the per-job timeout | raise `HYPER_TIMEOUT_MS` |

## POST /api/verify-password

Send the encrypted PDF as the body with `X-Password`; returns `{"valid":true}` or `{"valid":false}` without compressing. `true` means `/api/compress` will accept the same password. Useful for validating a user's password before queueing real work.

```bash
curl -s --data-binary @locked.pdf -H 'X-Password: hunter2' \
  http://localhost:8080/api/verify-password
```

## GET /healthz

```bash
curl -s http://localhost:8080/healthz
```

```json
{"ok":true,"active":0,"queued":0}
```

`active` is jobs currently compressing, `queued` is jobs waiting for a slot. Wire it into your load balancer or uptime monitoring as is.

## Limits and tuning

Concurrency, queue depth, upload size and timeout are environment variables on the container. Defaults: 2 parallel jobs, a queue of 8, 500 MB uploads, 10 minutes per job. See [Self-Hosting](/self-hosting#configuration).
