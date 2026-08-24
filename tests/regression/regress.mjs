import { readdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { sh, renderPages, aggregateFidelity, checkValidity, textSimilarity, judgeCell, withTmpDir } from './lib.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..', '..');
const CLI = join(ROOT, 'dist', 'cli', 'bin', 'hyper.js');
const WORKER = join(ROOT, 'cli', 'prebuilt', 'hpdf-worker');
const FAILSET = join(HERE, 'failset');
const PRESETS = ['lossless', 'low', 'medium', 'high'];

if (!existsSync(WORKER)) {
  console.log('regress: cli/prebuilt/hpdf-worker not built, skipping');
  process.exit(0);
}
if (!existsSync(CLI)) {
  console.error('regress: dist not built, run `npm run build` first');
  process.exit(1);
}

const filter = process.argv[2];
const files = (await readdir(FAILSET))
  .filter((f) => f.endsWith('.pdf') && (!filter || f.includes(filter)))
  .sort();

let failPairs = 0;
const failFiles = new Set();
const failures = [];

for (const f of files) {
  const src = join(FAILSET, f);
  const inPages = await renderPages(src, { dpi: 100, maxPages: 6 });
  if (!inPages) {
    console.log(`${f}: source not renderable, skipping`);
    continue;
  }
  const srcVal = await checkValidity(src);
  for (const preset of PRESETS) {
    await withTmpDir(async (d) => {
      const out = join(d, 'out.pdf');
      const r = await sh(process.execPath, [CLI, src, out, '--preset', preset], { timeoutMs: 300_000 });
      const runOk = r.code === 0 && existsSync(out);
      const outPages = runOk ? await renderPages(out, { dpi: 100, maxPages: 6 }) : null;
      const fid = aggregateFidelity(inPages, outPages);
      const val = runOk ? await checkValidity(out) : null;
      const textSim = runOk ? await textSimilarity(src, out) : null;
      const ssim = fid.ok && !fid.dimMismatch ? fid.ssimMin : null;
      const dq = judgeCell({ runOk, srcVal, val, fid, ssim, textSim, lossless: preset === 'lossless' });
      if (dq.length) {
        failPairs++;
        failFiles.add(f);
        failures.push({ f, preset, ssim, dq });
      }
    });
  }
  process.stdout.write('.');
}
console.log('');

for (const x of failures) {
  console.log(`FAIL ${x.f.padEnd(36)} ${x.preset.padEnd(9)} ssim=${x.ssim == null ? '?' : x.ssim.toFixed(3)} ${x.dq.join(',')}`);
}
console.log(`${failPairs} failing (file,preset) pairs across ${failFiles.size} of ${files.length} files`);
process.exit(failPairs === 0 ? 0 : 1);
