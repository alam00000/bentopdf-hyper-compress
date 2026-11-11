import csv
import os
import statistics as st
import sys
from collections import defaultdict

H = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(H, 'RESULTS.csv')
NAMES = {'hyper': 'Hyper', 'pdftools': 'PDF Tools', 'apryse': 'Apryse', 'nutrient': 'Nutrient'}
LEVELS = [('lossless', 'Lossless'), ('low', 'Low (200 dpi)'), ('medium', 'Medium (150 dpi)'), ('high', 'Highest (72 dpi)')]
SIZES = [(0, 100e3, '< 100 KB'), (100e3, 500e3, '100–500 KB'), (500e3, 2e6, '0.5–2 MB'), (2e6, 10e6, '2–10 MB'), (10e6, 1e15, '> 10 MB')]

R = defaultdict(dict)
for r in csv.DictReader(open(src)):
    ok = r['ok'] == '1'
    R[(r['engine'], r['level'])][r['file']] = dict(
        i=int(r['input_bytes']), o=int(r['output_bytes']) if ok else int(r['input_bytes']), ok=ok,
        t=float(r['seconds'] or 600), q=[float(r[k]) for k in ('quality_page1', 'quality_page2') if ok and r[k]])
engines = [e for e in NAMES if any(k[0] == e for k in R)]


def row(engine, cells):
    return '| ' + ' | '.join(f'**{c}**' if engine == 'hyper' else c for c in cells) + ' |'


out = []
for level, title in LEVELS:
    out += [f'### {title}', '',
            '| Engine | Output | Saved overall | Median saving per file | Failed | Files made bigger | Total time | Median time per file | Avg page quality (SSIMULACRA2) | Pages < 50 | Pages < 70 |',
            '|---|---|---|---|---|---|---|---|---|---|---|']
    for e in engines:
        d = R[(e, level)].values()
        if not d:
            out.append(row(e, [NAMES[e]] + ['N/A'] * 10))
            continue
        I, O = sum(v['i'] for v in d), sum(v['o'] for v in d)
        ok = [v for v in d if v['ok']]
        qs = [q for v in ok for q in v['q']]
        ts = [v['t'] for v in d]
        out.append(row(e, [NAMES[e], f'{O / 1e9:.2f} GB', f'{100 * (1 - O / I):.1f}%',
                           f'{st.median(100 * (1 - v["o"] / v["i"]) for v in ok):.1f}%',
                           str(len(d) - len(ok)), str(sum(v['o'] > v['i'] for v in ok)),
                           f'{sum(ts) / 60:.1f} min', f'{st.median(ts):.2f} s', f'{st.mean(qs):.1f}',
                           f'{100 * sum(q < 50 for q in qs) / len(qs):.1f}%', f'{100 * sum(q < 70 for q in qs) / len(qs):.1f}%']))
    out.append('')
    h = R[('hyper', level)]
    for e in engines[1:]:
        d = R[(e, level)]
        if not d:
            continue
        both = [f for f in h if f in d and h[f]['ok'] and d[f]['ok']]
        a = sum(h[f]['o'] < d[f]['o'] * 0.99 for f in both)
        b = sum(d[f]['o'] < h[f]['o'] * 0.99 for f in both)
        out.append(f'- Hyper vs {NAMES[e]}: Hyper smaller on **{a}** files, {NAMES[e]} smaller on {b}, '
                   f'within 1% on {len(both) - a - b} (of {len(both)} both handled).')
    out.append('')

out += ['### Saved overall by input size', '', '| Level | Engine | ' + ' | '.join(s[2] for s in SIZES) + ' |',
        '|---|---|' + '---|' * len(SIZES)]
for level, title in LEVELS:
    for e in engines:
        cells = []
        for lo, hi, _ in SIZES:
            sub = [v for v in R[(e, level)].values() if lo <= v['i'] < hi]
            I, O = sum(v['i'] for v in sub), sum(v['o'] for v in sub)
            cells.append(f'{100 * (1 - O / I):.1f}%' if I else 'N/A' if not R[(e, level)] else '–')
        out.append(row(e, [title.split(' (')[0], NAMES[e]] + cells))
print('\n'.join(out))
