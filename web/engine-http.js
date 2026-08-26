import { HYPER_PRESETS } from '../dist/sdk/node/presets.js';

export function createHttpEngine() {
  return {
    isReady: () => true,
    async verifyPassword(file) {
      const headers = {};
      if (file.password) headers['X-Password'] = file.password;
      try {
        const res = await fetch('/api/verify-password', { method: 'POST', headers, body: file.bytes });
        if (!res.ok) return false;
        return (await res.json()).valid === true;
      } catch {
        return false;
      }
    },
    async compress(file, jobs, targetSizeBytes) {
      const results = [];
      for (const job of jobs) {
        const params = new URLSearchParams({
          preset: HYPER_PRESETS[job.label] ? job.label : 'custom',
          options: JSON.stringify(job.options),
        });
        if (targetSizeBytes) params.set('targetSizeBytes', String(targetSizeBytes));
        const headers = {};
        if (file.encrypted && file.password) headers['X-Password'] = file.password;
        const started = performance.now();
        const res = await fetch(`/api/compress?${params}`, {
          method: 'POST', headers, body: file.bytes,
        });
        if (!res.ok) {
          let code = res.statusText;
          try { code = (await res.json()).error; } catch {}
          results.push({ preset: job.label, error: code });
          continue;
        }
        const blob = await res.blob();
        const data = new Uint8Array(await blob.arrayBuffer());
        let warnings = [];
        const warnRaw = res.headers.get('x-warnings');
        if (warnRaw) {
          try { warnings = JSON.parse(decodeURIComponent(warnRaw)); } catch {}
        }
        results.push({
          preset: job.label,
          originalSize: Number(res.headers.get('x-original-size')) || file.bytes.byteLength,
          compressedSize: Number(res.headers.get('x-compressed-size')) || data.length,
          signed: res.headers.get('x-signed') === 'true',
          warnings,
          ms: Math.round(performance.now() - started),
          data,
        });
      }
      return results;
    },
  };
}
