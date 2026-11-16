import csv
import hashlib
import io
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
UA = {"User-Agent": "hyper-compress-regress/1.0 (+https://github.com/alam00000/bentopdf-hyper-compress)"}


def get(url, headers=None, tries=4):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={**UA, **(headers or {})})
            with urllib.request.urlopen(req, timeout=300) as r:
                return r.read()
        except OSError:
            if i == tries - 1:
                raise
            time.sleep(5 * (i + 1))


class RangeFile(io.RawIOBase):
    def __init__(self, url):
        self.url, self.pos = url, 0
        req = urllib.request.Request(url, method="HEAD", headers=UA)
        with urllib.request.urlopen(req, timeout=60) as r:
            self.size = int(r.headers["Content-Length"])

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=io.SEEK_SET):
        self.pos = {io.SEEK_SET: 0, io.SEEK_CUR: self.pos, io.SEEK_END: self.size}[whence] + off
        return self.pos

    def read(self, n=-1):
        end = self.size if n is None or n < 0 else min(self.size, self.pos + n)
        if end <= self.pos:
            return b""
        data = get(self.url, {"Range": f"bytes={self.pos}-{end - 1}"})
        self.pos += len(data)
        return data


def fetch(url):
    if url.startswith("https://arxiv.org/abs/"):
        return get("https://arxiv.org/pdf/" + url.rsplit("/", 1)[1])
    if "#member=" in url:
        archive, member = url.split("#member=", 1)
        with zipfile.ZipFile(RangeFile(archive)) as z:
            return z.read(member)
    return get(url)


def main():
    out = HERE / "remote"
    out.mkdir(exist_ok=True)
    bad = 0
    for row in csv.DictReader(open(HERE / "remote.csv")):
        dest = out / row["file"]
        if dest.exists():
            continue
        data = fetch(row["source_url"])
        if hashlib.sha256(data).hexdigest() != row["sha256"]:
            print(f"checksum mismatch: {row['file']} from {row['source_url']}")
            bad += 1
            continue
        dest.write_bytes(data)
        print(f"fetched {row['file']} ({len(data)} bytes)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
