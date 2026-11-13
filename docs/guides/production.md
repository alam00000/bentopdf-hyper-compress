---
title: Running in production
description: Run Hyper Compress at scale, from batch jobs to a shared HTTP service, with the right concurrency, memory, limits and safety for untrusted uploads.
---

# Running in production

This guide covers what changes when Hyper Compress moves from one file on your laptop to thousands of files or a shared service: how to choose a surface, how many jobs to run at once, how fast to expect it to be, and how to accept files from people you do not trust.

## Before you begin

- Compress one file first. See the [Quickstart](/getting-started).
- Pick your preset and options on a sample of real files. See [Choose the right settings](/guides/choosing-settings).
- For the HTTP service, have Docker. See [Self-hosting](/self-hosting).

## 1. Choose how to run Hyper

| You have | Use | Why |
|---|---|---|
| A Node.js app or worker on macOS (Apple silicon), Linux (glibc, x64 or ARM64) or Windows x64 | [Node.js SDK](/node-sdk) | No network hop. Each job runs in its own engine processes, with timeouts and cancellation. |
| Several services or languages that need compression, or a team that should not install anything | [HTTP API](/http-api) in [Docker](/self-hosting) | One shared service with a queue, an upload limit and a token. |
| A browser, or a platform without a native build (Alpine, Intel Macs, FreeBSD) | [WebAssembly SDK](/wasm) | Runs anywhere. Single threaded, no timeouts, no cancellation, and the engine runs inside your process. |
| A Rust program | [Rust crate](/rust) | The same pipeline as the Node.js SDK, with timeouts and a cancel token. |
| Another language that calls C | [C API](/c-api) | The engine pass only, inside your process. Compare sizes and handle isolation yourself. |

All of them produce the same file for the same settings.

## 2. Process a folder

For a one-off batch, run files one after another. Each job already uses several CPU cores, so a long queue of files keeps the machine busy without extra work.

::: code-group

```bash [CLI]
mkdir -p out
for f in in/*.pdf; do
  hyper "$f" "out/$(basename "$f")" || echo "failed: $f" >&2
done
```

```js [Node.js]
import { mkdir, readdir } from 'node:fs/promises';
import path from 'node:path';
import { compress } from 'hyper-compress';

await mkdir('out', { recursive: true });
for (const name of await readdir('in')) {
  if (!name.toLowerCase().endsWith('.pdf')) continue;
  try {
    const r = await compress({
      sourcePath: path.join('in', name),
      savePath: path.join('out', name),
      totalTimeoutMs: 5 * 60 * 1000,
    });
    console.log(name, r.originalSize, '->', r.compressedSize);
  } catch (e) {
    console.error(name, 'failed:', e.code ?? e.message);
  }
}
```

```bash [Docker]
mkdir -p out
for f in in/*.pdf; do
  docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
    "$f" "out/$(basename "$f")"
done
```

:::

```text
report.pdf 2593056 -> 1250960
scan.pdf 3735039 -> 486287
```

One failed file does not stop the batch: each error is caught and logged, and the loop moves on. Errors are rare; a broken PDF usually comes back as its original bytes instead. See [Errors, timeouts and cancellation](/guides/errors-and-cancellation).

### Run a few at a time

On a machine with many cores, two to four jobs at once finish a batch sooner. Inside one job the native engine uses up to 8 threads for image work, or as many cores as the machine has if fewer, so more parallel jobs than that rarely help.

::: code-group

```bash [CLI]
mkdir -p out
find in -name '*.pdf' -print0 |
  xargs -0 -P 3 -I{} sh -c 'hyper "$1" "out/$(basename "$1")"' _ {}
```

```js [Node.js]
import { mkdir, readdir } from 'node:fs/promises';
import path from 'node:path';
import { compress } from 'hyper-compress';

const CONCURRENCY = 3;
const files = (await readdir('in')).filter((n) => n.toLowerCase().endsWith('.pdf'));
await mkdir('out', { recursive: true });

let next = 0;
async function worker() {
  while (next < files.length) {
    const name = files[next++];
    try {
      const r = await compress({
        sourcePath: path.join('in', name),
        savePath: path.join('out', name),
        totalTimeoutMs: 5 * 60 * 1000,
      });
      console.log(name, r.originalSize, '->', r.compressedSize);
    } catch (e) {
      console.error(name, 'failed:', e.code ?? e.message);
    }
  }
}
await Promise.all(Array.from({ length: CONCURRENCY }, worker));
```

:::

In Node.js, each `compress` and `verifyPassword` call runs on its own thread, outside Node's built-in thread pool, so parallel calls do not wait on each other or block your app's file reads. CPU cores are the real limit.

## 3. Size the HTTP service

The HTTP service runs at most `HYPER_CONCURRENCY` jobs at once and lets up to `HYPER_QUEUE` more wait. Any request beyond that gets `429`.

| Variable | Default | Range | What it controls |
|---|---|---|---|
| `HYPER_CONCURRENCY` | 2 | 1 to 1024 | jobs that run at once |
| `HYPER_QUEUE` | 8 | 0 to 100,000 | jobs that may wait; 0 means none wait |
| `HYPER_MAX_UPLOAD_MB` | 500 | from 1 | largest accepted upload |
| `HYPER_TIMEOUT_MS` | 600000 | 1 second to 24 hours | time limit for one job |
| `HYPER_API_TOKEN` | not set | | token required on `/api/*` when set |

```bash
docker run -d --name hyper -p 8080:8080 \
  -e HYPER_CONCURRENCY=4 \
  -e HYPER_QUEUE=32 \
  -e HYPER_MAX_UPLOAD_MB=200 \
  -e HYPER_TIMEOUT_MS=300000 \
  -e HYPER_API_TOKEN=change-me \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

On start the server prints one line:

```text
hyper-compress server on http://0.0.0.0:8080 (concurrency 4, max upload 200MB)
```

Start with `HYPER_CONCURRENCY` at 2 on a small server and 4 to 8 on a large one, then watch memory under real traffic. Each running job is a separate engine process with up to 8 threads of its own.

A queued request waits before its upload is read, so a waiting client's upload is paused, not held in the server's memory.

### Handle 429 with backoff

When the queue is full the service answers `429` with `{"error":"busy","detail":"queue full, retry later"}`. It also sends `Retry-After: 1`. Wait at least that long, and grow the delay on each retry so many clients do not all come back at once.

```js
import { readFile, writeFile } from 'node:fs/promises';

async function compressOverHttp(file, attempt = 0) {
  const res = await fetch('http://localhost:8080/api/compress?preset=medium', {
    method: 'POST',
    headers: { Authorization: 'Bearer change-me' },
    body: await readFile(file),
  });
  if (res.status === 429 && attempt < 5) {
    const after = Number(res.headers.get('retry-after') ?? 1);
    const delay = after * 1000 * 2 ** attempt + Math.random() * 250;
    await new Promise((resolve) => setTimeout(resolve, delay));
    return compressOverHttp(file, attempt + 1);
  }
  if (!res.ok) throw new Error(`${res.status} ${(await res.json()).error}`);
  return Buffer.from(await res.arrayBuffer());
}

await writeFile('output.pdf', await compressOverHttp('input.pdf'));
```

### Watch the queue

`GET /healthz` needs no token and returns the live numbers:

```bash
curl -s http://localhost:8080/healthz
```

```json
{"ok":true,"active":2,"queued":5}
```

`active` is jobs running, `queued` is jobs waiting. Use it as the health check for your load balancer. When `queued` stays close to `HYPER_QUEUE`, add capacity.

### Scale out

The service keeps nothing between requests: no sessions, no files, no database. To handle more traffic, run more containers behind a load balancer; any request can go to any container.

## 4. Know what speed to expect

In the [commercial SDK benchmark](/benchmarks), Hyper compressed 1,000 real PDFs (3.13 GB in total) 4 at a time on an Apple M4 laptop with 16 GB of memory. Times include starting the process for each file.

| Preset | Median time per file | Whole 1,000 files |
|---|---|---|
| `lossless` | 0.15 s | 8.0 minutes |
| `low` | 0.21 s | 14.5 minutes |
| `medium` | 0.25 s | 17.0 minutes |
| `high` | 0.33 s | 20.7 minutes |

Your files and hardware will give different numbers. Large scans, `speed: 'quality'`, a size target (several engine passes) and a report (two extra read passes) all take longer. Time a sample of your own files with your own settings before you size anything.

## 5. Plan memory and disk

- **Memory** grows with the largest images in a document, not with page count. Leave headroom for large scans and posters. Rasterizing skips any page that would be over 256 million pixels.
- **Input size.** The native engine handles inputs up to 2 GB. The WebAssembly SDK holds the input, its working memory and the output in a 4 GB address space, so use a native surface for files in the hundreds of megabytes.
- **Temporary files.** Each job writes its working files (the input copy over HTTP, the decrypted file, the engine output, the repacked file and any size target attempts) to a new folder in the system temporary directory and deletes the folder when the job ends. Allow a few times the largest input per running job. The Node.js SDK and the Rust crate use the folder in `TMPDIR` (or your platform's equivalent).

::: warning
The repository's `docker-compose.yml` mounts `/tmp` as `tmpfs`, which lives in memory. With that setup, temporary files count against the container's memory, so add a few times your largest upload per running job to its memory budget, or remove the `tmpfs` line.
:::

## 6. Accept untrusted uploads safely

Files from the public internet can be broken or built to attack PDF software. Hyper has several layers of protection; turn on the ones that are yours to set.

| Protection | Where | What you do |
|---|---|---|
| Each stage runs in a separate engine process, so a crash or hang ends that process, not your service | CLI, Node.js, HTTP, Rust | nothing; it is always on |
| Time limits stop runaway jobs with `timeout` | each stage has a 10 minute limit by default; Node.js `timeoutMs` and `totalTimeoutMs`, HTTP `HYPER_TIMEOUT_MS` | set limits that fit your largest legitimate files |
| An upload limit refuses huge bodies with `413` | HTTP `HYPER_MAX_UPLOAD_MB` | set it to your real maximum |
| Input that is not a PDF is refused: `400 not_a_pdf` over HTTP, `engine_error` elsewhere | every surface | nothing |
| A token keeps strangers off the API | HTTP `HYPER_API_TOKEN` | set it, and send it as `Authorization: Bearer <token>` or `x-api-token` |
| TLS and rate limiting | a reverse proxy in front of the container | the container speaks plain HTTP; terminate TLS in Caddy, nginx or Traefik |
| The container runs as an unprivileged user (`hyper`) | Docker image | nothing |
| The engine is fuzzed with ClusterFuzzLite on pull requests that touch it and nightly | the project | nothing |

The WebAssembly SDK and the C API run the engine inside your process. For untrusted files there, run WebAssembly in a worker thread and terminate it if it takes too long, and from C run the `hpdf-worker` program instead of linking the library. See [WebAssembly](/wasm) and the [C API](/c-api).

## 7. Keep logs free of documents

The HTTP service prints only its start-up line. It logs nothing about requests or documents: no file names, sizes, passwords or content. The Node.js SDK and the CLI print nothing except, for the CLI, the result and warning lines you ask for. If you need an audit trail, log in your own code from the result: `originalSize`, `compressedSize`, `signed`, `metTarget` and `warnings`.

## Troubleshooting

**Requests get 429 at busy times.** Retry with backoff, raise `HYPER_QUEUE` to absorb bursts, raise `HYPER_CONCURRENCY` if the server has spare cores and memory, or add containers.

**Jobs get 504.** They ran past `HYPER_TIMEOUT_MS`. Raise it, or ask for lighter settings (no size target, `speed: 'fast'`).

**The container is killed for using too much memory.** Lower `HYPER_CONCURRENCY`, give the container more memory, or move `/tmp` off `tmpfs`.

**Disk fills up during large batches.** Point `TMPDIR` at a larger disk, and allow a few times the largest input per running job.

## Next steps

- [Self-hosting](/self-hosting): run, configure and update the Docker image.
- [HTTP API](/http-api): every endpoint, header and error.
- [Errors, timeouts and cancellation](/guides/errors-and-cancellation): handle failures in batch jobs.
- [Benchmarks](/benchmarks): how the speed and size numbers were measured.
- [Platforms](/platforms): what each surface supports.
