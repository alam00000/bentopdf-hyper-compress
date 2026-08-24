#!/usr/bin/env python3
import argparse
import pathlib
import sys

BUILDCONFIG_FROM = '''  assert(
      false,
      "emscripten is not a supported target_os. It is available only as secondary toolchain.")'''

BUILDCONFIG_TO = '''  _default_toolchain = "//build/toolchain/wasm:wasm"'''

WASM_FLAGS = ("-D_POSIX_C_SOURCE=200112 -fno-stack-protector -fno-exceptions "
              "-Wno-unknown-warning-option -sSUPPORT_LONGJMP=wasm")

TOOLCHAIN_FROM = """  nm = cc
  ld = cxx
"""

TOOLCHAIN_TO = f"""  nm = cc
  ld = cxx

  extra_cflags = "{WASM_FLAGS}"
  extra_cxxflags = "{WASM_FLAGS}"
"""

V8_GUARD_FROM = '#include "v8/include/libplatform/libplatform.h"'
V8_GUARD_TO = ('#ifdef PDF_ENABLE_V8\n'
               '#include "v8/include/libplatform/libplatform.h"')

V8_END_FROM = """  g_bpdf_lib_inited = false;
}
"""
V8_END_TO = """  g_bpdf_lib_inited = false;
}
#endif  // PDF_ENABLE_V8
"""

SHA1_FROM = '''#include <stdio.h>
#include <limits.h> /* to get info about range unsigned int */

#if ULONG_MAX == 4294967295U
typedef unsigned long int uint32_t;
#elif UINT_MAX == 4294967295U
typedef unsigned int uint32_t;
#else
#error "There must be a way to get an unsigned 32 bit integer"
#endif'''

SHA1_TO = '''#include <stdio.h>
#include <stdint.h>'''

LEPT_FROM = '''    "NO_CONSOLE_IO",
  ]'''

LEPT_TO = '''    "NO_CONSOLE_IO",
  ]

  if (is_wasm) {
    defines += [ "_GNU_SOURCE" ]
  }'''

GNCHECK_FROM = '  if (defined(checkout_skia) && checkout_skia && !is_android) {'
GNCHECK_TO = '  if (defined(checkout_skia) && checkout_skia && !is_android &&\n      current_cpu != "wasm") {'

FXGE_FROM = '  if (is_linux || is_chromeos) {\n    sources += [ "linux/fx_linux_impl.cpp" ]\n  }'
FXGE_TO = '  if (is_linux || is_chromeos || is_wasm) {\n    sources += [ "linux/fx_linux_impl.cpp" ]\n  }'

EDITS = [
    ("BUILD.gn", GNCHECK_FROM, GNCHECK_TO,
     "gn_check skia off on wasm", False),
    ("core/fxge/BUILD.gn", FXGE_FROM, FXGE_TO,
     "fxge linux platform backend on wasm", False),
    ("third_party/afdko/shared/sha1.c", SHA1_FROM, SHA1_TO,
     "afdko sha1 uint32_t", False),
    ("third_party/leptonica/BUILD.gn", LEPT_FROM, LEPT_TO,
     "leptonica _GNU_SOURCE for realpath", False),
    ("build/config/BUILDCONFIG.gn", BUILDCONFIG_FROM, BUILDCONFIG_TO,
     "BUILDCONFIG default toolchain", False),
    ("build/toolchain/wasm/BUILD.gn", TOOLCHAIN_FROM, TOOLCHAIN_TO,
     "wasm toolchain posix flags", False),
    ("fpdfsdk/fpdf_view.cpp", V8_GUARD_FROM, V8_GUARD_TO,
     "fpdf_view v8 guard open", True),
    ("fpdfsdk/fpdf_view.cpp", V8_END_FROM, V8_END_TO,
     "fpdf_view v8 guard close", True),
]

def apply(path, old, new, label, revert, optional=False):
    text = path.read_text()
    already_applied = new in text
    if already_applied == (not revert):
        print(f"  {label}: already {'applied' if already_applied else 'reverted'}")
        return
    src, dst = (new, old) if revert else (old, new)
    if text.count(src) != 1:
        if optional:
            print(f"  {label}: anchor absent  - skipped (stock upstream tree)")
            return
        print(f"  {label}: anchor matched {text.count(src)} times, expected 1",
              file=sys.stderr)
        sys.exit(3)
    path.write_text(text.replace(src, dst, 1))
    print(f"  {label}: {'reverted' if revert else 'applied'}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tree")
    ap.add_argument("--revert", action="store_true")
    args = ap.parse_args()

    tree = pathlib.Path(args.tree)
    if not (tree / "fpdfsdk" / "fpdf_view.cpp").exists():
        print(f"not a pdfium checkout: {tree}", file=sys.stderr)
        return 2

    print(f"{'reverting' if args.revert else 'patching'} {tree}")
    for rel, old, new, label, optional in EDITS:
        apply(tree / rel, old, new, label, args.revert, optional)
    return 0

if __name__ == "__main__":
    sys.exit(main())
