#!/bin/bash
# job.sh ENGINE LEVEL FILE [RC SECONDS] -> one RESULTS.csv row
# With RC and SECONDS the output already exists (Nutrient batch mode); otherwise the engine runs here.
H=$(cd "$(dirname "$0")" && pwd)
engine=$1 level=$2 file=$3
in=$H/corpus/$file out=$H/out/$engine-$level-$file
mkdir -p "$H/out" "$H/ref"

if [ $# -ge 5 ]; then
  rc=$4 secs=$5
else
  case $engine in
    hyper)    cmd=(node "$H/node_modules/hyper-compress/dist/cli/bin/hyper.js" "$in" "$out" --preset "$level") ;;
    pdftools) cmd=("${PYTHON:-python3}" "$H/engines/pdftools.py" "$in" "$out" "$level") ;;
    apryse)   cmd=("${PYTHON:-python3}" "$H/engines/apryse.py" "$in" "$out" "$level") ;;
  esac
  t0=$(perl -MTime::HiRes=time -e 'print time')
  ( "${cmd[@]}" >/dev/null 2>&1 ) & pid=$!
  ( sleep 600; kill -9 $pid 2>/dev/null ) & guard=$!
  wait $pid; rc=$?
  kill $guard 2>/dev/null; wait $guard 2>/dev/null
  t1=$(perl -MTime::HiRes=time -e 'print time')
  secs=$(echo "$t1 - $t0" | bc)
fi

isize=$(stat -L -f%z "$in" 2>/dev/null || stat -L -c%s "$in")
osize= ok=0 q1= q2=
if [ -s "$out" ]; then
  pages_in=$(qpdf --show-npages "$in" 2>/dev/null | head -1)
  pages_out=$(qpdf --show-npages "$out" 2>/dev/null | head -1)
  if [ -n "$pages_out" ] && [ "$pages_out" = "$pages_in" ]; then
    ok=1
    osize=$(stat -f%z "$out" 2>/dev/null || stat -c%s "$out")
    for p in 1 2; do
      ref=$H/ref/${file%.pdf}-$p.png
      [ -f "$ref" ] || pdftoppm -r 100 -f $p -l $p -singlefile -png "$in" "${ref%.png}" >/dev/null 2>&1
      [ -f "$ref" ] || continue
      img=$H/out/$engine-$level-${file%.pdf}-$p
      pdftoppm -r 100 -f $p -l $p -singlefile -png "$out" "$img" >/dev/null 2>&1
      v=$(ssimulacra2 "$ref" "$img.png" 2>/dev/null)
      rm -f "$img.png"
      [ $p = 1 ] && q1=$v || q2=$v
    done
  fi
fi
rm -f "$out"
echo "$engine,$level,$file,$isize,$osize,$ok,$rc,$secs,$q1,$q2"
