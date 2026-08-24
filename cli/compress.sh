#!/usr/bin/env bash
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DRV="$HERE/prebuilt/hpdf-worker"
QPDF="$HERE/prebuilt/qpdf"; [ -x "$QPDF" ] || QPDF="qpdf"

IN="${1:?in}"; OUT="${2:?out}"; shift 2
PASSWORD="${HYPER_PASSWORD:-}"
[ -f "$IN" ] || { echo "no input: $IN" >&2; exit 2; }
[ -x "$DRV" ] || { echo "missing $DRV  - run core/build/build-native.sh" >&2; exit 2; }

TMP="$(mktemp -t hyperc.XXXXXX)"
TMP2="$(mktemp -t hyperc.XXXXXX)"
DEC="$(mktemp -t hyperc.XXXXXX)"
DERR="$(mktemp -t hyperc.XXXXXX)"
trap 'rm -f "$TMP" "$TMP2" "$DEC" "$DERR"' EXIT

fsize() { stat -f%z "$1" 2>/dev/null || stat -c%s "$1"; }

pack() {
  "$QPDF" --warning-exit-0 --object-streams=generate --recompress-flate \
    --compression-level=9 "$1" "$2" 2>/dev/null
}

WORKIN="$IN"
FALLBACK="$IN"
if [ -n "$PASSWORD" ]; then
  if ! "$QPDF" --warning-exit-0 --password="$PASSWORD" --decrypt "$IN" "$DEC" 2>/dev/null; then
    echo "decrypt failed" >&2; exit 3
  fi
  WORKIN="$DEC"; FALLBACK="$DEC"
fi

insize="$(fsize "$FALLBACK")"
final=""
if "$DRV" "$WORKIN" "$TMP" "$@" >/dev/null 2>"$DERR" && [ -s "$TMP" ]; then
  if grep -q 'SIGNED_SKIP' "$DERR"; then
    cp "$FALLBACK" "$OUT"; exit 0
  fi
  if pack "$TMP" "$TMP2" && [ -s "$TMP2" ]; then final="$TMP2"; else final="$TMP"; fi
else
  if pack "$WORKIN" "$TMP2" && [ -s "$TMP2" ]; then final="$TMP2"; fi
fi

if [ -n "$final" ] && [ -s "$final" ]; then
  outsize="$(fsize "$final")"
  if [ "$outsize" -lt "$insize" ]; then cp "$final" "$OUT"; else cp "$FALLBACK" "$OUT"; fi
else
  cp "$FALLBACK" "$OUT"
fi
exit 0
