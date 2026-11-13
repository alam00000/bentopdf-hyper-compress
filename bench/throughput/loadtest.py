"""Closed-loop load test against a running Hyper Compress HTTP service.

`--concurrency` workers each send one PDF, wait for the answer, and send the next, so the service always has
exactly that many jobs in flight. Files are taken in a fixed shuffled order (seed 20261113) and cycled, so every
run sees the same mix. Only requests that finish inside the measurement window (after the warm-up) are counted.

Writes <out>/c<N>/requests.jsonl (one line per request), samples.jsonl (CPU and memory every second) and
summary.json. Standard library only.
"""
import argparse
import csv
import json
import os
import random
import statistics
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

ENGINE_PROCESSES = {"hpdf-worker", "qpdf", "hyper-server"}


def percentile(values, p):
    if not values:
        return None
    s = sorted(values)
    k = (len(s) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def cpu_times():
    with open("/proc/stat") as f:
        parts = [int(x) for x in f.readline().split()[1:]]
    idle = parts[3] + parts[4]
    return sum(parts), idle


def engine_memory():
    total, peak = 0, 0
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/comm") as f:
                name = f.read().strip()
            if name not in ENGINE_PROCESSES:
                continue
            with open(f"/proc/{pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        kb = int(line.split()[1])
                        total += kb
                        if name != "hyper-server":
                            peak = max(peak, kb)
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return total * 1024, peak * 1024


def sampler(stop, out, interval=1.0):
    if not os.path.exists("/proc/stat"):
        Path(out).write_text("")
        return
    prev = cpu_times()
    with open(out, "w") as f:
        while not stop.wait(interval):
            cur = cpu_times()
            busy = 1 - (cur[1] - prev[1]) / max(1, cur[0] - prev[0])
            prev = cur
            mem_total, mem_peak_job = engine_memory()
            f.write(json.dumps({"t": time.time(), "cpu": round(busy, 4), "engine_rss": mem_total,
                                "largest_job_rss": mem_peak_job}) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="http://127.0.0.1:8080")
    ap.add_argument("--corpus", required=True, help="folder of <sha256>.pdf files")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--concurrency", type=int, required=True)
    ap.add_argument("--duration", type=float, default=600)
    ap.add_argument("--warmup", type=float, default=60)
    ap.add_argument("--preset", default="medium")
    ap.add_argument("--timeout", type=float, default=3600)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    files = []
    for r in csv.DictReader(open(args.manifest)):
        p = Path(args.corpus) / f"{r['sha256']}.pdf"
        if p.is_file():
            files.append({"path": p, "sha256": r["sha256"], "group": r["group"], "band": r["band"],
                          "bytes": int(r["bytes"]), "pages": int(r["pages"] or 0)})
    if not files:
        raise SystemExit("no corpus files found")
    random.Random(20261113).shuffle(files)

    out = Path(args.out) / f"c{args.concurrency}"
    out.mkdir(parents=True, exist_ok=True)
    url = f"{args.server}/api/compress?preset={args.preset}"
    start = time.time()
    window_start = start + args.warmup
    window_end = window_start + args.duration
    lock = threading.Lock()
    cursor = [0]
    records = []

    def next_file():
        with lock:
            f = files[cursor[0] % len(files)]
            cursor[0] += 1
            return f

    def worker():
        while time.time() < window_end:
            f = next_file()
            body = f["path"].read_bytes()
            req = urllib.request.Request(url, data=body, method="POST",
                                         headers={"Content-Type": "application/pdf"})
            t0 = time.time()
            status, out_bytes, err, ok = 0, 0, "", False
            try:
                with urllib.request.urlopen(req, timeout=args.timeout) as resp:
                    data = resp.read()
                    status = resp.status
                    out_bytes = len(data)
                    ok = data[:5] == b"%PDF-" and out_bytes <= len(body)
            except urllib.error.HTTPError as e:
                status = e.code
                try:
                    err = json.loads(e.read() or b"{}").get("error", "")
                except ValueError:
                    err = "unreadable"
            except Exception as e:
                err = type(e).__name__
            t1 = time.time()
            rec = {"start": t0, "end": t1, "seconds": round(t1 - t0, 4), "sha256": f["sha256"],
                   "group": f["group"], "band": f["band"], "bytes": f["bytes"], "pages": f["pages"],
                   "status": status, "out_bytes": out_bytes, "valid": ok, "error": err}
            with lock:
                records.append(rec)

    stop = threading.Event()
    sam = threading.Thread(target=sampler, args=(stop, out / "samples.jsonl"), daemon=True)
    sam.start()
    threads = [threading.Thread(target=worker) for _ in range(args.concurrency)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    stop.set()
    sam.join()

    with open(out / "requests.jsonl", "w") as fh:
        for r in records:
            fh.write(json.dumps(r) + "\n")

    counted = [r for r in records if window_start <= r["end"] <= window_end]
    good = [r for r in counted if r["status"] == 200 and r["valid"]]
    samples = [json.loads(l) for l in open(out / "samples.jsonl")]
    window_samples = [s for s in samples if window_start <= s["t"] <= window_end]
    hours = args.duration / 3600

    def stats(rs):
        secs = [r["seconds"] for r in rs]
        return {
            "documents": len(rs),
            "documents_per_hour": round(len(rs) / hours),
            "pages_per_hour": round(sum(r["pages"] for r in rs) / hours),
            "input_gb_per_hour": round(sum(r["bytes"] for r in rs) / 1e9 / hours, 2),
            "p50_seconds": percentile(secs, 50),
            "p95_seconds": percentile(secs, 95),
            "p99_seconds": percentile(secs, 99),
            "saved_percent": round(100 * (1 - sum(r["out_bytes"] for r in rs) / max(1, sum(r["bytes"] for r in rs))), 1),
        }

    summary = {
        "concurrency": args.concurrency,
        "preset": args.preset,
        "duration_seconds": args.duration,
        "warmup_seconds": args.warmup,
        "corpus_files": len(files),
        "vcpus": os.cpu_count(),
        "overall": stats(good),
        "errors": {},
        "invalid_outputs": sum(1 for r in counted if r["status"] == 200 and not r["valid"]),
        "cpu_busy_avg": round(statistics.mean(s["cpu"] for s in window_samples), 3) if window_samples else None,
        "engine_rss_peak_bytes": max((s["engine_rss"] for s in window_samples), default=None),
        "largest_job_rss_bytes": max((s["largest_job_rss"] for s in window_samples), default=None),
        "by_group": {g: stats([r for r in good if r["group"] == g]) for g in sorted({r["group"] for r in good})},
        "by_band": {b: stats([r for r in good if r["band"] == b]) for b in sorted({r["band"] for r in good})},
    }
    for r in counted:
        if r["status"] != 200:
            key = f"{r['status']} {r['error']}".strip()
            summary["errors"][key] = summary["errors"].get(key, 0) + 1
    (out / "summary.json").write_text(json.dumps(summary, indent=2))
    o = summary["overall"]
    print(f"c{args.concurrency}: {o['documents_per_hour']} docs/h, p50 {o['p50_seconds']} s, "
          f"p95 {o['p95_seconds']} s, cpu {summary['cpu_busy_avg']}, errors {sum(summary['errors'].values())}, "
          f"invalid {summary['invalid_outputs']}")


if __name__ == "__main__":
    main()
