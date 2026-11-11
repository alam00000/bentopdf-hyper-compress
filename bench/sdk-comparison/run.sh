#!/bin/bash
# run.sh [ENGINE...] -> appends rows to results.csv (engines: hyper pdftools apryse nutrient)
H=$(cd "$(dirname "$0")" && pwd)
cd "$H" || exit 1
engines=${*:-hyper pdftools apryse nutrient}
files=$(tail -n +2 MANIFEST.csv | cut -d, -f1)
[ -f results.csv ] || echo "engine,level,file,input_bytes,output_bytes,ok,exit_code,seconds,quality_page1,quality_page2" > results.csv
mkdir -p out

for engine in $engines; do
  for level in lossless low medium high; do
    [ "$level" = lossless ] && [ "$engine" != hyper ] && continue
    if [ "$engine" = nutrient ]; then
      reducer=$H/engines/nutrient/bin/Reducer.dll
      i=0
      rm -f out/nutrient-list-*.tsv
      for f in $files; do
        printf '%s\t%s\t%s\n' "$H/corpus/$f" "$H/out/nutrient-$level-$f" "$level" >> "out/nutrient-list-$((i % 4)).tsv"
        i=$((i + 1))
      done
      warm=$H/corpus/$(echo "$files" | head -1)
      for k in 0 1 2 3; do
        dotnet "$reducer" batch "out/nutrient-list-$k.tsv" "$warm" > "out/nutrient-times-$k.tsv" 2>/dev/null &
      done
      wait
      cat out/nutrient-times-*.tsv | awk -F'\t' 'NF == 3 && $1 ~ /\.pdf$/' | while IFS=$'\t' read -r out rc secs; do
        echo "$(basename "$out" | sed "s/^nutrient-$level-//") $rc $secs"
      done | xargs -P 4 -n 3 sh -c './job.sh nutrient '"$level"' "$0" "$1" "$2"' >> results.csv
    else
      echo "$files" | xargs -P 4 -I{} ./job.sh "$engine" "$level" {} >> results.csv
    fi
    echo "$(date +%H:%M) $engine $level done"
  done
done
