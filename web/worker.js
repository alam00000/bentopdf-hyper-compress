import HyperCompressModule from '../wasm/out/hyper-compress.js';
import { describePdfaOutcome } from '../dist/wasm/glue.js';
import { createEngine } from '../dist/wasm/sdk.js';

const engine = createEngine(() => HyperCompressModule());

self.onmessage = async (e) => {
  const { id, bytes, jobs, password, targetSizeBytes, verify } = e.data;
  if (verify) {
    let valid = false;
    try {
      valid = await engine.verifyPassword(new Uint8Array(bytes), password || '');
    } catch {}
    self.postMessage({ id, ok: true, verifyValid: valid });
    return;
  }
  try {
    const results = [];

    for (const job of jobs) {
      const started = performance.now();
      let res;
      try {
        res = await engine.compress(new Uint8Array(bytes), {
          options: job.options,
          targetSizeBytes,
          password: password || null,
        });
      } catch (err) {
        results.push({
          preset: job.label,
          error: (err && err.message) || String(err),
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
        warnings: res.warnings,
        ms: Math.round(performance.now() - started),
        data: res.data,
      });
    }

    const transfer = results.filter((r) => r.data).map((r) => r.data.buffer);
    self.postMessage({ id, ok: true, results }, transfer);
  } catch (err) {
    self.postMessage({ id, ok: false, error: (err && err.message) || String(err) });
  }
};

engine.init().then(
  () => self.postMessage({ ready: true }),
  (err) => self.postMessage({ ready: false, error: (err && err.message) || String(err) }),
);
