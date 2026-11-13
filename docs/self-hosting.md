# Self-Hosting

Run the whole thing on your own hardware: the web interface, the [HTTP API](/http-api) and the CLI in one container. Your files never leave your server, which is usually the entire reason to self-host a PDF tool.

## What you need

Docker. If `docker --version` prints a version, you are ready; otherwise install [Docker Engine](https://docs.docker.com/engine/install/) (Linux) or [Docker Desktop](https://docs.docker.com/desktop/) (macOS, Windows). The published image runs on `linux/amd64` and `linux/arm64`; Docker picks the right one for your machine, including Apple silicon Macs and ARM servers such as AWS Graviton.

## Start it

```bash
docker run -d --name hyper -p 8080:8080 \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

Open `http://localhost:8080`: that is the same interface as [hyper.bentopdf.com](https://hyper.bentopdf.com), except compression happens on this machine through the server API. Programs talk to `/api/compress`; see the [HTTP API](/http-api).

Or with compose, using the `docker-compose.yml` from the repository:

```bash
docker compose up -d
```

Images are published to GHCR on every release, tagged with the version (for example `v1.1.4`) and `latest`. Pin the version tag in production.

## Configuration

Set these with `-e` flags or in your compose file:

| Env var | Default | Meaning |
|---|---|---|
| `HYPER_HOST` | 0.0.0.0 | listen address; set to 127.0.0.1 to bind localhost only |
| `HYPER_PORT` | 8080 | listen port inside the container |
| `HYPER_MAX_UPLOAD_MB` | 500 | reject larger uploads (the engine's hard cap is 2 GB) |
| `HYPER_CONCURRENCY` | 2 | parallel compression jobs |
| `HYPER_QUEUE` | 8 | jobs allowed to wait before new ones get 429 |
| `HYPER_TIMEOUT_MS` | 600000 | total per-request timeout across all stages (10 minutes) |
| `HYPER_API_TOKEN` | (unset) | when set, `/api/*` requires `Authorization: Bearer <token>` (or `x-api-token`); unauthenticated otherwise. See [authentication](/http-api#authentication). |

Numbers outside the allowed range are clamped and a value that is not a number falls back to the default. Allowed ranges: `HYPER_PORT` 1 to 65535, `HYPER_MAX_UPLOAD_MB` from 1, `HYPER_CONCURRENCY` 1 to 1024, `HYPER_QUEUE` 0 to 100,000 (0 means no waiting: anything beyond the running jobs gets 429), `HYPER_TIMEOUT_MS` 1 second to 24 hours.

On start the server logs its address, concurrency and upload limit.

## Sizing

Each job is a separate engine process, and inside one job the engine uses up to 8 threads for image work. Size `HYPER_CONCURRENCY` to your cores and memory: 2 is right for a small VPS, 4 to 8 for a real server. Memory per job grows with the largest images in the document, so give large scans more headroom.

For a sense of speed: in the [commercial SDK benchmark](/benchmarks#against-commercial-sdks), 1,000 real PDFs averaging 3 MB each ran 4 at a time on an Apple M4 laptop with 16 GB of memory. The median file took 0.15 seconds lossless and 0.21 to 0.33 seconds at the lossy presets, including starting the process, and the whole set took 8 minutes lossless and 14.5 to 21 minutes lossy. Times on your hardware and documents will differ, so measure with your own files.

To scale out, run more containers behind a load balancer. They share nothing, so any request can go to any container.

## Temporary files

Each request writes its input and output to a fresh folder under the container's temporary directory and deletes it when the response is sent. Nothing is kept between requests and nothing is logged about the documents. Give `/tmp` room for a few times your largest upload per running job.

## Put TLS in front

The container speaks plain HTTP, which is the standard contract for a service container: terminate TLS at a reverse proxy. Its only authentication is the optional `HYPER_API_TOKEN`. With [Caddy](https://caddyserver.com), the entire config is:

```
compress.example.com {
    reverse_proxy localhost:8080
}
```

Caddy obtains and renews the certificate itself. Any reverse proxy works (nginx, Traefik); if the instance is public, add rate limiting there too.

## Updating

```bash
docker pull ghcr.io/alam00000/bentopdf-hyper-compress:latest
docker stop hyper && docker rm hyper
docker run -d --name hyper -p 8080:8080 \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

The container is stateless; nothing needs migrating.

## Monitoring

`GET /healthz` returns `{ok, active, queued}`. Wire it into uptime checks as is; sustained `queued` near `HYPER_QUEUE` means you should raise concurrency or add hardware.

## CLI mode

The same image doubles as the CLI: pass arguments and it compresses instead of serving. Every [CLI flag](/cli) works, and paths are relative to `/work`, the folder you mount.

```bash
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  in.pdf out.pdf --preset high
```

## Troubleshooting

**`port is already allocated`.** Something else owns 8080. Map another host port: `-p 9090:8080`, then use `localhost:9090`.

**Requests return 429.** The queue is full. Raise `HYPER_QUEUE` for burst tolerance or `HYPER_CONCURRENCY` for throughput, and check `/healthz` to see the live numbers.

**Requests return 401.** `HYPER_API_TOKEN` is set. Send the token, see [authentication](/http-api#authentication).

**Requests return 413.** The upload is over `HYPER_MAX_UPLOAD_MB`. Raise it, up to the engine's 2 GB limit.

**Requests return 504.** A job hit the timeout, usually an enormous scan at high settings. Raise `HYPER_TIMEOUT_MS`.

**The container runs but the page will not load.** Confirm the port mapping (`docker ps` shows `0.0.0.0:8080->8080`) and that you are browsing the host port from the left side of that arrow.
