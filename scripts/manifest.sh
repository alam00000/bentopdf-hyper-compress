#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
{
  echo "# sha256 of committed binary artifacts  - regenerate with scripts/manifest.sh"
  find core/third_party -name '*.a' -o -name '*.lib' | sort | xargs shasum -a 256
  find wasm/prebuilt -name '*.a' 2>/dev/null | sort | xargs shasum -a 256
  [ -f cli/prebuilt/qpdf ] && shasum -a 256 cli/prebuilt/qpdf
} > BINARIES.sha256
wc -l BINARIES.sha256
