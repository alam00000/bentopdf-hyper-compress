.PHONY: build wasm check test lint typecheck smoke verify regress bench harden manifest fuzz clean

build:
	core/build/apply-tree.sh
	core/build/build-native.sh
	sdk/native/build-dylib.sh

wasm:
	wasm/build-pdfium-wasm.sh
	wasm/build-wasm.sh

check: typecheck lint test version-check

typecheck:
	npm run --silent typecheck

lint:
	npm run --silent lint

test:
	npm run --silent test

version-check:
	@node -e "const p=require('./package.json').version;const h=require('fs').readFileSync('sdk/native/hpdf.h','utf8').match(/HPDF_VERSION \"(.*)\"/)[1];if(p!==h){console.error('version drift: package.json '+p+' vs hpdf.h '+h);process.exit(1)};console.log('version '+p)"

smoke:
	node dist/scripts/smoke.js "$${PDF:?set PDF=path/to.pdf}"

verify:
	node wasm/verify.mjs "$${PDF:?set PDF=path/to.pdf}"

regress:
	npm run --silent build
	node tests/regression/regress.mjs

bench:
	npm run --silent build
	node bench/bench.mjs "$${CORPUS:?set CORPUS=path/to/corpus}" bench/results.json

harden:
	scripts/harden.sh

manifest:
	scripts/manifest.sh

fuzz:
	scripts/build-fuzz.sh && ./fuzz/out/fuzz_compress -max_total_time=60 fuzz/corpus

clean:
	rm -rf dist core/build/out wasm/out fuzz/out
