import { readdir, writeFile, copyFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { join, dirname, resolve, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { sh, fileSize, renderPages, aggregateFidelity, checkValidity, textSimilarity, judgeCell, withTmpDir } from '../tests/regression/lib.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..');
const CLI = join(ROOT, 'dist', 'cli', 'bin', 'hyper.js');

const corpusDir = process.argv[2];
const outJson = process.argv[3] || join(HERE, 'results.json');
const perCategory = Number(process.env.BENCH_PER_CATEGORY || 0);
const concurrency = Math.max(1, Number(process.env.BENCH_CONCURRENCY || 1));
if (!corpusDir) {
  console.error('usage: node bench/bench.mjs <corpusDir> [results.json]');
  process.exit(2);
}

const nonEmpty = async (p) => (await fileSize(p)) > 0;

async function runHyper(input, output, preset) {
  const r = await sh(process.execPath, [CLI, input, output, '--preset', preset], { timeoutMs: 300_000 });
  return { ok: r.code === 0 && (await nonEmpty(output)), note: r.timedOut ? 'timeout' : '' };
}

const GS_SETTING = { prepress: '/prepress', printer: '/printer', ebook: '/ebook', screen: '/screen' };
async function runGhostscript(input, output, preset) {
  const r = await sh('gs', [
    '-sDEVICE=pdfwrite', '-dCompatibilityLevel=1.6', `-dPDFSETTINGS=${GS_SETTING[preset]}`,
    '-dNOPAUSE', '-dBATCH', '-dQUIET', '-dAutoRotatePages=/None',
    `-sOutputFile=${output}`, input,
  ], { timeoutMs: 300_000 });
  return { ok: r.code === 0 && (await nonEmpty(output)), note: r.timedOut ? 'timeout' : '' };
}

const MU_STRUCT = ['-gggg', '-z', '-Z', '-i', '-f'];
const MU_IMG = {
  r300: { dpi: '450,300', q: 85 },
  r150: { dpi: '225,150', q: 75 },
  r72: { dpi: '108,72', q: 60 },
};
async function runMutool(input, output, preset) {
  const args = [...MU_STRUCT];
  if (preset !== 'structural') {
    const t = MU_IMG[preset];
    args.push(
      '--color-image-subsample-method', 'bicubic',
      '--gray-image-subsample-method', 'bicubic',
      '--color-image-subsample-dpi', t.dpi,
      '--gray-image-subsample-dpi', t.dpi,
      '--color-image-recompress-method', `jpeg:${t.q}`,
      '--gray-image-recompress-method', `jpeg:${t.q}`,
    );
  }
  const r = await sh('mutool', ['clean', ...args, input, output], { timeoutMs: 300_000 });
  return { ok: r.code === 0 && (await nonEmpty(output)), note: r.timedOut ? 'timeout' : '' };
}

async function runQpdf(input, output) {
  const r = await sh('qpdf', ['--warning-exit-0', '--object-streams=generate', '--recompress-flate', '--compression-level=9', input, output], { timeoutMs: 300_000 });
  return { ok: r.code === 0 && (await nonEmpty(output)), note: r.timedOut ? 'timeout' : '' };
}

async function runCpdf(input, output) {
  const r = await sh('cpdf', ['-squeeze', input, '-o', output], { timeoutMs: 300_000 });
  return { ok: r.code === 0 && (await nonEmpty(output)), note: r.timedOut ? 'timeout' : '' };
}

const ENGINES = [
  { id: 'hyper', label: 'Hyper', presets: ['lossless', 'low', 'medium', 'high'], run: runHyper },
  { id: 'gs', label: 'Ghostscript', presets: ['prepress', 'printer', 'ebook', 'screen'], run: runGhostscript },
  { id: 'mutool', label: 'MuPDF', presets: ['structural', 'r300', 'r150', 'r72'], run: runMutool },
  { id: 'qpdf', label: 'qpdf', presets: ['structural'], run: runQpdf },
  { id: 'cpdf', label: 'cpdf', presets: ['structural'], run: runCpdf },
];

const PRESET_TIER = {
  'hyper|lossless': 'lossless', 'hyper|low': 'r300', 'hyper|medium': 'r150', 'hyper|high': 'r72',
  'gs|prepress': 'rmax', 'gs|printer': 'r300', 'gs|ebook': 'r150', 'gs|screen': 'r72',
  'mutool|structural': 'lossless', 'mutool|r300': 'r300', 'mutool|r150': 'r150', 'mutool|r72': 'r72',
  'qpdf|structural': 'lossless', 'cpdf|structural': 'lossless',
};

const docs = [];
const entries = (await readdir(corpusDir, { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name));
for (const e of entries) {
  if (e.isDirectory()) {
    let files = (await readdir(join(corpusDir, e.name))).filter((f) => f.endsWith('.pdf')).sort();
    if (perCategory > 0) files = files.slice(0, perCategory);
    for (const f of files) docs.push({ path: join(corpusDir, e.name, f), category: e.name, file: f });
  } else if (e.name.endsWith('.pdf')) {
    docs.push({ path: join(corpusDir, e.name), category: 'root', file: e.name });
  }
}
console.log(`bench: ${docs.length} docs, ${ENGINES.reduce((n, e) => n + e.presets.length, 0)} cells per doc`);

const cells = [];
let done = 0;
async function benchDoc(doc) {
  const srcSize = await fileSize(doc.path);
  const inPages = await renderPages(doc.path, { dpi: 80, maxPages: 6 });
  if (!inPages) {
    console.log(`skip (unrenderable source): ${doc.file}`);
    return;
  }
  const srcVal = await checkValidity(doc.path);
  for (const eng of ENGINES) {
    for (const preset of eng.presets) {
      await withTmpDir(async (d) => {
        const out = join(d, 'out.pdf');
        const t0 = performance.now();
        const r = await eng.run(doc.path, out, preset);
        const ms = performance.now() - t0;
        const runOk = r.ok && existsSync(out);
        const outSize = runOk ? await fileSize(out) : 0;
        const outPages = runOk ? await renderPages(out, { dpi: 80, maxPages: 6 }) : null;
        const fid = aggregateFidelity(inPages, outPages);
        const val = runOk ? await checkValidity(out) : null;
        const textSim = runOk ? await textSimilarity(doc.path, out) : null;
        const tier = PRESET_TIER[`${eng.id}|${preset}`];
        const lossless = tier === 'lossless';
        const ssim = fid.ok && !fid.dimMismatch ? fid.ssimMin : null;
        const dq = judgeCell({ runOk, srcVal, val, fid, ssim, textSim, lossless });
        cells.push({
          file: doc.file, category: doc.category, engine: eng.id, preset, tier,
          srcSize, outSize, savedPct: runOk ? +(100 * (1 - outSize / srcSize)).toFixed(2) : null,
          grew: runOk ? outSize > srcSize : null,
          ssimMin: ssim, ssimMean: fid.ok && !fid.dimMismatch ? +fid.ssimMean.toFixed(5) : null,
          textSim: textSim && textSim.hadText ? +textSim.similarity.toFixed(4) : null,
          ms: Math.round(ms), dq, note: r.note || '',
        });
        if (process.env.BENCH_KEEP && dq.length) {
          await copyFile(out, join(HERE, `dq-${eng.id}-${preset}-${basename(doc.file)}`)).catch(() => {});
        }
      });
    }
  }
  done++;
  console.log(`[${done}/${docs.length}] ${doc.category}/${doc.file}`);
}

const queue = docs.slice();
await Promise.all(Array.from({ length: Math.min(concurrency, queue.length) }, async () => {
  while (queue.length) await benchDoc(queue.shift());
}));

await writeFile(outJson, JSON.stringify({
  generatedAt: new Date().toISOString(),
  corpus: corpusDir, docCount: done,
  engines: ENGINES.map((e) => ({ id: e.id, label: e.label, presets: e.presets })),
  cells,
}, null, 1));
console.log(`wrote ${outJson}: ${cells.length} cells over ${done} docs`);
