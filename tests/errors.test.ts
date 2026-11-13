import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { compress, HyperError } from '../sdk/node/index.js';
import { hasEngine, imagePdf, prebuiltBin, textPdf } from './helpers.js';

const engine = hasEngine();
const opts = { skip: engine ? false : 'built worker/qpdf not present' };

test('a missing sourcePath rejects with a typed error', async () => {
  await assert.rejects(
    () => compress({ sourcePath: '' }),
    (err: unknown) => err instanceof HyperError && err.code === 'engine_error',
  );
});

test('a wrong password on an encrypted file rejects as decrypt_failed', opts, async () => {
  const { spawnSync } = await import('node:child_process');
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-enc-'));
  try {
    const plain = path.join(dir, 'plain.pdf');
    const enc = path.join(dir, 'enc.pdf');
    const out = path.join(dir, 'out.pdf');
    writeFileSync(plain, textPdf());
    const qpdf = process.env.HYPER_QPDF ?? prebuiltBin('qpdf');
    const r = spawnSync(qpdf, ['--encrypt', 'user', 'owner', '256', '--', plain, enc]);
    if (r.status !== 0) {
      return;
    }
    await assert.rejects(
      () => compress({ sourcePath: enc, savePath: out, password: 'wrong' }),
      (err: unknown) => err instanceof HyperError && err.code === 'decrypt_failed',
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a correct password decrypts and compresses through the stdin path', opts, async () => {
  const { spawnSync } = await import('node:child_process');
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-enc-ok-'));
  try {
    const plain = path.join(dir, 'plain.pdf');
    const enc = path.join(dir, 'enc.pdf');
    const out = path.join(dir, 'out.pdf');
    writeFileSync(plain, textPdf());
    const qpdf = process.env.HYPER_QPDF ?? prebuiltBin('qpdf');
    const r = spawnSync(qpdf, ['--encrypt', 'user', 'owner', '256', '--', plain, enc]);
    if (r.status !== 0) {
      return;
    }
    const result = await compress({ sourcePath: enc, savePath: out, password: 'user' });
    assert.ok(result.compressedSize > 0);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('an exhausted total-timeout budget surfaces as timeout, not decrypt_failed', opts, async () => {
  const { spawnSync } = await import('node:child_process');
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-enc-to-'));
  try {
    const plain = path.join(dir, 'plain.pdf');
    const enc = path.join(dir, 'enc.pdf');
    const out = path.join(dir, 'out.pdf');
    writeFileSync(plain, textPdf());
    const qpdf = process.env.HYPER_QPDF ?? prebuiltBin('qpdf');
    const r = spawnSync(qpdf, ['--encrypt', 'user', 'owner', '256', '--', plain, enc]);
    if (r.status !== 0) {
      return;
    }
    await assert.rejects(
      () => compress({ sourcePath: enc, savePath: out, password: 'user', totalTimeoutMs: 1 }),
      (err: unknown) => err instanceof HyperError && err.code === 'timeout',
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('corrupt input degrades to the original bytes, not a throw', opts, async () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-bad-'));
  try {
    const src = path.join(dir, 'bad.pdf');
    const out = path.join(dir, 'out.pdf');
    const garbage = Buffer.from('%PDF-1.7\nnot a real pdf body\n%%EOF');
    writeFileSync(src, garbage);
    const result = await compress({ sourcePath: src, savePath: out });
    assert.ok(result.compressedSize > 0);
    assert.ok(result.compressedSize <= result.originalSize);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('an already aborted signal rejects as cancelled without running', opts, async () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-abort-'));
  try {
    const src = path.join(dir, 'in.pdf');
    writeFileSync(src, textPdf());
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(
      () => compress({ sourcePath: src, savePath: path.join(dir, 'out.pdf'), signal: controller.signal }),
      (err: unknown) => err instanceof HyperError && err.code === 'cancelled',
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('aborting mid-run stops the work and rejects as cancelled', opts, async () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-abort-'));
  try {
    const src = path.join(dir, 'in.pdf');
    writeFileSync(src, imagePdf(2400, 2400));
    const controller = new AbortController();
    const started = Date.now();
    setTimeout(() => controller.abort(), 150);
    await assert.rejects(
      () => compress({
        sourcePath: src,
        savePath: path.join(dir, 'out.pdf'),
        targetSizeBytes: 100,
        options: { speed: 'quality' },
        signal: controller.signal,
      }),
      (err: unknown) => err instanceof HyperError && err.code === 'cancelled',
    );
    assert.ok(Date.now() - started < 5000, `took ${Date.now() - started} ms to stop`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a stage that runs past timeoutMs is a timeout error, not a silent fallback', opts, async () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-stage-'));
  try {
    const src = path.join(dir, 'in.pdf');
    writeFileSync(src, imagePdf(2400, 2400));
    await assert.rejects(
      () => compress({ sourcePath: src, savePath: path.join(dir, 'out.pdf'), timeoutMs: 20, options: { speed: 'quality' } }),
      (err: unknown) => err instanceof HyperError && err.code === 'timeout' && /stage limit/.test(err.message),
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('verifyPassword on a missing file is an error, not false', opts, async () => {
  const { verifyPassword } = await import('../sdk/node/index.js');
  await assert.rejects(
    () => verifyPassword(path.join(tmpdir(), 'hyper-no-such-file.pdf'), 'x'),
    (err: unknown) => err instanceof HyperError && err.code === 'engine_error',
  );
});
