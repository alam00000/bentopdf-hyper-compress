import { execFileSync } from 'node:child_process';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');
const PREBUILT = path.join(ROOT, 'cli', 'prebuilt');

const HyperCompressModule = (await import(path.join(HERE, 'out', 'hyper-compress.js'))).default;
const { compressBuffer } = await import(path.join(ROOT, 'dist', 'wasm', 'glue.js'));

const PRESETS = ['low', 'medium', 'high', 'lossless'];

async function nativeCompress(srcPath, preset, tmp) {
  const { compress } = await import(path.join(ROOT, 'dist', 'sdk', 'node', 'engine.js'));
  const savePath = path.join(tmp, `native-${preset}.pdf`);
  const res = await compress({ sourcePath: srcPath, savePath, preset });
  return { bytes: fs.readFileSync(res.outputPath), signed: res.signed };
}

function render(pdfPath, outPpm) {
  try {
    execFileSync(path.join(PREBUILT, 'hpdf-render'), [pdfPath, outPpm, '1.0'], {
      stdio: 'ignore',
      timeout: 120000,
    });
    return fs.existsSync(outPpm) ? fs.readFileSync(outPpm) : null;
  } catch {
    return null;
  }
}

function pixelDiff(a, b) {
  if (!a || !b) return null;
  if (a.length !== b.length) return 'size-mismatch';
  let sum = 0;
  for (let i = 0; i < a.length; i++) sum += Math.abs(a[i] - b[i]);
  return sum / a.length;
}

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error('usage: node wasm/verify.mjs <pdf...>');
  process.exit(2);
}

const mod = await HyperCompressModule();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hyper-verify-'));
let failures = 0;

for (const file of files) {
  const input = fs.readFileSync(file);
  console.log(`\n=== ${path.basename(file)}  (${(input.length / 1024).toFixed(0)} KB)`);

  for (const preset of PRESETS) {
    let line = `  ${preset.padEnd(9)}`;
    try {
      const wasmRes = compressBuffer(mod, new Uint8Array(input), { preset });
      const nat = await nativeCompress(file, preset, tmp);

      const wPct = (100 * (1 - wasmRes.compressedSize / input.length)).toFixed(1);
      const nPct = (100 * (1 - nat.bytes.length / input.length)).toFixed(1);
      line += ` wasm ${String(wasmRes.compressedSize).padStart(9)} (${wPct.padStart(5)}%)`;
      line += `  native ${String(nat.bytes.length).padStart(9)} (${nPct.padStart(5)}%)`;

      if (wasmRes.compressedSize > input.length) {
        line += '  NEVER-BIGGER VIOLATED';
        failures++;
      }

      const wPath = path.join(tmp, `wasm-${preset}.pdf`);
      fs.writeFileSync(wPath, wasmRes.data);
      const nPath = path.join(tmp, `native-${preset}.pdf`);
      fs.writeFileSync(nPath, nat.bytes);
      const wPpm = render(wPath, path.join(tmp, `wasm-${preset}.ppm`));
      const nPpm = render(nPath, path.join(tmp, `native-${preset}.ppm`));
      if (!wPpm) {
        line += '  WASM OUTPUT DID NOT RENDER';
        failures++;
      } else {
        const d = pixelDiff(wPpm, nPpm);
        line += typeof d === 'number' ? `  pixdiff ${d.toFixed(2)}` : `  pixdiff ${d}`;
        if (typeof d === 'number' && d > 2.0) failures++;
      }
      if (wasmRes.signed || nat.signed) line += '  [signed]';
    } catch (err) {
      line += `  ERROR ${err.message}`;
      failures++;
    }
    console.log(line);
  }
}

console.log(`\n${failures === 0 ? 'OK' : `${failures} FAILURE(S)`}  (scratch: ${tmp})`);
process.exit(failures === 0 ? 0 : 1);
