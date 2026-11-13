"""Download the load-test corpus from its public origins and check every file against the manifest's SHA-256.

  python3 bench/throughput/fetch_corpus.py bench/throughput/manifest.csv corpus/

source_url forms:
  https://...                      a direct download
  https://.../x.zip#member=a/b.pdf one file inside a public zip (Common Crawl, GovDocs1); each zip is downloaded once
  https://arxiv.org/abs/<id>       fetched from https://arxiv.org/pdf/<id>
Files that no longer match their checksum (a web page changed) are reported, not kept.
"""
import concurrent.futures as cf
import csv
import hashlib
import io
import sys
import threading
import time
import urllib.request
import zipfile
from pathlib import Path

UA = {"User-Agent": "hyper-compress-loadtest/1.0 (+https://github.com/alam00000/bentopdf-hyper-compress)"}
zip_locks = {}
zip_cache = {}
guard = threading.Lock()


def get(url, tries=4):
    for i in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=600) as r:
                return r.read()
        except OSError:
            if i == tries - 1:
                raise
            time.sleep(5 * (i + 1))


def resolve(url):
    if url.startswith("https://arxiv.org/abs/"):
        return "https://arxiv.org/pdf/" + url.rsplit("/", 1)[1], None
    if "#member=" in url:
        archive, member = url.split("#member=", 1)
        return archive, member
    return url, None


def member_bytes(archive, member, cache_dir):
    with guard:
        lock = zip_locks.setdefault(archive, threading.Lock())
    with lock:
        if archive not in zip_cache:
            local = cache_dir / hashlib.sha256(archive.encode()).hexdigest()
            if not local.exists():
                local.write_bytes(get(archive))
            zip_cache[archive] = local
    with zipfile.ZipFile(zip_cache[archive]) as z:
        return z.read(member)


def fetch(row, out, cache_dir):
    dest = out / f"{row['sha256']}.pdf"
    if dest.exists():
        return "kept"
    url, member = resolve(row["source_url"])
    try:
        data = member_bytes(url, member, cache_dir) if member else get(url)
    except Exception as e:
        return f"failed {type(e).__name__}"
    if hashlib.sha256(data).hexdigest() != row["sha256"]:
        return "changed"
    dest.write_bytes(data)
    return "ok"


def main():
    manifest, out = sys.argv[1], Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    cache_dir = out / ".archives"
    cache_dir.mkdir(exist_ok=True)
    rows = list(csv.DictReader(open(manifest)))
    results = {}
    with cf.ThreadPoolExecutor(8) as ex:
        for row, res in zip(rows, ex.map(lambda r: fetch(r, out, cache_dir), rows)):
            results.setdefault(res.split()[0], []).append(row["sha256"])
            if res not in ("ok", "kept"):
                print(f"{res}: {row['source_url']}")
    print({k: len(v) for k, v in results.items()})


if __name__ == "__main__":
    main()
