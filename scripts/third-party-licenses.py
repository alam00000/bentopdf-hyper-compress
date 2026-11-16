import base64
import io
import json
import re
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PDFIUM = "https://pdfium.googlesource.com/pdfium/+/162c9521f74c17bb0c8595608f23ab22cec3d407/"
CR = "https://chromium.googlesource.com/"
GH = "https://raw.githubusercontent.com/"
EM = GH + "emscripten-core/emscripten/4.0.22/system/lib/"


def fetch(url):
    if "googlesource.com" in url:
        return base64.b64decode(urllib.request.urlopen(url + "?format=TEXT", timeout=60).read()).decode()
    return urllib.request.urlopen(url, timeout=60).read().decode()


def ijg9f():
    data = urllib.request.urlopen("https://storage.googleapis.com/webassembly/emscripten-ports/jpegsrc.v9f.tar.gz", timeout=60).read()
    readme = tarfile.open(fileobj=io.BytesIO(data)).extractfile("jpeg-9f/README").read().decode("latin-1")
    return readme[readme.index("LEGAL ISSUES"):readme.index("REFERENCES\n")]


def icu():
    text = fetch(CR + "chromium/deps/icu/+/d578f2e8b7bd5938e21cfb6bf15c079e0aa5b738/LICENSE")
    return text[:text.index("SPDX-License-Identifier: Unicode-3.0")] + "SPDX-License-Identifier: Unicode-3.0\n"


def local(path):
    return lambda: (ROOT / path).read_text()


def url(u):
    return lambda: fetch(u)


def icc():
    lines = (ROOT / "core/src/hyper_generic_cmyk_icc.h").read_text().splitlines()
    return "\n".join(line[3:] for line in lines if line.startswith("//")) + "\n"


COMPONENTS = [
    ("PDFium", "162c9521f74c17bb0c8595608f23ab22cec3d407", "BSD-3-Clause (with Apache-2.0 text as shipped)", [url(PDFIUM + "LICENSE")]),
    ("zlib (Chromium fork, in PDFium)", "c5cc9edf8992ff36dfca3c2c4f6c8327a66b6782", "Zlib; SIMD files BSD-3-Clause (Chromium)", [url(CR + "chromium/src/third_party/zlib/+/c5cc9edf8992ff36dfca3c2c4f6c8327a66b6782/LICENSE"), url(CR + "chromium/src/+/refs/heads/main/LICENSE")]),
    ("libjpeg-turbo (in PDFium)", "640f254ad0fa03f6b1f29f89b7dd9366f2f6e533", "IJG AND BSD-3-Clause AND Zlib", [url(CR + "chromium/deps/libjpeg_turbo/+/640f254ad0fa03f6b1f29f89b7dd9366f2f6e533/LICENSE.md"), url(CR + "chromium/deps/libjpeg_turbo/+/640f254ad0fa03f6b1f29f89b7dd9366f2f6e533/README.ijg")]),
    ("OpenJPEG (in PDFium)", "2.5.4", "BSD-2-Clause", [url(PDFIUM + "third_party/libopenjpeg/LICENSE")]),
    ("Little CMS (in PDFium)", "2.19", "MIT", [url(PDFIUM + "third_party/lcms/LICENSE")]),
    ("FreeType (in PDFium)", "656cb777798fa420a13faba3758779e9ed6c4798", "FTL", [url(CR + "chromium/src/third_party/freetype2/+/656cb777798fa420a13faba3758779e9ed6c4798/docs/FTL.TXT")]),
    ("Brotli (in PDFium)", "803ac71664ad62af1de39ff55e63ebbe07006fc8", "MIT", [url(CR + "chromium/src/third_party/brotli/+/803ac71664ad62af1de39ff55e63ebbe07006fc8/LICENSE")]),
    ("HarfBuzz (in PDFium)", "28f4dc622fef010dab12f4275ac195b5d4a4a704", "MIT-Modern-Variant (Old MIT)", [url(CR + "external/github.com/harfbuzz/harfbuzz/+/28f4dc622fef010dab12f4275ac195b5d4a4a704/COPYING")]),
    ("ICU (in PDFium)", "d578f2e8b7bd5938e21cfb6bf15c079e0aa5b738", "Unicode-3.0", [icu]),
    ("Abseil (in PDFium)", "df548c50b2cda67158364d3d23c63043881b391d", "Apache-2.0", [url(CR + "chromium/src/third_party/abseil-cpp/+/df548c50b2cda67158364d3d23c63043881b391d/LICENSE")]),
    ("Anti-Grain Geometry (in PDFium)", "2.3", "AGG", [url(PDFIUM + "third_party/agg23/copying")]),
    ("fast_float (in PDFium)", "34164f547b7df3f5d794ff67e9f885c36819ebfc", "MIT", [url(CR + "external/github.com/fastfloat/fast_float/+/34164f547b7df3f5d794ff67e9f885c36819ebfc/LICENSE-MIT")]),
    ("Dragonbox (in PDFium)", "beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44", "BSL-1.0", [url(CR + "external/github.com/jk-jeon/dragonbox/+/beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44/LICENSE-Boost")]),
    ("qpdf", "12.3.2", "Apache-2.0", [url(GH + "qpdf/qpdf/v12.3.2/LICENSE.txt"), url(GH + "qpdf/qpdf/v12.3.2/NOTICE.md")]),
    ("OpenSSL (Windows qpdf30.dll)", "3.6.0", "Apache-2.0", [url(GH + "openssl/openssl/openssl-3.6.0/LICENSE.txt")]),
    ("IJG libjpeg (Windows qpdf30.dll, WebAssembly build)", "9f", "IJG", [ijg9f]),
    ("zlib (Windows qpdf30.dll, WebAssembly build)", "1.3.1", "Zlib", [url(GH + "madler/zlib/v1.3.1/LICENSE")]),
    ("Microsoft Visual C++ runtime (Windows)", "14.x", "Microsoft Visual Studio redistributable terms", [lambda: "concrt140.dll, msvcp140*.dll, vcruntime140*.dll are distributed under the\nMicrosoft Visual Studio license terms for redistributable code.\n"]),
    ("jpegli (libjxl)", "0.11.1", "BSD-3-Clause", [url(GH + "libjxl/libjxl/v0.11.1/LICENSE"), url(GH + "libjxl/libjxl/v0.11.1/PATENTS")]),
    ("Highway", "457c891775a7397bdb0376bb1031e6e027af1c48", "Apache-2.0", [url(GH + "google/highway/457c891775a7397bdb0376bb1031e6e027af1c48/LICENSE")]),
    ("Leptonica", "bundled", "BSD-2-Clause (Leptonica License)", [local("core/third_party/leptonica/leptonica-license.txt")]),
    ("jbig2enc", "bundled", "Apache-2.0", [local("core/third_party/jbig2enc/COPYING")]),
    ("AFDKO (subset)", "5.0.1", "Apache-2.0", [url(GH + "adobe-type-tools/afdko/5.0.1/LICENSE.md")]),
    ("ISO Coated v2 300% ICC profile (basICColor GmbH)", "bundled", "Zlib", [icc]),
    ("musl libc (WebAssembly build, Emscripten 4.0.22)", "4.0.22", "MIT", [url(EM + "libc/musl/COPYRIGHT")]),
    ("libc++ (WebAssembly build, Emscripten 4.0.22)", "4.0.22", "Apache-2.0 WITH LLVM-exception", [url(EM + "libcxx/LICENSE.TXT")]),
    ("libc++abi (WebAssembly build, Emscripten 4.0.22)", "4.0.22", "Apache-2.0 WITH LLVM-exception", [url(EM + "libcxxabi/LICENSE.TXT")]),
    ("libunwind (WebAssembly build, Emscripten 4.0.22)", "4.0.22", "Apache-2.0 WITH LLVM-exception", [url(EM + "libunwind/LICENSE.TXT")]),
    ("compiler-rt (WebAssembly build, Emscripten 4.0.22)", "4.0.22", "Apache-2.0 WITH LLVM-exception", [url(EM + "compiler-rt/LICENSE.TXT")]),
    ("dlmalloc (WebAssembly build, Emscripten 4.0.22)", "2.8.6", "Public domain", [lambda: "This is a version (aka dlmalloc) of malloc/free/realloc written by\nDoug Lea and released to the public domain, as explained at\nhttp://creativecommons.org/publicdomain/zero/1.0/\n"]),
    ("DM Sans", "bundled", "OFL-1.1", [local("web/fonts/OFL.txt")]),
    ("Phosphor Icons", "bundled", "MIT", [local("web/icons/LICENSE.txt")]),
]

LICENSE_FILE = re.compile(r"^(LICEN[CS]E|COPYING|COPYRIGHT|UNLICENSE|NOTICE|AUTHORS$)", re.I)


def crates():
    meta = json.loads(subprocess.check_output(["cargo", "metadata", "--format-version", "1", "--manifest-path", str(ROOT / "rust/Cargo.toml")]))
    pkgs = {p["id"]: p for p in meta["packages"]}
    nodes = {n["id"]: n for n in meta["resolve"]["nodes"]}
    stack = [i for i, p in pkgs.items() if p["name"] in ("hyper-compress-node", "hyper-compress-cli", "hyper-compress-server")]
    seen = set()
    while stack:
        i = stack.pop()
        if i not in seen:
            seen.add(i)
            stack += [d["pkg"] for d in nodes[i]["deps"] if any(k["kind"] is None for k in d["dep_kinds"])]
    out = []
    for p in sorted((pkgs[i] for i in seen if pkgs[i]["source"]), key=lambda p: (p["name"], p["version"])):
        d = Path(p["manifest_path"]).parent
        texts = [lambda f=f: f.read_text() for f in sorted(d.iterdir()) if f.is_file() and LICENSE_FILE.match(f.name)]
        if not texts:
            sha = json.loads((d / ".cargo_vcs_info.json").read_text())["git"]["sha1"]
            texts = [url(p["repository"].replace("https://github.com/", GH) + f"/{sha}/LICENSE")]
        out.append(("Rust crate " + p["name"], p["version"], p["license"], texts))
    return out


def main():
    parts = [
        "Hyper Compress third-party licenses\n",
        "This software is based in part on the work of the Independent JPEG Group.\n",
        "Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved.\n",
    ]
    for name, version, lic, texts in COMPONENTS + crates():
        body = "\n".join(t().rstrip() + "\n" for t in texts)
        parts.append("=" * 78 + f"\n{name}\nVersion: {version}\nLicense: {lic}\n" + "=" * 78 + "\n\n" + body)
    (ROOT / "THIRD_PARTY_LICENSES").write_text("\n".join(parts))


if __name__ == "__main__":
    sys.exit(main())
