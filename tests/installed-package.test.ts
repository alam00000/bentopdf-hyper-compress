import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { hasEngine, textPdf } from './helpers.js';
import { writeFileSync } from 'node:fs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');

test('the packed tarball installs and works the way npm delivers it', async () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-installed-'));
  try {
    execFileSync('npm', ['pack', '--pack-destination', dir], {
      cwd: ROOT, stdio: 'pipe',
    });
    const tarball = readdirSync(dir).find((f) => f.endsWith('.tgz'));
    assert.ok(tarball, 'npm pack produced no tarball');
    const prefix = path.join(dir, 'prefix');
    execFileSync('npm', [
      'install', '-g', '--prefix', prefix, '--no-audit', '--no-fund',
      path.join(dir, tarball),
    ], { stdio: 'pipe' });

    const bin = path.join(prefix, 'bin', 'hyper');
    assert.ok(existsSync(bin), 'installed prefix has no hyper bin');

    const usage = spawnSync(bin, [], { encoding: 'utf8' });
    assert.equal(usage.status, 2, 'bare invocation must print usage, not exit silently');
    assert.match(usage.stderr, /usage: hyper/);

    const help = spawnSync(bin, ['--help'], { encoding: 'utf8' });
    assert.equal(help.status, 0, '--help must exit 0');
    assert.match(help.stdout, /usage: hyper/);
    assert.match(help.stdout, /--target-size/);

    const ver = spawnSync(bin, ['--version'], { encoding: 'utf8' });
    assert.equal(ver.status, 0, '--version must exit 0');
    assert.match(ver.stdout.trim(), /^\d+\.\d+\.\d+$/);

    const installedPkg = path.join(
      prefix, 'lib', 'node_modules', 'hyper-compress', 'dist', 'sdk', 'node', 'index.js',
    );
    const sdk = (await import(pathToFileURL(installedPkg).href)) as {
      resolveOptions: (input: { preset?: string }) => Record<string, unknown>;
      HYPER_OPTION_DOCS: Record<string, unknown>;
    };
    assert.equal(sdk.resolveOptions({ preset: 'high' }).imageQuality, 20);
    assert.equal(Object.keys(sdk.HYPER_OPTION_DOCS).length, 28);

    if (hasEngine()) {
      const src = path.join(dir, 'in.pdf');
      const out = path.join(dir, 'out.pdf');
      writeFileSync(src, textPdf());
      const run = spawnSync(bin, [src, out], {
        encoding: 'utf8',
        env: {
          ...process.env,
          HYPER_DRV: path.join(ROOT, 'cli', 'prebuilt', 'hpdf-worker'),
          HYPER_QPDF: path.join(ROOT, 'cli', 'prebuilt', 'qpdf'),
        },
      });
      assert.equal(run.status, 0, `compress failed: ${run.stderr}`);
      assert.ok(existsSync(out), 'installed cli wrote no output file');
      assert.match(run.stdout, /-> \d+ bytes/);
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
