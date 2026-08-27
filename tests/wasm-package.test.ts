import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');
const WASM_MODULE = path.join(ROOT, 'wasm', 'out', 'hyper-compress.wasm');
const PKG = path.join(ROOT, 'packages', 'npm', 'hyper-compress-wasm');
const FIXTURE = path.join(
  ROOT, 'tests', 'regression', 'failset', 'govdocs001-0142b23c0caa.pdf',
);

const opts = {
  skip: existsSync(WASM_MODULE) ? false : 'wasm/out not built',
};

test('the assembled wasm package imports and compresses', opts, async () => {
  execFileSync('bash', [path.join(ROOT, 'scripts', 'pack-wasm-npm.sh')], {
    stdio: 'pipe',
  });
  for (const f of [
    'index.js', 'index.d.ts', 'LICENSE', 'README.md', 'package.json',
    'engine/hyper-compress.js', 'engine/hyper-compress.wasm',
    'lib/wasm/sdk.js', 'lib/wasm/glue.js',
    'lib/sdk/node/options.js', 'lib/sdk/node/presets.js',
    'lib/sdk/node/pdfa.js', 'lib/sdk/node/errors.js', 'lib/sdk/node/target.js',
  ]) {
    assert.ok(existsSync(path.join(PKG, f)), `package is missing ${f}`);
  }
  interface PackageSurface {
    compress(input: Uint8Array, opts?: { preset?: string }): Promise<{
      originalSize: number;
      compressedSize: number;
      data: Uint8Array;
      warnings: string[];
    }>;
    verifyPassword(input: Uint8Array, password: string): Promise<boolean>;
  }
  const pkg = (await import(
    pathToFileURL(path.join(PKG, 'index.js')).href
  )) as PackageSurface;
  const input = new Uint8Array(readFileSync(FIXTURE));
  const result = await pkg.compress(input, { preset: 'medium' });
  assert.ok(result.compressedSize > 0);
  assert.ok(result.compressedSize <= result.originalSize);
  assert.equal(result.originalSize, input.length);
  assert.deepEqual(
    Array.from(result.data.slice(0, 5)),
    Array.from(Buffer.from('%PDF-')),
  );
  assert.ok(Array.isArray(result.warnings));
  assert.equal(typeof (await pkg.verifyPassword(input, 'x')), 'boolean');
});
