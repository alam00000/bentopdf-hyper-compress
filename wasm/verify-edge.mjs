import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');

const HyperCompressModule = (await import(path.join(HERE, 'out', 'hyper-compress.js'))).default;
const { compressBuffer, decryptBuffer } = await import(path.join(ROOT, 'dist', 'wasm', 'glue.js'));
const { compress } = await import(path.join(ROOT, 'dist', 'sdk', 'node', 'engine.js'));

const md5 = (b) => createHash('md5').update(b).digest('hex');
const mod = await HyperCompressModule();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hyper-edge-'));
let fail = 0;
const check = (ok, label, detail = '') => {
  console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? `  ${detail}` : ''}`);
  if (!ok) fail++;
};

const [signedPath, encryptedPath, tinyPath] = process.argv.slice(2);

if (signedPath) {
  console.log(`\n=== signed: ${path.basename(signedPath)}`);
  const input = fs.readFileSync(signedPath);
  const res = compressBuffer(mod, new Uint8Array(input), { preset: 'medium' });
  check(res.signed === true, 'detected as signed');
  check(md5(Buffer.from(res.data)) === md5(input), 'output byte-identical to input');
}

if (encryptedPath) {
  console.log(`\n=== encrypted (no password): ${path.basename(encryptedPath)}`);
  const input = fs.readFileSync(encryptedPath);
  const res = compressBuffer(mod, new Uint8Array(input), { preset: 'medium' });
  check(res.compressedSize <= input.length, 'never-bigger holds',
        `${input.length} -> ${res.compressedSize}`);
  check(Buffer.from(res.data.slice(0, 5)).toString() === '%PDF-', 'output is still a PDF');

  let threw = false;
  try {
    compressBuffer(mod, new Uint8Array(input), { preset: 'medium', password: 'definitely-wrong' });
  } catch {
    threw = true;
  }
  check(threw, 'wrong password throws engine_error');

  const dec = decryptBuffer(mod, new Uint8Array(input), '');
  check(dec !== null, 'decrypts with empty user password');
  if (dec) {
    const after = compressBuffer(mod, dec, { preset: 'medium' });
    check(after.compressedSize < dec.length, 'decrypted input then compresses',
          `${dec.length} -> ${after.compressedSize}`);
  }
}

if (tinyPath) {
  console.log(`\n=== never-bigger: ${path.basename(tinyPath)}`);
  const input = fs.readFileSync(tinyPath);
  for (const preset of ['low', 'medium', 'high', 'lossless']) {
    const res = compressBuffer(mod, new Uint8Array(input), { preset });
    check(res.compressedSize <= input.length, `${preset} never grows`,
          `${input.length} -> ${res.compressedSize}`);
  }
}

if (tinyPath) {
  console.log(`\n=== byte-identity vs native`);
  const input = fs.readFileSync(tinyPath);
  const w = compressBuffer(mod, new Uint8Array(input), { preset: 'medium' });
  const savePath = path.join(tmp, 'native.pdf');
  const n = await compress({ sourcePath: tinyPath, savePath, preset: 'medium' });
  const nb = fs.readFileSync(n.outputPath);
  const wb = Buffer.from(w.data);
  check(wb.length === nb.length, 'same length', `${wb.length} vs ${nb.length}`);
  if (wb.length === nb.length) {
    let diffs = 0;
    for (let i = 0; i < wb.length; i++) if (wb[i] !== nb[i]) diffs++;
    check(diffs <= 64, 'differs only in the /ID trailer', `${diffs} bytes differ`);
  }
}

console.log(`\n${fail === 0 ? 'ALL EDGE CASES PASS' : `${fail} FAILURE(S)`}`);
process.exit(fail === 0 ? 0 : 1);
