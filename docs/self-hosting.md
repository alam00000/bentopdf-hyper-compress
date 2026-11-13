---
title: Self-hosting
description: Run the Hyper Compress web page, HTTP API and CLI on your own server with Docker, Docker Compose or Kubernetes, and configure, secure and size it.
---

# Self-hosting

Run Hyper Compress on your own hardware: the web page, the [HTTP API](/http-api) and the [CLI](/cli), all in one container image. Your files never leave your server.

```bash
docker run -d --name hyper -p 8080:8080 \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

## Before you begin

- [Docker Engine](https://docs.docker.com/engine/install/) on Linux, or [Docker Desktop](https://docs.docker.com/desktop/) on macOS and Windows. Check with `docker --version`.
- An x64 or ARM64 machine. [Sizing](#sizing) helps you plan cores and memory.

## Quick start

1. Start the container:

   ```bash
   docker run -d --name hyper -p 8080:8080 \
     ghcr.io/alam00000/bentopdf-hyper-compress:latest
   ```

2. Check that it is up:

   ```bash
   curl -s http://localhost:8080/healthz
   ```

   ```json
   {"active":0,"ok":true,"queued":0}
   ```

3. Open `http://localhost:8080` in a browser for the web page, or compress from the command line:

   ```bash
   curl -o output.pdf --data-binary @input.pdf \
     'http://localhost:8080/api/compress?preset=high'
   ```

On start the container logs one line with its address, concurrency and upload limit:

```text
hyper-compress server on http://0.0.0.0:8080 (concurrency 2, max upload 500MB)
```

The service logs nothing about the documents it compresses.

## Docker Compose

The repository ships a [`docker-compose.yml`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/docker-compose.yml):

```yaml
services:
  hyper-compress:
    image: ghcr.io/alam00000/bentopdf-hyper-compress:latest
    container_name: hyper-compress
    restart: unless-stopped
    ports:
      - '8080:8080'
    environment:
      - HYPER_MAX_UPLOAD_MB=500
      - HYPER_CONCURRENCY=2
    tmpfs:
      - /tmp
```

Start it from the folder that holds the file:

```bash
docker compose up -d
```

`tmpfs: /tmp` keeps each request's temporary files in memory instead of on disk. See [Temporary files](#temporary-files) for what that costs.

## The image

The image is `ghcr.io/alam00000/bentopdf-hyper-compress`. Every release publishes one multi-platform image for `linux/amd64` and `linux/arm64`, and Docker pulls the one that matches your machine: x64 servers, ARM servers such as AWS Graviton, and Apple silicon Macs.

| Tag | Points to |
|---|---|
| `latest` | the newest release |
| `v1.1.4` (the release version, with a leading `v`) | that release, for both platforms |
| `v1.1.4-amd64`, `v1.1.4-arm64` | that release for one platform |

What is inside:

| Path | Contents |
|---|---|
| `/engine/cli/prebuilt/hyper-server` | the HTTP service, a Rust program |
| `/engine/cli/prebuilt/hyper` | the CLI, a Rust program that takes the same flags as the npm `hyper` command |
| `/engine/cli/prebuilt/hpdf-worker` | the compression engine, run as a separate process for each file |
| `/engine/cli/prebuilt/hpdf-render` | a page renderer used by the test suite |
| `/usr/bin/qpdf` | qpdf from Debian, used to decrypt and repack files |
| `/engine/web` | the web page served at `/`, with its fonts, icons and images |
| `/engine/dist/sdk/node` | the preset and option definitions the web page loads |
| `/entrypoint.sh` | starts the service or the CLI |

The base is `debian:trixie-slim`. There is no Node.js, no compiler and no shell tool beyond what Debian's base includes; `curl` and `wget` are not installed.

The container runs as the user `hyper` (uid 1000), not as root. The working folder is `/work` and the exposed port is `8080`.

What the container runs depends on its arguments:

| Arguments | Runs |
|---|---|
| none, or `serve` | the HTTP service |
| anything else | the CLI with those arguments, see [CLI mode](#cli-mode) |

## Environment variables

Set these with `-e NAME=value` on `docker run`, or under `environment:` in Compose.

| Name | Type | Default | Description |
|---|---|---|---|
| `HYPER_PORT` | integer, 1 to 65535 | `8080` | Port the service listens on inside the container. Change the `-p` mapping to match. |
| `HYPER_HOST` | address | `0.0.0.0` | Address the service listens on. Keep `0.0.0.0` in a container; see the tip below. Use brackets for IPv6, for example `[::]`. |
| `HYPER_MAX_UPLOAD_MB` | integer, 1 to 1048576 | `500` | Largest request body in megabytes. Larger uploads get `413`. |
| `HYPER_CONCURRENCY` | integer, 1 to 1024 | `2` | Jobs that compress at the same time. |
| `HYPER_QUEUE` | integer, 0 to 100000 | `8` | Jobs that may wait for a free slot. More get `429`. `0` means no waiting. |
| `HYPER_TIMEOUT_MS` | integer, 1000 to 86400000 | `600000` | Time limit for one job in milliseconds (10 minutes by default), for the whole job and for each step in it. Over the limit gets `504`. |
| `HYPER_API_TOKEN` | string | not set | When set, `/api/compress` and `/api/verify-password` require this token. See [Authentication](#authentication). |
| `HYPER_ROOT` | path | found automatically | Folder that holds `web/index.html` and `dist/sdk/node`. Without it the service looks in up to 8 parent folders of its own program. |
| `HYPER_DRV` | path | found automatically | The `hpdf-worker` program. Without it the service looks next to its own program, then in `cli/prebuilt` in up to 8 parent folders. A path that does not exist makes every job fail with `engine_error`. |
| `HYPER_QPDF` | path | `/usr/bin/qpdf` in the image | The `qpdf` program. Found the same way as `hpdf-worker` when not set. A path that does not exist makes jobs fail with `engine_error`. |

How numbers are read:

- Unset, blank, or not a number: the default is used.
- Decimals are rounded down. `2.9` becomes `2`.
- Out of range: the nearest end of the range is used. `HYPER_CONCURRENCY=5000` becomes `1024`, `HYPER_TIMEOUT_MS=10` becomes `1000`.

::: tip
To keep the service off the network when a proxy runs on the same host, bind the published port to localhost with `-p 127.0.0.1:8080:8080`. Do not set `HYPER_HOST=127.0.0.1` inside a container: the service then only listens inside the container and Docker cannot reach it.
:::

For example, a bigger server with a token:

```bash
docker run -d --name hyper -p 8080:8080 \
  -e HYPER_CONCURRENCY=6 \
  -e HYPER_QUEUE=32 \
  -e HYPER_TIMEOUT_MS=1200000 \
  -e HYPER_API_TOKEN=my-secret-token \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

## Sizing

Each job runs the engine as a separate process. Inside one job the engine uses as many threads as the machine has cores, up to 8, for image work. Memory per job grows with the largest images in the document, so large scans need more headroom than text documents.

Set `HYPER_CONCURRENCY` to the number of CPUs the container can use: one job per CPU keeps every core busy, and fewer leaves cores idle (on a 32-CPU server, 4 jobs at once did under a third of the work of 32). The default `2` suits a small VPS. Plan about 1.5 GB of memory per job; large scans need more. Raise `HYPER_QUEUE` to absorb bursts without `429`; each waiting request holds an open connection and nothing else. Then measure with your own files.

Measured throughput: in the [throughput benchmark](/benchmarks#throughput), with one job per CPU on 5,003 real PDFs, an 8-CPU Arm server compressed 11,694 PDFs an hour (65,471 an hour of files under 5 MB) and a 32-CPU Arm server 36,888 an hour, with memory peaking at 10 GB and 23 GB.

Measured speed: in the [benchmark](/benchmarks), 1,000 real PDFs (3 MB on average) were compressed 4 at a time on a MacBook Pro with an Apple M4 and 16 GB of memory, through the CLI. The median file took 0.15 seconds with `lossless`, 0.21 s with `low`, 0.25 s with `medium` and 0.33 s with `high`, including starting the process. The whole set took 8.0 minutes with `lossless` and 14.5 to 20.7 minutes with the other presets. Your hardware and documents will give different numbers.

To handle more traffic, run more containers behind a load balancer. They share nothing, so any request can go to any container.

## Temporary files

Each request writes its input and output to a new folder under `/tmp` and deletes the folder when the response is sent. Nothing is kept between requests.

Plan `/tmp` space of a few times your largest upload for each running job: the input, the engine's output and the repacked output each take a copy. With the Compose file, `/tmp` is a tmpfs, so that space comes out of the container's memory. If memory is tight, remove the `tmpfs` entry and `/tmp` uses the container's disk instead.

## Put TLS in front

The service speaks plain HTTP. Put a reverse proxy in front of it for HTTPS. If the service is public, add rate limiting in the proxy too.

::: code-group

```text [Caddy]
compress.example.com {
    reverse_proxy localhost:8080
}
```

```nginx [nginx]
server {
    listen 443 ssl;
    server_name compress.example.com;

    ssl_certificate     /etc/ssl/certs/compress.example.com.pem;
    ssl_certificate_key /etc/ssl/private/compress.example.com.key;

    client_max_body_size 500m;
    proxy_read_timeout   660s;
    proxy_send_timeout   660s;

    location / {
        proxy_pass http://127.0.0.1:8080;
    }
}
```

:::

[Caddy](https://caddyserver.com) gets and renews the certificate itself. With nginx, keep these in step with the service:

- `client_max_body_size` at least `HYPER_MAX_UPLOAD_MB`. The nginx default is 1 MB.
- `proxy_read_timeout` longer than `HYPER_TIMEOUT_MS` plus the time a request may wait in the queue. The nginx default is 60 seconds.

## Authentication

Set `HYPER_API_TOKEN` and every API request must send the token as `Authorization: Bearer <token>` or `x-api-token: <token>`. A missing or wrong token gets `401`.

```bash
docker run -d --name hyper -p 8080:8080 \
  -e HYPER_API_TOKEN=my-secret-token \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

`/healthz`, the web page and its files stay open. The web page cannot send a token, so with a token set the page loads but cannot compress. To protect a page that people use in a browser, leave the token unset and add a login in your reverse proxy. [HTTP API: Authentication](/http-api#authentication) has the details.

## CLI mode

Pass arguments to the image and it runs the CLI instead of the service. Every [CLI flag](/cli) works. Paths are relative to `/work`, so mount your folder there:

```bash
docker run --rm -v "$PWD":/work \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf --preset high
```

```text
output.pdf
  2593056 -> 915051 bytes (65% smaller)
```

The container writes as uid 1000. If your user on the host has a different uid, run the container as your user so it can write to the folder:

```bash
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/work \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf
```

## Health checks

`GET /healthz` answers `{"active":0,"ok":true,"queued":0}` with status `200` whenever the service is running. It needs no token and never waits in the queue. The image does not define a Docker `HEALTHCHECK`.

The image has no `curl` or `wget`, so a check that runs inside the container cannot use them. Use one of these instead:

- **An HTTP probe from outside the container.** Kubernetes probes, load balancer health checks and uptime monitors call `/healthz` over the network. This is the recommended option.
- **A Compose health check with bash.** bash is in the image and can open a TCP connection by itself:

  ```yaml
  services:
    hyper-compress:
      image: ghcr.io/alam00000/bentopdf-hyper-compress:latest
      healthcheck:
        test: ["CMD", "bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/8080 && printf 'GET /healthz HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q '\"ok\":true' <&3"]
        interval: 30s
        timeout: 5s
        retries: 3
  ```

  Change `8080` if you set `HYPER_PORT`.

Watch `queued` as well as `ok`. When `queued` stays near `HYPER_QUEUE`, raise `HYPER_CONCURRENCY` or add containers.

## Kubernetes

A Deployment with two replicas and a Service:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: hyper-compress
spec:
  replicas: 2
  selector:
    matchLabels:
      app: hyper-compress
  template:
    metadata:
      labels:
        app: hyper-compress
    spec:
      securityContext:
        runAsNonRoot: true
        runAsUser: 1000
        runAsGroup: 1000
      containers:
        - name: hyper-compress
          image: ghcr.io/alam00000/bentopdf-hyper-compress:v1.1.4
          ports:
            - containerPort: 8080
          env:
            - name: HYPER_CONCURRENCY
              value: "2"
            - name: HYPER_QUEUE
              value: "8"
          resources:
            requests:
              cpu: "2"
              memory: 2Gi
            limits:
              memory: 4Gi
          readinessProbe:
            httpGet:
              path: /healthz
              port: 8080
            periodSeconds: 10
          livenessProbe:
            httpGet:
              path: /healthz
              port: 8080
            initialDelaySeconds: 5
            periodSeconds: 30
          volumeMounts:
            - name: tmp
              mountPath: /tmp
      volumes:
        - name: tmp
          emptyDir: {}
---
apiVersion: v1
kind: Service
metadata:
  name: hyper-compress
spec:
  selector:
    app: hyper-compress
  ports:
    - port: 80
      targetPort: 8080
```

The resource numbers are a starting point; scans with large images need more memory. To require a token, add `HYPER_API_TOKEN` to `env` from a Secret with `valueFrom.secretKeyRef`. The probes keep working because `/healthz` never needs a token.

`/healthz` reports `ok` even when the queue is full, so a busy pod stays in the Service. Spread load with more replicas, not with the probes.

## Upgrade and pin versions

The container keeps no state, so upgrading is replacing it. Pin a version tag in production, so an upgrade happens only when you change the tag:

```yaml
image: ghcr.io/alam00000/bentopdf-hyper-compress:v1.1.4
```

With Compose, change the tag, then:

```bash
docker compose pull
docker compose up -d
```

With `docker run`:

```bash
docker pull ghcr.io/alam00000/bentopdf-hyper-compress:v1.1.4
docker stop hyper && docker rm hyper
docker run -d --name hyper -p 8080:8080 \
  ghcr.io/alam00000/bentopdf-hyper-compress:v1.1.4
```

The [releases page](https://github.com/alam00000/bentopdf-hyper-compress/releases) lists every version and what changed.

## Run without Docker

The service is one Rust program, `hyper-server`. It needs the engine programs `hpdf-worker` and `qpdf`, and, for the web page, the repository's `web` folder and the built `dist/sdk/node` folder.

From a repository checkout with the engine built (see [Building from source](/building)), one command builds the web page's files and starts the service:

```bash
npm run serve
```

To install it somewhere else:

1. Build the program:

   ```bash
   cargo build --release -p hyper-compress-server --manifest-path rust/Cargo.toml
   ```

2. Copy `rust/target/release/hyper-server` into the same folder as `hpdf-worker` and `qpdf`, or set `HYPER_DRV` and `HYPER_QPDF` to their paths.
3. For the web page, set `HYPER_ROOT` to a folder that holds `web/` and `dist/sdk/node/` (run `npm run build` in the checkout to create `dist`). Without it, `/` shows a short placeholder page and the API works as usual.
4. Start it:

   ```bash
   HYPER_PORT=8080 ./hyper-server
   ```

The same environment variables apply. The engine runs on macOS (Apple silicon), Linux x64 and ARM64, and Windows x64; see [Platforms](/platforms).

## Troubleshooting

**`port is already allocated`.** Another program uses port 8080 on the host. Map another host port, for example `-p 9090:8080`, and use `http://localhost:9090`.

**`cannot listen on 0.0.0.0:8080`.** The service could not open its port inside the container, usually because `HYPER_PORT` or `HYPER_HOST` has a wrong value. The message ends with the reason.

**The page does not load.** Check the mapping with `docker ps`: it shows `0.0.0.0:8080->8080/tcp`. Browse to the host port on the left of the arrow. If you set `HYPER_HOST=127.0.0.1` in the container, remove it.

**Requests return `401`.** `HYPER_API_TOKEN` is set. Send the token; see [Authentication](#authentication).

**Requests return `413`.** The upload is larger than `HYPER_MAX_UPLOAD_MB`. Raise it, and raise `client_max_body_size` if nginx is in front.

**Requests return `429`.** All slots are busy and the queue is full. Retry after the `Retry-After` delay (1 second), raise `HYPER_QUEUE` to absorb bursts, or raise `HYPER_CONCURRENCY` for more throughput. `/healthz` shows the live numbers.

**Requests return `504`.** A job, or one step of it, ran longer than `HYPER_TIMEOUT_MS`, usually a very large scan with heavy settings. Raise the limit, and the proxy timeout with it.

**`Permission denied` in CLI mode.** The container writes as uid 1000 and cannot write to your folder. Add `--user "$(id -u):$(id -g)"`.

**Requests return `500` `engine_error` after you set `HYPER_DRV` or `HYPER_QPDF`.** The path does not point at the program. The service sends only the error code. Fix the path or unset the variable.

**Jobs fail or the container is killed when memory runs out.** Large scans need more memory per job. Lower `HYPER_CONCURRENCY`, give the container more memory, or move `/tmp` off tmpfs.

**`web/index.html changed shape; update the server transform` on start.** The service found a web page it does not recognise, usually through a `HYPER_ROOT` that points at changed files. Point `HYPER_ROOT` at the repository's own `web` folder or unset it.

## Next steps

- [HTTP API](/http-api): every endpoint, parameter, header and error.
- [Running in production](/guides/production): batches, retries and untrusted uploads.
- [CLI](/cli): every flag for CLI mode.
- [Platforms](/platforms): where each way of running Hyper works.
