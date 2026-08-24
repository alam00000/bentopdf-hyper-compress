import HyperCompressModule from '../wasm/out/hyper-compress.js';
import { compressBuffer, decryptBuffer, describePdfaOutcome } from '../dist/wasm/glue.js';

let modPromise = null;

function readLastError(mod) {
  try {
    const ptr = mod._hyper_last_error();
    if (!ptr) return '';
    let s = '';
    for (let i = ptr; mod.HEAPU8[i]; i++) s += String.fromCharCode(mod.HEAPU8[i]);
    return s.replace(/^in\.pdf:\s*/, '');
  } catch {
    return '';
  }
}

async function getModule() {
  if (!modPromise) modPromise = HyperCompressModule();
  return modPromise;
}

self.onmessage = async (e) => {
  const { id, bytes, jobs, password, targetSizeBytes, verify } = e.data;
  if (verify) {
    let valid;
    try {
      const mod = await getModule();
      valid = !!decryptBuffer(mod, new Uint8Array(bytes), password || '');
    } catch {
      valid = false;
    }
    self.postMessage({ id, ok: true, verifyValid: !!valid });
    return;
  }
  try {
    const mod = await getModule();
    const results = [];

    for (const job of jobs) {
      const started = performance.now();
      const input = new Uint8Array(bytes.slice(0));
      let res;
      try {
        res = compressBuffer(mod, input, {
          options: job.options,
          targetSizeBytes,
          password: password || null,
        });
      } catch (err) {
        const detail = readLastError(mod);
        results.push({
          preset: job.label,
          error: detail || err.message || String(err),
        });
        continue;
      }
      results.push({
        preset: job.label,
        originalSize: res.originalSize,
        compressedSize: res.compressedSize,
        signed: res.signed,
        pdfa: describePdfaOutcome(res),
        pdfaOutcome: res.pdfaOutcome,
        ms: Math.round(performance.now() - started),
        data: res.data,
      });
    }

    const transfer = results.filter((r) => r.data).map((r) => r.data.buffer);
    self.postMessage({ id, ok: true, results }, transfer);
  } catch (err) {
    self.postMessage({ id, ok: false, error: err.message || String(err) });
  }
};

getModule().then(
  () => self.postMessage({ ready: true }),
  (err) => self.postMessage({ ready: false, error: err.message || String(err) }),
);
