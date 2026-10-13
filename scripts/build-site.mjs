import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { cpSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const OUT = path.join(ROOT, 'site');
const { version } = JSON.parse(readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
const RELEASE = `https://github.com/alam00000/bentopdf-hyper-compress/releases/download/v${version}`;
const TARBALL = `hyper-compress-v${version}-wasm.tar.gz`;

const download = async (url) => {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
};

execFileSync('npm', ['run', 'docs:build'], { cwd: ROOT, stdio: 'inherit' });

rmSync(OUT, { recursive: true, force: true });
cpSync(path.join(ROOT, 'web'), OUT, {
  recursive: true,
  filter: (src) => !['serve.mjs', 'engine-http.js'].includes(path.basename(src)),
});
for (const dir of ['sdk', 'wasm']) {
  cpSync(path.join(ROOT, 'dist', dir), path.join(OUT, 'dist', dir), { recursive: true });
}
cpSync(path.join(ROOT, 'docs', '.vitepress', 'dist'), path.join(OUT, 'docs'), { recursive: true });

const [sums, tarball] = await Promise.all([download(`${RELEASE}/SHA256SUMS`), download(`${RELEASE}/${TARBALL}`)]);
const expected = sums
  .toString('utf8')
  .split('\n')
  .find((line) => line.trim().endsWith(TARBALL))
  ?.split(/\s+/)[0];
if (!expected || createHash('sha256').update(tarball).digest('hex') !== expected) {
  throw new Error(`${TARBALL}: checksum mismatch against SHA256SUMS`);
}
const engineDir = path.join(OUT, 'wasm', 'out');
mkdirSync(engineDir, { recursive: true });
writeFileSync(path.join(engineDir, TARBALL), tarball);
execFileSync('tar', ['-xzf', TARBALL], { cwd: engineDir });
rmSync(path.join(engineDir, TARBALL));

for (const f of readdirSync(OUT).filter((f) => f.endsWith('.js'))) {
  const file = path.join(OUT, f);
  const src = readFileSync(file, 'utf8').replaceAll("'../dist/", "'./dist/").replaceAll("'../wasm/out/", "'./wasm/out/");
  if (src.includes("'../")) throw new Error(`unrewritten parent import in ${f}`);
  writeFileSync(file, src);
}

console.log(`assembled ${OUT} (engine v${version})`);
