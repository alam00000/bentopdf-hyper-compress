import { test } from 'node:test';
import assert from 'node:assert/strict';
import { loadNative } from '../sdk/node/engine.js';
import { buildHyperTokens, normalizeHyperOptions, HYPER_OPTION_DOCS } from '../sdk/node/options.js';
import { HYPER_PRESETS, type CompressLevel } from '../sdk/node/presets.js';

let native: ReturnType<typeof loadNative> | null = null;
try {
  native = loadNative();
} catch {
  native = null;
}
const opts = { skip: native ? false : 'native addon not built' };

function rng(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 2 ** 32;
  };
}

const ODD: unknown[] = [null, 'x', '', -1, 0, 1, 2.5, 1e9, -1e9, true, false, 420, 1.7, 2, '420', '1.7', 'force', NaN];

function randomOverrides(r: () => number): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [key, doc] of Object.entries(HYPER_OPTION_DOCS)) {
    if (r() > 0.35) continue;
    const roll = r();
    if (roll < 0.15) {
      out[key] = ODD[Math.floor(r() * ODD.length)];
    } else if (doc.type === 'boolean') {
      out[key] = r() < 0.5;
    } else if (doc.type === 'enum') {
      const values = doc.values ?? [];
      out[key] = values[Math.floor(r() * values.length)];
    } else {
      const lo = doc.min ?? 0;
      const hi = doc.max ?? 100;
      out[key] = Math.round((lo - 10 + r() * (hi - lo + 20)) * (r() < 0.2 ? 1.37 : 1));
    }
  }
  return out;
}

test('native options and tokens match the TypeScript ones for every preset', opts, () => {
  for (const preset of Object.keys(HYPER_PRESETS) as CompressLevel[]) {
    const tsOpts = normalizeHyperOptions(HYPER_PRESETS[preset]);
    assert.deepEqual(native!.resolveOptionsNative(preset, {}), tsOpts, preset);
    assert.deepEqual(native!.buildTokensNative(tsOpts), buildHyperTokens(tsOpts), preset);
  }
});

test('native options and tokens match the TypeScript ones on random input', opts, () => {
  const r = rng(20261113);
  const presets = Object.keys(HYPER_PRESETS) as CompressLevel[];
  for (let i = 0; i < 3000; i++) {
    const preset = presets[i % presets.length] as CompressLevel;
    const overrides = JSON.parse(JSON.stringify(randomOverrides(r))) as Record<string, unknown>;
    const tsOpts = normalizeHyperOptions({ ...HYPER_PRESETS[preset], ...overrides });
    const rsOpts = native!.resolveOptionsNative(preset, overrides);
    assert.deepEqual(rsOpts, tsOpts, `case ${i}: ${JSON.stringify(overrides)}`);
    assert.deepEqual(native!.buildTokensNative(tsOpts), buildHyperTokens(tsOpts), `case ${i}`);
  }
});
