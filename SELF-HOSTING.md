# Self-hosting

The docker image runs a small web service: a branded upload page at `/` and an
HTTP API, both backed by the native engine. Files are processed on your server
and never leave it.

```bash
docker run -d -p 8080:8080 ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

or with compose:

```bash
docker compose up -d     # uses the docker-compose.yml in this repo
```

Open http://localhost:8080 for the upload page.

## API

`POST /api/compress` with the PDF as the raw request body. The response body is
the compressed PDF.

```bash
curl -o out.pdf --data-binary @in.pdf \
  'http://localhost:8080/api/compress?preset=high'
```

Query parameters:

- `preset`: `low`, `medium` (default), `high`, `lossless`, or `custom`
- `options`: JSON object of engine options, at most 4096 bytes, applied on top
  of the preset. Required when `preset=custom`; fields you leave unset fall
  back to the `medium` baseline.
- `targetSizeBytes`: search toward a size, down to quality 5 and 36 dpi; on a
  miss you get the smallest achievable file and `X-Met-Target: false`
- `brotli=true`: PDF 2.0 Brotli stream compression (opt-in; readers vary)

Headers:

- `X-Password` (request): password for encrypted input
- `X-Original-Size`, `X-Compressed-Size`, `X-Signed`, `X-Pdfa`,
  `X-Met-Target`, `X-Warnings` (response). `X-Warnings` is a URL-encoded JSON
  array of notes, for example a rasterize request that was dropped to preserve
  PDF/A conformance.

Errors are JSON: `400 not_a_pdf` / `bad_preset` / `bad_options` /
`options_required` / `decrypt_failed`, `413 too_large`, `429 busy`
(queue full), `504 timeout`.

`GET /healthz` returns `{ok, active, queued}` for monitoring.

## Configuration

| env var | default | meaning |
|---|---|---|
| `HYPER_PORT` | 8080 | listen port |
| `HYPER_MAX_UPLOAD_MB` | 500 | reject larger uploads (engine hard cap is 2 GB) |
| `HYPER_CONCURRENCY` | 2 | parallel compression jobs |
| `HYPER_QUEUE` | 8 | jobs allowed to wait before 429 |
| `HYPER_TIMEOUT_MS` | 600000 | per-job timeout |

The container runs as a non-root user. Hostile input is parsed in a worker
subprocess with a timeout; a crash kills the worker, not the service.

Put a reverse proxy (nginx, Caddy, Traefik) in front for TLS and, if the
instance is public, rate limiting; the service itself does no authentication.

## CLI mode

The same image doubles as the CLI: pass arguments and it compresses instead
of serving:

```bash
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  in.pdf out.pdf --preset high
```

## Without docker

```bash
npm ci && npm run serve      # needs built worker binaries in cli/prebuilt/
```
