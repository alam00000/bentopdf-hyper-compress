"""Draw the report charts as SVG files from a results folder.

  python3 bench/throughput/charts.py bench/throughput/results/2026-11-13 --prices bench/throughput/prices.json \
    --price-note "At Google Cloud on-demand list prices, November 2026"

Writes <results>/charts/throughput.svg, cost.svg and by-size.svg. Light and dark colours follow the reader's setting.
"""
import argparse
import json
import math
from html import escape
from pathlib import Path

from report import SIZES

STYLE = """<style>
svg{--surface:#fcfcfb;--text:#0b0b0b;--muted:#52514e;--grid:#e6e5e0;--arm:#2a78d6;--intel:#eb6834;
font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif}
@media (prefers-color-scheme:dark){svg{--surface:#1a1a19;--text:#f0efec;--muted:#c3c2b7;--grid:#383835;
--arm:#3987e5;--intel:#d95926}}
.bg{fill:var(--surface)}.t{fill:var(--text)}.m{fill:var(--muted)}.g{stroke:var(--grid);stroke-width:1}
.arm{fill:var(--arm)}.intel{fill:var(--intel)}
</style>"""

LABEL_W, BAR_W, RIGHT, BAR_H, GAP, GROUP_GAP = 210, 470, 120, 20, 2, 18


INFO = {}


def family(machine):
    return "arm" if INFO[machine]["arch"] in ("aarch64", "arm64") else "intel"


def nice(machine):
    return f"{INFO[machine]['vcpus']}-CPU {'Arm' if family(machine) == 'arm' else 'Intel'} server"


def bar(x, y, w, h, cls, tip):
    w = max(w, 2)
    r = min(4, w / 2)
    d = f"M{x},{y}h{w - r}a{r},{r} 0 0 1 {r},{r}v{h - 2 * r}a{r},{r} 0 0 1 -{r},{r}h-{w - r}z"
    return f'<path class="{cls}" d="{d}"><title>{escape(tip)}</title></path>'


def legend(y):
    return (f'<rect class="arm" x="{LABEL_W}" y="{y}" width="12" height="12" rx="2"/>'
            f'<text class="m" x="{LABEL_W + 18}" y="{y + 10}" font-size="13">Arm</text>'
            f'<rect class="intel" x="{LABEL_W + 70}" y="{y}" width="12" height="12" rx="2"/>'
            f'<text class="m" x="{LABEL_W + 88}" y="{y + 10}" font-size="13">Intel</text>')


def chart(title, subtitle, groups, fmt, log=False, ticks=None, value_fmt=None):
    """groups: [(label, [(cls, value, tip), ...]), ...]"""
    top = 92
    height = top + sum(len(b) * (BAR_H + GAP) - GAP + GROUP_GAP for _, b in groups) + 34
    width = LABEL_W + BAR_W + RIGHT
    vmax = max(v for _, b in groups for _, v, _ in b)
    lo = ticks[0] if log else 0
    hi = ticks[-1] if log else vmax

    def sx(v):
        if log:
            return BAR_W * (math.log10(max(v, lo)) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))
        return BAR_W * v / hi

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}" '
           f'role="img" aria-label="{escape(title)}">', STYLE,
           f'<rect class="bg" width="{width}" height="{height}" rx="8"/>',
           f'<text class="t" x="20" y="32" font-size="18" font-weight="600">{escape(title)}</text>',
           f'<text class="m" x="20" y="54" font-size="13">{escape(subtitle)}</text>', legend(66)]
    plot_bottom = height - 34
    for t in ticks or []:
        x = LABEL_W + sx(t)
        out.append(f'<line class="g" x1="{x}" x2="{x}" y1="{top - 6}" y2="{plot_bottom}"/>'
                   f'<text class="m" x="{x}" y="{plot_bottom + 18}" font-size="12" text-anchor="middle">{fmt(t)}</text>')
    out.append(f'<line class="g" x1="{LABEL_W}" x2="{LABEL_W}" y1="{top - 6}" y2="{plot_bottom}"/>')
    y = top
    for label, bars in groups:
        block = len(bars) * (BAR_H + GAP) - GAP
        out.append(f'<text class="t" x="{LABEL_W - 12}" y="{y + block / 2 + 5}" font-size="13" '
                   f'text-anchor="end">{escape(label)}</text>')
        for cls, v, tip in bars:
            w = sx(v)
            out.append(bar(LABEL_W, y, w, BAR_H, cls, tip))
            out.append(f'<text class="t" x="{LABEL_W + w + 8}" y="{y + BAR_H / 2 + 5}" font-size="13" '
                       f'font-weight="600">{(value_fmt or fmt)(v)}</text>')
            y += BAR_H + GAP
        y += GROUP_GAP - GAP
    out.append("</svg>")
    return "\n".join(out)


def compact(v):
    for div, suffix in ((1e6, "M"), (1e3, "K")):
        if v >= div:
            s = f"{v / div:.1f}".rstrip("0").rstrip(".")
            return s + suffix
    return f"{v:,.0f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results")
    ap.add_argument("--prices")
    ap.add_argument("--price-note", default="At on-demand list prices")
    args = ap.parse_args()
    prices = json.load(open(args.prices)) if args.prices else {}
    root = Path(args.results)
    full = {}
    for m in sorted(p for p in root.iterdir() if p.is_dir() and (p / "machine.json").exists()):
        INFO[m.name] = json.loads((m / "machine.json").read_text())
        vcpus = INFO[m.name]["vcpus"]
        s = m / f"c{vcpus}" / "summary.json"
        if s.exists():
            full[m.name] = (vcpus, json.loads(s.read_text()))
    first = next(iter(full.values()))[1]
    version = next((p.read_text().split()[-1] for p in root.glob("*/engine-version.txt")), "")
    order = sorted(full, key=lambda n: (full[n][0], family(n) != "arm"))
    (root / "charts").mkdir(exist_ok=True)

    groups = [(nice(n), [(family(n), full[n][1]["overall"]["documents_per_hour"],
                          f"{n}: {full[n][1]['overall']['documents_per_hour']:,} PDFs per hour")]) for n in order]
    top = max(b[0][1] for _, b in groups)
    step = 10 ** math.floor(math.log10(top))
    ticks = [i * step for i in range(0, math.ceil(top / step) + 1)]
    (root / "charts" / "throughput.svg").write_text(chart(
        "PDFs compressed per hour, every core busy",
        f"{first['corpus_files']:,} real public PDFs, Hyper Compress {version}, {first['preset']} preset",
        groups, compact, ticks=ticks, value_fmt=lambda v: f"{v:,.0f}"))

    priced = [n for n in order if prices.get(n)]
    groups = [(nice(n), [(family(n), prices[n] * 1000 / full[n][1]["overall"]["documents_per_hour"],
                          f"{n}: US${prices[n]:.3f} per hour")]) for n in priced]
    top = max(b[0][1] for _, b in groups)
    ticks = [i / 100 for i in range(0, math.ceil(top * 100) + 1)]
    (root / "charts" / "cost.svg").write_text(chart(
        "Cost per 1,000 PDFs (US$, lower is better)",
        args.price_note,
        groups, lambda v: f"${v:.3f}".rstrip("0").rstrip(".") if v else "$0", ticks=ticks))

    pair = [n for n in order if full[n][0] == 8]
    rows = {}
    for n in pair:
        good = [r for r in map(json.loads, (root / n / "c8" / "requests.jsonl").open())
                if r["status"] == 200 and r["valid"]]
        for label, lo, hi in SIZES:
            secs = [r["seconds"] for r in good if lo <= r["bytes"] < hi]
            if secs:
                rows.setdefault(label, []).append(
                    (family(n), 8 * 3600 / (sum(secs) / len(secs)),
                     f"{nice(n)}, {label}: {len(secs)} files measured"))
    (root / "charts" / "by-size.svg").write_text(chart(
        "PDFs per hour on one 8-CPU server, by file size",
        "If every file were this size. Log scale: each grid line is 10 times the one before",
        list(rows.items()), compact, log=True, ticks=[10, 100, 1000, 10000, 100000, 1000000]))


if __name__ == "__main__":
    main()
