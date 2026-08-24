import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { compress, type CompressInput } from '../sdk/node/engine.js';
import {
  hasEngine,
  textPdf,
  richTextPdf,
  imagePdf,
  expandPdf,
  signedPdf,
  pdfaPdf,
  countMarker,
} from './helpers.js';

const engine = hasEngine();
const opts = { skip: engine ? false : 'built worker/qpdf not present' };

async function withTemp<T>(fn: (dir: string) => Promise<T>): Promise<T> {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-test-'));
  try {
    return await fn(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

type RunOptions = Omit<CompressInput, 'sourcePath' | 'savePath'>;

async function run(bytes: Uint8Array, input: RunOptions) {
  return withTemp(async (dir) => {
    const src = path.join(dir, 'in.pdf');
    const out = path.join(dir, 'out.pdf');
    writeFileSync(src, bytes);
    const result = await compress({ sourcePath: src, savePath: out, ...input });
    return { result, output: readFileSync(out) };
  });
}

test('every preset produces a valid, no-larger PDF', opts, async () => {
  for (const preset of ['low', 'medium', 'high', 'lossless'] as const) {
    const { result, output } = await run(imagePdf(), { preset });
    assert.equal(
      new TextDecoder().decode(output.subarray(0, 5)),
      '%PDF-',
      `${preset} output must be a PDF`,
    );
    assert.ok(
      result.compressedSize <= result.originalSize,
      `${preset} must never grow (${result.compressedSize} > ${result.originalSize})`,
    );
    assert.equal(result.signed, false);
  }
});

test('lossless run keeps a text document byte-valid and no larger', opts, async () => {
  const { result, output } = await run(textPdf(), { preset: 'lossless' });
  assert.equal(new TextDecoder().decode(output.subarray(0, 5)), '%PDF-');
  assert.ok(result.compressedSize <= result.originalSize);
});

test('brotli marks the file PDF 2.0 and writes BrotliDecode streams', opts, async () => {
  const { output } = await run(richTextPdf(), {
    preset: 'lossless',
    options: { brotli: true },
  });
  assert.ok(
    countMarker(output, '/BrotliDecode') > 0,
    'expected at least one BrotliDecode stream',
  );
  const expanded = expandPdf(output);
  assert.ok(
    countMarker(expanded, 'DeveloperExtensions') > 0,
    'expected the developer-extensions entry in the catalog',
  );
  assert.ok(
    countMarker(expanded, '/Version') > 0 &&
      countMarker(expanded, '/2.0') > 0,
    'expected the catalog to be marked PDF 2.0',
  );
});

test('default run emits no BrotliDecode and stays PDF 1.x', opts, async () => {
  const { output } = await run(textPdf(), { preset: 'lossless' });
  assert.equal(countMarker(output, '/BrotliDecode'), 0);
});

test('brotli is suppressed while preserving PDF/A', opts, async () => {
  const { output } = await run(pdfaPdf(), {
    preset: 'lossless',
    options: { brotli: true, preserveConformance: true },
  });
  assert.equal(
    countMarker(output, '/BrotliDecode'),
    0,
    'a conforming file must not gain a non-ISO filter',
  );
});

test('a signed document is returned byte-for-byte', opts, async () => {
  const input = signedPdf();
  const { result, output } = await run(input, { preset: 'high' });
  assert.equal(result.signed, true);
  assert.deepEqual(output, Buffer.from(input));
});

test('target size is met on an image-heavy document', opts, async () => {
  const input = imagePdf();
  const target = 60 * 1024;
  const { result } = await run(input, { preset: 'medium', targetSizeBytes: target });
  assert.equal(result.metTarget, true);
  assert.ok(result.compressedSize <= target);
});

test('an unreachable target reports best effort, not failure', opts, async () => {
  const { result } = await run(textPdf(), {
    preset: 'lossless',
    targetSizeBytes: 64,
  });
  assert.equal(result.metTarget, false);
  assert.ok(result.compressedSize > 0);
});

test('metTarget is null when no target is requested', opts, async () => {
  const { result } = await run(textPdf(), { preset: 'medium' });
  assert.equal(result.metTarget, null);
});
