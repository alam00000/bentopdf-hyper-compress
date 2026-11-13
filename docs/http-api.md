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

## Authentication

By default the API is open to anyone who can reach the port. Set `HYPER_API_TOKEN` on the container and every `/api/*` request must carry the token, in either header:

```bash
curl -o out.pdf --data-binary @in.pdf \
  -H 'Authorization: Bearer my-secret-token' \
  'http://localhost:8080/api/compress'

curl -o out.pdf --data-binary @in.pdf \
  -H 'x-api-token: my-secret-token' \
  'http://localhost:8080/api/compress'
```

A missing or wrong token gets `401` with `{"error":"unauthorized"}`. The web page at `/` and `/healthz` stay open, so health checks need no token. The web page calls the API without a token, so with a token set the page loads but cannot compress. If you want both the page and protection, leave the token unset and put a login in your reverse proxy instead.

## POST /api/compress

### Query parameters

| Parameter | Meaning |
|---|---|
| `preset` | `low`, `medium` (default), `high`, `lossless` or `custom` |
| `options` | JSON object of [engine options](/options), at most 4096 characters, applied on top of the preset. Required when `preset=custom`; fields you leave unset fall back to the `medium` preset. Out of range values are clamped and unknown fields are ignored. |
| `targetSizeBytes` | a size in bytes, greater than zero. Hyper searches toward it, down to quality 5 and 36 dpi; on a miss you get the smallest achievable file and `X-Met-Target: false` |
| `brotli=true` | PDF 2.0 Brotli stream compression (opt in; the reader must support it) |
| `preservePdfa=true` | keep a PDF/A input conformant; anything that would break the standard is skipped and reported in `X-Warnings`. Same as `preserveConformance` in `options`. |

`brotli=true` and `preservePdfa=true` win over the same fields in `options`. Any other value for them, such as `1`, counts as not set.

The request body is the PDF itself, with any `Content-Type`. Requests are queued before the body is read, so a queued client waits with its upload paused.

Custom settings, URL-encoded by curl itself:

```bash
curl -o out.pdf --data-binary @in.pdf \
  --url-query 'preset=custom' \
  --url-query 'options={"imageQuality":35,"grayscale":true}' \
  'http://localhost:8080/api/compress'
```

`--url-query` needs curl 7.87 or newer. Do not use `-G` here: it moves the PDF itself into the URL.

### Headers

| Header | Direction | Meaning |
|---|---|---|
| `X-Password` | request | password for encrypted input |
| `X-Original-Size` | response | input size in bytes |
| `X-Compressed-Size` | response | output size in bytes; never larger than the input |
| `Authorization` or `x-api-token` | request | the API token, when `HYPER_API_TOKEN` is set |
| `Content-Type` | response | `application/pdf` |
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
| 400 | `bad_target_size` | `targetSizeBytes` is not a number greater than zero | send a byte count |
| 400 | `bad_options` | options JSON invalid, not an object, or over 4096 characters | fix the JSON |
| 400 | `options_required` | `preset=custom` without `options` | add the options parameter |
| 400 | `decrypt_failed` | wrong or missing password | send `X-Password` |
| 401 | `unauthorized` | `HYPER_API_TOKEN` is set and the token is missing or wrong | send the token |
| 404 | `not_found` | unknown path or method | check the URL; the API only takes `POST` |
| 413 | `too_large` | body exceeds the upload limit; the response includes `maxBytes` | raise `HYPER_MAX_UPLOAD_MB` |
| 429 | `busy` | queue full; the response includes `detail` | retry with backoff, or raise `HYPER_QUEUE` |
| 500 | `engine_error` | the engine could not process the document | check the file opens in a viewer; report it if it does |
| 500 | `cancelled` | the job was stopped | retry |
| 500 | `internal` | an unexpected server error | check the container logs |
| 504 | `timeout` | job exceeded the per-job timeout | raise `HYPER_TIMEOUT_MS` |

Example error body:

```json
{"error":"too_large","maxBytes":524288000}
```

## POST /api/verify-password

Send the encrypted PDF as the body with `X-Password`; returns `{"valid":true}` or `{"valid":false}` without compressing. `true` means `/api/compress` will accept the same password. Useful for validating a user's password before queueing real work. An unencrypted PDF returns `{"valid":true}` for any password.

It shares the queue, upload limit and token with `/api/compress`, so it can also return `401`, `413`, `429` and `500` with the same bodies. The check itself has a 60 second limit.

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

## The web interface

`GET /` serves the same page as [hyper.bentopdf.com](https://hyper.bentopdf.com), wired to this server's API instead of the in-browser engine, so files are compressed on the server. Its scripts, styles and fonts are served from `/web/` and `/dist/`.

## Limits and tuning

Concurrency, queue depth, upload size and timeout are environment variables on the container. Defaults: 2 parallel jobs, a queue of 8, 500 MB uploads, 10 minutes per job. See [Self-Hosting](/self-hosting#configuration).
