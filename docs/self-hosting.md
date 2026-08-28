# Self-Hosting

Run the whole thing on your own hardware: the web interface, the [HTTP API](/http-api) and the CLI in one container. Your files never leave your server, which is usually the entire reason to self-host a PDF tool.

## What you need

Docker. If `docker --version` prints a version, you are ready; otherwise install [Docker Engine](https://docs.docker.com/engine/install/) (Linux) or [Docker Desktop](https://docs.docker.com/desktop/) (macOS, Windows). The published image is `linux/amd64`.

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

Images are published to GHCR on every release, tagged with the version (`v0.1.0`) and `latest`. Pin the version tag in production.

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
| `HYPER_API_TOKEN` | (unset) | when set, `/api/*` requires `Authorization: Bearer <token>` (or `x-api-token`); unauthenticated otherwise |

Each job is a separate engine process, so size `HYPER_CONCURRENCY` to your cores and memory: 2 is right for a small VPS, 4 to 8 for a real server.

## Put TLS in front

The container speaks plain HTTP and does no authentication, which is the standard contract for a service container: terminate TLS at a reverse proxy. With [Caddy](https://caddyserver.com), the entire config is:

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

The same image doubles as the CLI: pass arguments and it compresses instead of serving.

```bash
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  in.pdf out.pdf --preset high
```

## Troubleshooting

**`port is already allocated`.** Something else owns 8080. Map another host port: `-p 9090:8080`, then use `localhost:9090`.

**Requests return 429.** The queue is full. Raise `HYPER_QUEUE` for burst tolerance or `HYPER_CONCURRENCY` for throughput, and check `/healthz` to see the live numbers.

**Requests return 504.** A job hit the timeout, usually an enormous scan at high settings. Raise `HYPER_TIMEOUT_MS`.

**The container runs but the page will not load.** Confirm the port mapping (`docker ps` shows `0.0.0.0:8080->8080`) and that you are browsing the host port from the left side of that arrow.
