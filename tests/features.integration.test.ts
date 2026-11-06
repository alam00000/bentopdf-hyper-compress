import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { compress, type CompressInput } from '../sdk/node/engine.js';
import {
  hasEngine,
  engineHasBranchFeatures,
  textPdf,
  imagePdf,
  richTextPdf,
  pdfaPdf,
  featurePdf,
  monoScanPdf,
  cmykImagePdf,
  expandPdf,
  countMarker,
} from './helpers.js';

const engine = hasEngine();
const opts = { skip: engine ? false : 'built worker/qpdf not present' };
const branch = engineHasBranchFeatures();
const needsNewWorker = {
  skip: branch ? false : 'needs an hpdf-worker built from this branch; the prebuilt one predates these engine changes',
};

type RunOptions = Omit<CompressInput, 'sourcePath' | 'savePath'>;

async function run(bytes: Uint8Array, input: RunOptions) {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-feat-'));
  try {
    const src = path.join(dir, 'in.pdf');
    const out = path.join(dir, 'out.pdf');
    writeFileSync(src, bytes);
    const result = await compress({ sourcePath: src, savePath: out, ...input });
    const output = new Uint8Array(readFileSync(out));
    return { result, output, expanded: expandPdf(output) };
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

const has = (bytes: Uint8Array, marker: string): boolean => countMarker(bytes, marker) > 0;

test('linearize writes a linearized file', opts, async () => {
  const { output } = await run(richTextPdf(), { preset: 'medium', options: { linearize: true } });
  assert.ok(has(output.subarray(0, 2048), '/Linearized'));
  const plain = await run(richTextPdf(), { preset: 'medium' });
  assert.ok(!has(plain.output.subarray(0, 2048), '/Linearized'));
});

test('minVersion raises a lower header and never lowers a higher one', opts, async () => {
  const header = (b: Uint8Array) => new TextDecoder().decode(b.subarray(0, 8));
  assert.equal(header((await run(richTextPdf(), { options: { minVersion: '2.0' } })).output), '%PDF-2.0');
  assert.equal(header((await run(textPdf(), { preset: 'lossless', options: { minVersion: '2.0' } })).output), '%PDF-2.0');
  assert.equal(header((await run(richTextPdf(), { options: { minVersion: '1.4' } })).output), '%PDF-1.7');
});

test('report lists fonts, images and cleanups and can be written to a file', opts, async () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-report-'));
  try {
    const reportPath = path.join(dir, 'report.json');
    const { result } = await run(imagePdf(), { preset: 'medium', report: reportPath });
    const report = result.report;
    assert.ok(report, 'report must be returned');
    assert.deepEqual(JSON.parse(readFileSync(reportPath, 'utf8')), report);
    assert.equal(report.compressedSize, result.compressedSize);
    assert.equal(report.images.length, 1);
    const img = report.images[0]!;
    assert.equal(img.page, 1);
    assert.equal(img.name, 'Im0');
    assert.equal(img.before.codec, 'FlateDecode');
    assert.equal(img.before.colorSpace, 'DeviceRGB');
    assert.ok(img.after && img.after.bytes < img.before.bytes);

    const text = await run(featurePdf(), {
      preset: 'medium',
      report: true,
      options: { removeBookmarks: true, removeThumbnails: true },
    });
    const fonts = text.result.report?.fonts ?? [];
    const helv = fonts.find((f) => f.name === 'Helvetica');
    assert.ok(helv);
    assert.equal(helv.type, 'Type1');
    assert.equal(helv.before.embedded, false);
    const bookmarks = text.result.report?.cleanup.find((r) => r.item === 'bookmarks');
    assert.deepEqual(bookmarks, { item: 'bookmarks', before: 1, after: 0 });
    assert.equal((await run(textPdf(), {})).result.report, undefined);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('report carries the placed image resolution from the engine', needsNewWorker, async () => {
  const { result } = await run(imagePdf(500, 400), { preset: 'medium', report: true });
  const dpi = result.report?.images[0]?.dpi;
  assert.equal(typeof dpi, 'number');
  assert.ok(Math.abs(dpi! - 64) <= 2, `500 px over 560 pt is about 64 dpi, got ${dpi}`);
});

test('removeMetadata drops the XMP packet and the document info', opts, async () => {
  const { expanded } = await run(featurePdf(), { preset: 'medium', options: { removeMetadata: true } });
  assert.ok(!has(expanded, 'FEATUREXMP'));
  assert.ok(!has(expanded, 'FEATURETITLE'));
  const kept = await run(featurePdf(), { preset: 'medium' });
  assert.ok(has(kept.expanded, 'FEATUREXMP') && has(kept.expanded, 'FEATURETITLE'));
});

test('removeMetadata also drops custom document info entries', needsNewWorker, async () => {
  const { expanded } = await run(featurePdf(), { preset: 'medium', options: { removeMetadata: true } });
  assert.ok(!has(expanded, 'FEATURECUSTOM'));
});

test('removeMetadata is refused with a warning while preserving PDF/A', opts, async () => {
  const { result, output } = await run(pdfaPdf(), {
    preset: 'medium',
    options: { removeMetadata: true, preserveConformance: true },
  });
  assert.ok(result.warnings.some((w) => /removeMetadata refused/.test(w)));
  assert.ok(has(expandPdf(output), 'pdfaid:part'));
});

test('removeBookmarks, removeAttachments and removePrintSettings clean the catalog', opts, async () => {
  const { expanded } = await run(featurePdf(), {
    preset: 'medium',
    options: { removeBookmarks: true, removeAttachments: true, removePrintSettings: true },
  });
  assert.ok(!has(expanded, '/Outlines'));
  assert.ok(!has(expanded, '/EmbeddedFiles'));
  assert.ok(!has(expanded, '/PrintScaling'));
  const kept = await run(featurePdf(), { preset: 'medium' });
  for (const m of ['/Outlines', '/EmbeddedFiles', '/PrintScaling']) assert.ok(has(kept.expanded, m), m);
});

test('removeAttachments drops file attachment annotations and removePrintSettings keeps other preferences', needsNewWorker, async () => {
  const { expanded } = await run(featurePdf(), {
    preset: 'medium',
    options: { removeAttachments: true, removePrintSettings: true },
  });
  assert.ok(!has(expanded, '/FileAttachment'));
  assert.ok(!has(expanded, 'FEATUREATTACH'));
  assert.ok(has(expanded, '/DisplayDocTitle'));
});

test('removeActions drops the open action', opts, async () => {
  const { expanded } = await run(featurePdf(), { preset: 'medium', options: { removeActions: true } });
  assert.ok(!has(expanded, '/OpenAction'));
});

test('removeActions drops document scripts and launch actions but keeps plain links', needsNewWorker, async () => {
  const { expanded } = await run(featurePdf(), { preset: 'medium', options: { removeActions: true } });
  assert.ok(!has(expanded, 'FEATUREJS'));
  assert.ok(!has(expanded, '/Launch'));
  assert.ok(!has(expanded, '/AA'));
  assert.equal(countMarker(expanded, '/Subtype /Link'), 2);
});

test('removeNamedDests resolves links and bookmarks before dropping the names', needsNewWorker, async () => {
  const { expanded } = await run(featurePdf(), { preset: 'medium', options: { removeNamedDests: true } });
  assert.ok(!has(expanded, '(target)'));
  assert.ok(!has(expanded, '/Dests'));
  assert.ok(countMarker(expanded, '/Fit') >= 3, 'link, bookmark and open action point at the page');
});

test('annotations flatten keeps forms and links (the old removeAnnots behaviour)', opts, async () => {
  for (const options of [{ annotations: 'flatten' as const }, { removeAnnots: true }]) {
    const { expanded } = await run(featurePdf(), { preset: 'medium', options });
    assert.ok(!has(expanded, '/Subtype /Text'), JSON.stringify(options));
    assert.equal(countMarker(expanded, '/Subtype /Link'), 2);
  }
});

test('annotations remove deletes markup and keeps links', needsNewWorker, async () => {
  const { expanded } = await run(featurePdf(), { preset: 'medium', options: { annotations: 'remove' } });
  assert.ok(!has(expanded, 'FEATURENOTE'));
  assert.equal(countMarker(expanded, '/Subtype /Link'), 2);
});

test('jpegProgressive writes progressive JPEG images', opts, async () => {
  const sof2 = (b: Uint8Array) => {
    for (let i = 0; i + 1 < b.length; i++) if (b[i] === 0xff && b[i + 1] === 0xc2) return true;
    return false;
  };
  const prog = await run(imagePdf(), { preset: 'medium', options: { jpegProgressive: true, preferJpx: false } });
  assert.ok(has(prog.output, '/DCTDecode') && sof2(prog.output));
});

test('colorTarget cmyk converts page text and vector colours', needsNewWorker, async () => {
  const { expanded, result } = await run(featurePdf(), { preset: 'medium', options: { colorTarget: 'cmyk' } });
  assert.ok(result.compressedSize <= result.originalSize);
  assert.ok(!/\b(rg|RG)\n/.test(new TextDecoder('latin1').decode(expanded)), 'no RGB colour operators left');
  assert.ok(has(expanded, '0 0 0 1 K'), 'K-only black stays K-only');
});

test('colorTarget rgb converts CMYK images and gray converts vectors', needsNewWorker, async () => {
  const rgb = await run(cmykImagePdf(), { preset: 'medium', options: { colorTarget: 'rgb' } });
  assert.ok(!has(rgb.expanded, '/DeviceCMYK'));
  const gray = await run(featurePdf(), { preset: 'medium', options: { colorTarget: 'gray' } });
  const body = new TextDecoder('latin1').decode(gray.expanded);
  assert.ok(!/\b(rg|RG|k|K)\n/.test(body), 'only gray colour operators left');
});

test('maxDpiMono downsamples 1-bit images and keeps them 1-bit', needsNewWorker, async () => {
  const width = (b: Uint8Array) => Number(/\/Width (\d+)/.exec(new TextDecoder('latin1').decode(b))?.[1]);
  const off = await run(monoScanPdf(), { preset: 'medium' });
  assert.equal(width(off.expanded), 2400);
  const on = await run(monoScanPdf(), { preset: 'medium', options: { maxDpiMono: 150 } });
  assert.ok(width(on.expanded) < 1400, `width ${width(on.expanded)}`);
  assert.ok(has(on.expanded, '/BitsPerComponent 1'));
});

test('speed fast never tries JPEG 2000', needsNewWorker, async () => {
  const { output } = await run(imagePdf(), { preset: 'medium', options: { preferJpx: true, speed: 'fast' } });
  assert.ok(!has(output, '/JPXDecode'));
});
