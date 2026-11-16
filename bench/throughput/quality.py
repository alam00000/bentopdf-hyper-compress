import argparse
import concurrent.futures as cf
import csv
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from collections import Counter
import urllib.request
from pathlib import Path

CHUNK = 20
DPI = 100
MAX_SIDE = 2000
BROKEN_SSIM = 0.7
LOOK_SS2 = 30


def run(cmd, timeout=900):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, b"", b"timeout"


def manifest(path):
    return list(csv.DictReader(open(path)))



def compress(args):
    rows = sorted(manifest(args.manifest), key=lambda r: -int(r["bytes"]))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    log = open(out / "compress.jsonl", "a")
    lock = threading.Lock()

    def one(r):
        dest = out / f"{r['sha256']}.pdf"
        if dest.exists():
            return
        body = (Path(args.corpus) / f"{r['sha256']}.pdf").read_bytes()
        req = urllib.request.Request(f"{args.server}/api/compress?{args.query}", data=body, method="POST")
        t0, status, err = time.time(), 0, ""
        try:
            with urllib.request.urlopen(req, timeout=3600) as resp:
                data, status = resp.read(), resp.status
            dest.write_bytes(data)
        except urllib.error.HTTPError as e:
            status, err = e.code, e.read()[:300].decode("utf-8", "replace")
        except Exception as e:
            err = type(e).__name__
        with lock:
            log.write(json.dumps({"sha256": r["sha256"], "status": status, "error": err,
                                  "seconds": round(time.time() - t0, 3)}) + "\n")
            log.flush()

    with cf.ThreadPoolExecutor(args.jobs) as ex:
        list(ex.map(one, rows))



def page_info(pdf):
    code, out, _ = run(["pdfinfo", "-f", "1", "-l", "1000000", str(pdf)])
    if code != 0:
        return None
    sizes = re.findall(rb"Page\s+\d+ size:\s+([\d.]+) x ([\d.]+)", out)
    rots = re.findall(rb"Page\s+\d+ rot:\s+(\d+)", out)
    return [(round(float(w), 1), round(float(h), 1), int(r)) for (w, h), r in zip(sizes, rots)]


def mutool_errors(pdf):
    code, _, err = run(["mutool", "draw", "-o", "/dev/null", "-r", "10", str(pdf)])
    return code != 0 or bool(re.search(rb"error", err, re.I))


def qpdf_bad(pdf):
    return run(["qpdf", "--check", str(pdf)])[0] == 2


def structure(pdf):
    code, out, _ = run(["qpdf", "--json=2", "--json-key=acroform", "--json-key=pages", "--json-key=outlines",
                        "--json-key=attachments", str(pdf)])
    if code not in (0, 3):
        return None
    try:
        j = json.loads(out)
    except ValueError:
        return None

    def count(outlines):
        return sum(1 + count(o.get("kids", [])) for o in outlines)

    fields = sorted((f.get("fullname", ""), f.get("fieldtype", ""), json.dumps(f.get("value")), f.get("pageposfrom1"))
                    for f in j.get("acroform", {}).get("fields", []))
    return {"fields": fields,
            "annots": sum(len(p.get("annotations", [])) for p in j.get("pages", [])),
            "outlines": count(j.get("outlines", [])),
            "attachments": len(j.get("attachments", {}))}


def text(pdf):
    _, out, _ = run(["pdftotext", "-q", str(pdf), "-"])
    return re.sub(r"\s+", " ", out.decode("utf-8", "replace")).strip()


def dice(a, b):
    def grams(s):
        g = {}
        for i in range(min(len(s), 200_000) - 1):
            g[s[i:i + 2]] = g.get(s[i:i + 2], 0) + 1
        return g
    A, B = grams(a), grams(b)
    inter = sum(min(v, A.get(k, 0)) for k, v in B.items())
    return 2 * inter / ((sum(A.values()) + sum(B.values())) or 1)


def pdfa_claim(pdf):
    _, out, _ = run(["pdfinfo", "-meta", str(pdf)])
    return re.search(rb"pdfaid:part", out, re.I) is not None


def valid_sigs(pdf):
    _, out, _ = run(["pdfsig", str(pdf)])
    return out.count(b"Signature is Valid")


def file_checks(job):
    src, out, group = job
    r = {"in_bytes": src.stat().st_size, "out_bytes": out.stat().st_size}
    sb, ob = src.read_bytes(), out.read_bytes()
    r["identical"] = sb == ob
    r["header_ok"] = ob[:5] == b"%PDF-"
    if r["identical"]:
        if group == "signed" or b"/ByteRange" in sb:
            r["sig_in"] = r["sig_out"] = valid_sigs(src)
        r["pages_in"] = r["pages_out"] = len(page_info(src) or [])
        return r
    pi, po = page_info(src), page_info(out)
    r["pages_in"], r["pages_out"] = len(pi or []), len(po or [])
    r["geometry_ok"] = pi is not None and pi == po
    r["new_qpdf_errors"] = qpdf_bad(out) and not qpdf_bad(src)
    r["new_mutool_errors"] = mutool_errors(out) and not mutool_errors(src)
    si, so = structure(src), structure(out)
    if si and so:
        r["fields_in"], r["fields_ok"] = len(si["fields"]), si["fields"] == so["fields"]
        for k in ("annots", "outlines", "attachments"):
            r[f"{k}_in"], r[f"{k}_out"] = si[k], so[k]
    ti, to = text(src), text(out)
    r["text_chars"] = len(ti)
    r["text_equal"] = ti == to
    r["text_dice"] = 1.0 if ti == to else round(dice(ti, to), 4)
    ca, cb = Counter(ti.replace(" ", "")), Counter(to.replace(" ", ""))
    r["text_chars_diff"] = 0 if ti == to else sum(((ca - cb) + (cb - ca)).values())
    if group == "signed" or b"/ByteRange" in sb:
        r["sig_in"], r["sig_out"] = valid_sigs(src), valid_sigs(out)
    if group == "pdfa":
        r["pdfa_out_claims"] = pdfa_claim(out)
    r["chunks"] = [(i, min(i + CHUNK - 1, len(pi))) for i in range(1, len(pi or []) + 1, CHUNK)] if r["geometry_ok"] else []
    r["max_side_pt"] = max((max(w, h) for w, h, _ in pi), default=0) if pi else 0
    return r



def read_ppm(path):
    import numpy as np
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    pix = np.frombuffer(data[len(data) - w * h * 3:], dtype=np.uint8).reshape(h, w, 3)
    return pix


def ssim(a, b):
    import numpy as np
    if a.shape != b.shape:
        h, w = min(a.shape[0], b.shape[0]), min(a.shape[1], b.shape[1])
        if abs(a.shape[0] - b.shape[0]) > max(2, round(a.shape[0] * 0.01)) or \
           abs(a.shape[1] - b.shape[1]) > max(2, round(a.shape[1] * 0.01)):
            return None
        a, b = a[:h, :w], b[:h, :w]
    k = np.array([0.299, 0.587, 0.114])
    ga, gb = a.astype(np.float64) @ k, b.astype(np.float64) @ k
    ma, mb = ga.mean(), gb.mean()
    va, vb = ga.var(), gb.var()
    cov = ((ga - ma) * (gb - mb)).mean()
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    return float(((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2)))


def page_checks(job):
    sha, src, out, first, last, dpi = job
    d = tempfile.mkdtemp(prefix="hq-")
    try:
        ca, _, ea = run(["pdftoppm", "-r", str(dpi), "-f", str(first), "-l", str(last), str(src), f"{d}/a"])
        cb, _, eb = run(["pdftoppm", "-r", str(dpi), "-f", str(first), "-l", str(last), str(out), f"{d}/b"])
        new_err = (cb != 0 or b"Error" in eb) and not (ca != 0 or b"Error" in ea)
        pages = []
        for name in sorted(os.listdir(d)):
            if not name.startswith("a"):
                continue
            pa, pb = f"{d}/{name}", f"{d}/b{name[1:]}"
            n = int(re.search(r"(\d+)\.ppm$", name).group(1))
            if not os.path.exists(pb):
                pages.append((n, None, None))
                continue
            s = ssim(read_ppm(pa), read_ppm(pb))
            code, so, _ = run(["ssimulacra2", pa, pb], timeout=300)
            s2 = float(so.split()[-1]) if code == 0 and so.split() else None
            pages.append((n, s, s2))
        return sha, new_err, pages
    finally:
        shutil.rmtree(d, ignore_errors=True)



def verapdf(paths):
    if not paths:
        return {}
    code, out, _ = run(["verapdf", "--format", "mrr", "--flavour", "0", *map(str, paths)], timeout=3600)
    res = {str(p): None for p in paths}
    for job in out.split(b"<job>")[1:]:
        name = re.search(rb"<name>([^<]+)</name>", job)
        verdict = re.search(rb'<validationReport[^>]*isCompliant="(true|false)"', job)
        if name and verdict:
            res[name.group(1).decode()] = verdict.group(1) == b"true"
    return res


def pdfa_checks(pairs, jobs):
    paths = [str(Path(p).resolve()) for _, p in pairs]
    batches = [paths[i:i + 10] for i in range(0, len(paths), 10)]
    out = {}
    with cf.ThreadPoolExecutor(jobs) as ex:
        for part in ex.map(verapdf, batches):
            out.update(part)
    return {sha: out.get(str(Path(p).resolve())) for sha, p in pairs}



def check(args):
    rows = {r["sha256"]: r for r in manifest(args.manifest)}
    corpus, outdir = Path(args.corpus), Path(args.out)
    status = {}
    log = outdir / "compress.jsonl"
    if log.exists():
        for line in open(log):
            e = json.loads(line)
            status[e["sha256"]] = e
    jobs, results = [], {}
    for sha, r in rows.items():
        out = outdir / f"{sha}.pdf"
        if not out.exists():
            results[sha] = {"missing_output": True, "http": status.get(sha, {}).get("status"),
                            "error": status.get(sha, {}).get("error", "")}
            continue
        jobs.append((corpus / f"{sha}.pdf", out, r["group"], sha))
    t0 = time.time()
    with cf.ProcessPoolExecutor(args.jobs) as ex:
        for (src, out, group, sha), r in zip(jobs, ex.map(file_checks, [j[:3] for j in jobs], chunksize=4)):
            results[sha] = r
    print(f"file checks: {len(jobs)} files in {time.time() - t0:.0f}s", file=sys.stderr)

    page_jobs = []
    for src, out, group, sha in jobs:
        r = results[sha]
        dpi = max(20, min(DPI, int(MAX_SIDE * 72 / r["max_side_pt"]))) if r.get("max_side_pt") else DPI
        for first, last in r.get("chunks", []):
            page_jobs.append((sha, src, out, first, last, dpi))
    page_jobs.sort(key=lambda j: -(j[4] - j[3]))
    t0 = time.time()
    with cf.ProcessPoolExecutor(args.jobs) as ex:
        for sha, new_err, pages in ex.map(page_checks, page_jobs, chunksize=1):
            r = results[sha]
            r["new_render_errors"] = r.get("new_render_errors", False) or new_err
            r.setdefault("page_scores", []).extend(pages)
    print(f"page checks: {len(page_jobs)} chunks in {time.time() - t0:.0f}s", file=sys.stderr)

    pdfa = [(sha, rows[sha]) for sha in rows if rows[sha]["group"] == "pdfa"]
    if pdfa:
        t0 = time.time()
        src_ok = pdfa_checks([(s, corpus / f"{s}.pdf") for s, _ in pdfa], args.jobs)
        out_ok = pdfa_checks([(s, outdir / f"{s}.pdf") for s, _ in pdfa if (outdir / f"{s}.pdf").exists()], args.jobs)
        pres = {}
        if args.pdfa_preserved:
            p = Path(args.pdfa_preserved)
            pres = pdfa_checks([(s, p / f"{s}.pdf") for s, _ in pdfa if (p / f"{s}.pdf").exists()], args.jobs)
        for s, _ in pdfa:
            results[s].update({"pdfa_in": src_ok.get(s), "pdfa_out": out_ok.get(s), "pdfa_preserved": pres.get(s)})
        print(f"pdf/a checks: {len(pdfa)} files in {time.time() - t0:.0f}s", file=sys.stderr)

    cols = ["sha256", "group", "band", "pages", "verdict", "problems", "http", "in_bytes", "out_bytes", "identical",
            "ssim_min", "ssim_worst_page", "ss2_min", "ss2_median", "text_chars", "text_equal", "text_chars_diff", "text_dice",
            "fields_in", "fields_ok", "annots_in", "annots_out", "outlines_in", "outlines_out", "attachments_in",
            "attachments_out", "sig_in", "sig_out", "pdfa_in", "pdfa_out_claims", "pdfa_out", "pdfa_preserved"]
    with open(args.results, "w", newline="") as f:
        w = csv.DictWriter(f, cols, extrasaction="ignore")
        w.writeheader()
        for sha, r in sorted(results.items()):
            m = rows[sha]
            r.update({"sha256": sha, "group": m["group"], "band": m["band"], "pages": m["pages"],
                      "http": r.get("http", status.get(sha, {}).get("status"))})
            pg = [p for p in r.get("page_scores", []) if p[1] is not None]
            if pg:
                worst = min(pg, key=lambda p: p[1])
                r["ssim_min"], r["ssim_worst_page"] = round(worst[1], 4), worst[0]
            s2 = [p[2] for p in r.get("page_scores", []) if p[2] is not None]
            if s2:
                r["ss2_min"], r["ss2_median"] = round(min(s2), 2), round(statistics.median(s2), 2)
            r["problems"] = ";".join(problems(r))
            r["verdict"] = "fail" if any(not p.startswith("look:") for p in problems(r)) else (
                "look" if r["problems"] else "pass")
            w.writerow(r)


def problems(r):
    p = []
    if r.get("missing_output"):
        return [f"no-output:{r.get('http')}:{r.get('error', '')[:60]}"]
    if not r.get("header_ok", True):
        p.append("not-a-pdf")
    if r["out_bytes"] > r["in_bytes"]:
        p.append("grew")
    if r.get("identical"):
        if r.get("sig_in", 0) != r.get("sig_out", 0):
            p.append("signature")
        return p
    if r.get("pages_in") != r.get("pages_out"):
        p.append("page-count")
    elif not r.get("geometry_ok"):
        p.append("page-size")
    if r.get("new_qpdf_errors"):
        p.append("qpdf-errors")
    if r.get("new_mutool_errors") or r.get("new_render_errors"):
        p.append("reader-errors")
    pg = r.get("page_scores", [])
    if any(s is None for _, s, _ in pg) and r.get("geometry_ok"):
        p.append("page-not-rendered")
    if r.get("ssim_min") is not None and r["ssim_min"] < BROKEN_SSIM:
        p.append("broken-page")
    if r.get("text_chars") and r.get("text_chars_diff"):
        p.append("text-lost" if r.get("text_dice", 1) < 0.6 else "text-changed")
    if r.get("fields_in") and not r.get("fields_ok"):
        p.append("form-fields")
    for k in ("annots", "outlines", "attachments"):
        if r.get(f"{k}_in") is not None and r.get(f"{k}_in") != r.get(f"{k}_out"):
            p.append(f"{k}-count")
    if r.get("sig_in", 0) and r.get("sig_out", 0) < r["sig_in"]:
        p.append("signature")
    if r.get("pdfa_in") is True and r.get("pdfa_out_claims") and r.get("pdfa_out") is False:
        p.append("pdfa-claims-but-fails")
    if r.get("pdfa_in") is True and r.get("pdfa_preserved") is False:
        p.append("pdfa-preserve-failed")
    if r.get("ss2_min") is not None and r["ss2_min"] < LOOK_SS2:
        p.append("look:low-ssimulacra2")
    return p



def report(args):
    rows = list(csv.DictReader(open(args.results)))
    n = len(rows)
    out = [f"# Quality check of the load-test corpus\n", f"{n} files checked.\n"]
    verdicts = {v: sum(r["verdict"] == v for r in rows) for v in ("pass", "look", "fail")}
    out.append(f"Pass {verdicts['pass']}, needs a look {verdicts['look']}, fail {verdicts['fail']}.\n")
    pa = [r for r in rows if r["group"] == "pdfa"]
    if pa:
        out.append(f"PDF/A: {sum(r['pdfa_in'] == 'True' for r in pa)} of {len(pa)} PDF/A files passed veraPDF before "
                   f"compression; only those count in the PDF/A rows.\n")
    out.append("| Check | Files checked | Failed |\n|---|---|---|")
    checks = [
        ("Output produced", lambda r: True, lambda p: p.startswith("no-output")),
        ("Never bigger", lambda r: True, lambda p: p == "grew"),
        ("Opens without new errors (qpdf, poppler, MuPDF)", lambda r: r["identical"] == "False",
         lambda p: p in ("qpdf-errors", "reader-errors", "not-a-pdf", "page-not-rendered")),
        ("Same pages and page sizes", lambda r: r["identical"] == "False", lambda p: p in ("page-count", "page-size")),
        (f"Looks the same (worst page SSIM >= {BROKEN_SSIM})", lambda r: r["ssim_min"] != "", lambda p: p == "broken-page"),
        ("Same text", lambda r: r["text_chars"] not in ("", "0"), lambda p: p.startswith("text-")),
        ("Same form fields", lambda r: r["fields_in"] not in ("", "0"), lambda p: p == "form-fields"),
        ("Same links and annotations, bookmarks, attachments", lambda r: r["annots_in"] != "",
         lambda p: p.endswith("-count")),
        ("Signatures still valid", lambda r: r["sig_in"] not in ("", "0"), lambda p: p == "signature"),
        ("PDF/A: valid input never comes back claiming PDF/A while failing", lambda r: r["pdfa_in"] == "True",
         lambda p: p == "pdfa-claims-but-fails"),
        ("PDF/A with preservePdfa: valid input stays valid", lambda r: r["pdfa_in"] == "True" and r["pdfa_preserved"] != "",
         lambda p: p == "pdfa-preserve-failed"),
    ]
    for name, applies, hit in checks:
        sub = [r for r in rows if applies(r)]
        bad = [r for r in sub if any(hit(p) for p in r["problems"].split(";") if p)]
        out.append(f"| {name} | {len(sub)} | {len(bad)} |")
    out.append("\n## SSIMULACRA2 by kind of PDF (changed files)\n\n| Kind | Files | Median of medians | Worst page |\n|---|---|---|---|")
    for g in sorted({r["group"] for r in rows}):
        v = [r for r in rows if r["group"] == g and r["ss2_median"] != ""]
        if v:
            out.append(f"| {g} | {len(v)} | {statistics.median(float(r['ss2_median']) for r in v):.1f} | "
                       f"{min(float(r['ss2_min']) for r in v):.1f} |")
    bad = [r for r in rows if r["verdict"] != "pass"]
    out.append(f"\n## Files that failed or need a look ({len(bad)})\n\n| File | Kind | Problems | Worst SSIM | Worst SSIMULACRA2 |\n|---|---|---|---|---|")
    for r in sorted(bad, key=lambda r: (r["verdict"] != "fail", r["group"])):
        out.append(f"| {r['sha256'][:12]} | {r['group']} | {r['problems']} | {r['ssim_min']} | {r['ss2_min']} |")
    print("\n".join(out))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compress")
    c.add_argument("manifest"); c.add_argument("corpus"); c.add_argument("out")
    c.add_argument("--server", default="http://localhost:8080"); c.add_argument("--query", default="preset=medium")
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    k = sub.add_parser("check")
    k.add_argument("manifest"); k.add_argument("corpus"); k.add_argument("out"); k.add_argument("results")
    k.add_argument("--pdfa-preserved"); k.add_argument("--jobs", type=int, default=os.cpu_count())
    r = sub.add_parser("report")
    r.add_argument("results")
    a = ap.parse_args()
    {"compress": compress, "check": check, "report": report}[a.cmd](a)


if __name__ == "__main__":
    main()
