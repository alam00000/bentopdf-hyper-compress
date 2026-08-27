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
  assert.equal(o.imageQuality, 5);
  assert.equal(o.maxDpi, 600);
});

test('image quality clamps to the engine floor, not to a preset floor', () => {
  assert.equal(normalizeHyperOptions({ imageQuality: 0 }).imageQuality, 5);
  assert.equal(normalizeHyperOptions({ imageQuality: -10 }).imageQuality, 5);
  assert.equal(normalizeHyperOptions({ imageQuality: 12 }).imageQuality, 12);
  assert.equal(normalizeHyperOptions({ imageQuality: 200 }).imageQuality, 100);
});

test('the target ladder ends below every preset, so a target can go further', async () => {
  const { TARGET_QUALITY_FLOOR, TARGET_DPI_LADDER, targetLadder, targetStartQuality } =
    await import('../sdk/node/target.js');
  const { HYPER_PRESETS } = await import('../sdk/node/presets.js');

  for (const preset of Object.values(HYPER_PRESETS)) {
    assert.ok(
      preset.imageQuality > TARGET_QUALITY_FLOOR,
      'no preset should sit at the target floor',
    );
  }

  const ladder = targetLadder(HYPER_PRESETS.medium);
  assert.equal(ladder.length, TARGET_DPI_LADDER.length);
  for (const rung of ladder) {
    assert.equal(rung.imageQuality, TARGET_QUALITY_FLOOR);
    assert.equal(rung.forceDownsample, true);
  }
  const dpis = ladder.map((r) => r.maxDpi);
  assert.deepEqual(dpis, [...dpis].sort((a, b) => b - a), 'ladder must descend');
  assert.ok(dpis[dpis.length - 1]! < HYPER_PRESETS.high.maxDpi);

  assert.equal(targetStartQuality(HYPER_PRESETS.medium), HYPER_PRESETS.medium.imageQuality);
  assert.ok(targetStartQuality(HYPER_PRESETS.lossless) <= 95);
});
