import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { parse, version } from '../cli/bin/hyper.js';

test('target-size accepts unit suffixes', () => {
  assert.equal(parse(['a', 'b', '--target-size', '2MB']).targetSizeBytes, 2 * 1024 * 1024);
  assert.equal(parse(['a', 'b', '--target-size', '500KB']).targetSizeBytes, 500 * 1024);
  assert.equal(parse(['a', 'b', '--target-size', '1234']).targetSizeBytes, 1234);
});

test('--brotli maps onto the option override', () => {
  assert.equal(parse(['a', 'b', '--brotli']).overrides.brotli, true);
});

test('--set coerces booleans and numbers', () => {
  const p = parse(['a', 'b', '--set', 'grayscale=true', '--set', 'imageQuality=55']);
  assert.equal(p.overrides.grayscale, true);
  assert.equal(p.overrides.imageQuality, 55);
});

test('--help and --version are recognised, long and short', () => {
  for (const flag of ['--help', '-h']) {
    assert.equal(parse([flag]).help, true, `${flag} should request help`);
  }
  for (const flag of ['--version', '-v', '-V']) {
    assert.equal(parse([flag]).version, true, `${flag} should request the version`);
  }
  const plain = parse(['a', 'b']);
  assert.equal(plain.help, false);
  assert.equal(plain.version, false);
});

test('version() reports the package version', () => {
  const pkg = createRequire(import.meta.url)('../../package.json') as { version: string };
  assert.equal(version(), pkg.version);
  assert.match(version(), /^\d+\.\d+\.\d+$/);
});

test('the bin runs when invoked through a symlink, as npm installs it', async () => {
  const { spawnSync } = await import('node:child_process');
  const { mkdtempSync, symlinkSync, rmSync } = await import('node:fs');
  const { tmpdir } = await import('node:os');
  const path = (await import('node:path')).default;
  const { fileURLToPath } = await import('node:url');
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-bin-'));
  try {
    const real = path.resolve(
      path.dirname(fileURLToPath(import.meta.url)), '..', 'cli', 'bin', 'hyper.js',
    );
    const link = path.join(dir, 'hyper');
    symlinkSync(real, link);
    const r = spawnSync(process.execPath, [link], { encoding: 'utf8' });
    assert.equal(r.status, 2);
    assert.match(r.stderr, /usage: hyper/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the output path is optional and defaults beside the input', async () => {
  const { spawnSync } = await import('node:child_process');
  const { mkdtempSync, copyFileSync, existsSync, rmSync } = await import('node:fs');
  const { tmpdir } = await import('node:os');
  const path = (await import('node:path')).default;
  const { fileURLToPath } = await import('node:url');
  const { hasEngine } = await import('./helpers.js');
  if (!hasEngine()) return;

  const here = path.dirname(fileURLToPath(import.meta.url));
  const root = path.resolve(here, '..', '..');
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-defaultout-'));
  try {
    const src = path.join(dir, 'report.pdf');
    copyFileSync(
      path.join(root, 'tests', 'regression', 'failset', 'govdocs001-0142b23c0caa.pdf'),
      src,
    );
    const r = spawnSync(process.execPath, [path.join(root, 'dist', 'cli', 'bin', 'hyper.js'), src], {
      encoding: 'utf8',
    });
    assert.equal(r.status, 0, `expected success, got: ${r.stderr}`);
    assert.ok(
      existsSync(path.join(dir, 'report-compressed.pdf')),
      'expected report-compressed.pdf beside the input',
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
