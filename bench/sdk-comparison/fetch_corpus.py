import csv
import hashlib
import io
import os
import sys
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'corpus')
UA = {'User-Agent': 'hyper-compress-benchmark/1.0'}


class RemoteFile(io.RawIOBase):
    def __init__(self, url):
        self.url, self.pos = url, 0
        req = urllib.request.Request(url, method='HEAD', headers=UA)
        with urllib.request.urlopen(req) as r:
            self.size = int(r.headers['Content-Length'])

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=0):
        self.pos = [off, self.pos + off, self.size + off][whence]
        return self.pos

    def readinto(self, b):
        if self.pos >= self.size or len(b) == 0:
            return 0
        end = min(self.pos + len(b), self.size) - 1
        req = urllib.request.Request(self.url, headers={**UA, 'Range': f'bytes={self.pos}-{end}'})
        with urllib.request.urlopen(req) as r:
            data = r.read()
        b[:len(data)] = data
        self.pos += len(data)
        return len(data)


def fetch(url):
    if '#member=' in url:
        base, member = url.split('#member=', 1)
        with zipfile.ZipFile(io.BufferedReader(RemoteFile(base), 1 << 20)) as z:
            return z.read(member)
    if 'arxiv.org' in url:
        time.sleep(3)
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA)) as r:
        return r.read()


def main():
    os.makedirs(OUT, exist_ok=True)
    bad = []
    rows = list(csv.DictReader(open(os.path.join(HERE, 'MANIFEST.csv'))))
    for i, row in enumerate(rows, 1):
        dst = os.path.join(OUT, row['file'])
        if os.path.exists(dst) and hashlib.sha256(open(dst, 'rb').read()).hexdigest() == row['sha256']:
            continue
        urls = [row['url']]
        if '#member=' not in row['url']:
            urls.append('https://web.archive.org/web/2025id_/' + row['url'])
        data, why = None, ''
        for url in urls:
            try:
                got = fetch(url)
            except Exception as e:
                why = str(e)
                continue
            if hashlib.sha256(got).hexdigest() == row['sha256']:
                data = got
                break
            why = 'sha256 differs from MANIFEST.csv'
        if data is None:
            bad.append((row['file'], why))
            continue
        with open(dst, 'wb') as f:
            f.write(data)
        print(f'{i}/{len(rows)} {row["file"]}', flush=True)
    for name, why in bad:
        print(f'FAILED {name}: {why}', file=sys.stderr)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
