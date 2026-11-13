"""Turn a results folder (results/<run>/<machine>/c<N>/summary.json) into a Markdown report.

Optional prices: a JSON file mapping machine type to USD per hour, passed as --prices, adds cost per 1,000 documents.
"""
import argparse
import json
from pathlib import Path


def fmt_s(v):
    return "" if v is None else f"{v:.2f} s"


def gb(v):
    return "" if v is None else f"{v / 1e9:.1f} GB"


SIZES = [("under 100 KB", 0, 1e5), ("100 to 500 KB", 1e5, 5e5), ("0.5 to 2 MB", 5e5, 2e6), ("2 to 5 MB", 2e6, 5e6),
         ("5 to 10 MB", 5e6, 1e7), ("10 to 100 MB", 1e7, 1e8), ("over 100 MB", 1e8, float("inf")),
         ("all under 5 MB", 0, 5e6), ("all under 10 MB", 0, 1e7)]


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(p / 100 * len(values)))]


def only_this_kind(rows, label, n):
    secs = [r["seconds"] for r in rows]
    if not secs:
        return f"| {label} | 0 | | | | |"
    rate = n * 3600 / (sum(secs) / len(secs))
    saved = 100 - 100 * sum(r["out_bytes"] for r in rows) / sum(r["bytes"] for r in rows)
    return f"| {label} | {len(rows):,} | {rate:,.0f} | {fmt_s(pct(secs, 50))} | {fmt_s(pct(secs, 95))} | {saved:.0f}% |"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results")
    ap.add_argument("--prices")
    args = ap.parse_args()
    prices = json.load(open(args.prices)) if args.prices else {}
    root = Path(args.results)
    machines = sorted(p for p in root.iterdir() if p.is_dir())
    out = [f"# Hyper Compress throughput: {root.name}", ""]
    best_rows = []
    for m in machines:
        info = json.loads((m / "machine.json").read_text()) if (m / "machine.json").exists() else {}
        version = (m / "engine-version.txt").read_text().strip() if (m / "engine-version.txt").exists() else ""
        levels = sorted((json.loads(p.read_text()) for p in m.glob("c*/summary.json")), key=lambda s: s["concurrency"])
        if not levels:
            continue
        out += [f"## {m.name}", "",
                f"{info.get('vcpus', '?')} vCPUs, {info.get('arch', '?')}, {info.get('cpu', '')}, "
                f"{gb(info.get('memory_bytes'))} memory, Hyper {version}, preset `{levels[0]['preset']}`, "
                f"{levels[0]['corpus_files']} PDFs, {int(levels[0]['duration_seconds'])} s per level after "
                f"{int(levels[0]['warmup_seconds'])} s warm-up.", "",
                "| Jobs at once | Documents/hour | Pages/hour | p50 | p95 | p99 | CPU busy | Engine memory peak | Largest job | Errors |",
                "|---|---|---|---|---|---|---|---|---|---|"]
        for s in levels:
            o = s["overall"]
            cpu = "" if s["cpu_busy_avg"] is None else f"{s['cpu_busy_avg'] * 100:.0f}%"
            problems = sum(s["errors"].values()) + s["invalid_outputs"]
            out.append(f"| {s['concurrency']} | {o['documents_per_hour']:,} | {o['pages_per_hour']:,} | "
                       f"{fmt_s(o['p50_seconds'])} | {fmt_s(o['p95_seconds'])} | {fmt_s(o['p99_seconds'])} | "
                       f"{cpu} | {gb(s['engine_rss_peak_bytes'])} | {gb(s['largest_job_rss_bytes'])} | {problems} |")
        best = max(levels, key=lambda s: s["overall"]["documents_per_hour"])
        out += ["", f"Best: {best['concurrency']} jobs at once.", "", "By group at the best level:", "",
                "| Group | Documents/hour | p50 | p95 | Saved |", "|---|---|---|---|---|"]
        for g, o in best["by_group"].items():
            out.append(f"| {g} | {o['documents_per_hour']:,} | {fmt_s(o['p50_seconds'])} | {fmt_s(o['p95_seconds'])} | {o['saved_percent']}% |")
        out += ["", "By file size at the best level:", "", "| Size | Documents/hour | p50 | p95 |", "|---|---|---|---|"]
        for b, o in best["by_band"].items():
            out.append(f"| {b} | {o['documents_per_hour']:,} | {fmt_s(o['p50_seconds'])} | {fmt_s(o['p95_seconds'])} |")
        req = m / f"c{best['concurrency']}" / "requests.jsonl"
        if req.exists():
            good = [r for r in map(json.loads, req.open()) if r["status"] == 200 and r["valid"]]
            n = best["concurrency"]
            out += ["", f"If every file were of one kind ({n} jobs at once, from every finished request of that level; "
                    "documents/hour = jobs at once x 3,600 / average seconds per file):", "",
                    "| Files | Measured | Documents/hour | p50 | p95 | Saved |", "|---|---|---|---|---|---|"]
            out += [only_this_kind([r for r in good if lo <= r["bytes"] < hi], label, n) for label, lo, hi in SIZES]
            out += [only_this_kind([r for r in good if r["group"] == g], g, n) for g in sorted({r["group"] for r in good})]
        out.append("")
        price = prices.get(m.name)
        dph = best["overall"]["documents_per_hour"]
        best_rows.append((m.name, info.get("vcpus"), info.get("arch"), best["concurrency"], dph,
                          best["overall"]["p95_seconds"], price, price * 1000 / dph if price and dph else None))
    out = out[:2] + ["## Summary", "",
                     "| Machine | vCPUs | CPU | Best jobs at once | Documents/hour | Documents/hour per vCPU | p95 | USD/hour | USD per 1,000 documents |",
                     "|---|---|---|---|---|---|---|---|---|"] + [
        f"| {n} | {v} | {a} | {c} | {d:,} | {round(d / v) if v else ''} | {fmt_s(p)} | "
        f"{'' if pr is None else f'{pr:.3f}'} | {'' if k is None else f'{k:.4f}'} |"
        for n, v, a, c, d, p, pr, k in best_rows] + [""] + out[2:]
    print("\n".join(out))


if __name__ == "__main__":
    main()
