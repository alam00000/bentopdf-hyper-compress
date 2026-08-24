import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildHyperTokens, normalizeHyperOptions } from '../sdk/node/options.js';
import { HYPER_PRESETS } from '../sdk/node/presets.js';

test('presets normalize without brotli and stay stable', () => {
  for (const name of ['low', 'medium', 'high', 'lossless'] as const) {
    const o = normalizeHyperOptions(HYPER_PRESETS[name]);
    assert.equal(o.brotli, false, `${name} must not enable brotli by default`);
    const tokens = buildHyperTokens(o);
    assert.ok(!tokens.includes('65=1'), `${name} must not emit the codec token`);
  }
});

test('brotli emits codec + quality tokens', () => {
  const o = normalizeHyperOptions({ ...HYPER_PRESETS.lossless, brotli: true });
  const tokens = buildHyperTokens(o);
  assert.ok(tokens.includes('65=1'));
  assert.ok(tokens.includes('66=11'));
});

test('preserving PDF/A forces brotli off', () => {
  const o = normalizeHyperOptions({
    ...HYPER_PRESETS.lossless,
    brotli: true,
    preserveConformance: true,
  });
  const tokens = buildHyperTokens(o);
  assert.ok(!tokens.includes('65=1'), 'codec token must not appear while preserving');
  assert.ok(tokens.includes('64=3'), 'the PDF/A mode token must still appear');
});

test('numeric clamps hold', () => {
  const o = normalizeHyperOptions({ imageQuality: 5, maxDpi: 100000 });
  assert.equal(o.imageQuality, 20);
  assert.equal(o.maxDpi, 600);
});
