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

test('rasterizeText forces the rasterize pass and removeSignatures sets its discard bit', () => {
  const base = normalizeHyperOptions(HYPER_PRESETS.medium);
  const mask = (o: typeof base): number =>
    Number(buildHyperTokens(o).find((t) => t.startsWith('20='))?.slice(3) ?? 0);
  assert.ok(buildHyperTokens({ ...base, rasterizePages: true }).includes('60=1'));
  assert.ok(
    buildHyperTokens({ ...base, rasterizePages: true, rasterizeText: true }).includes('60=2'),
  );
  assert.ok(!buildHyperTokens({ ...base, rasterizeText: true }).some((t) => t.startsWith('60=')));
  assert.equal(mask({ ...base, removeSignatures: true }) & 0x400000, 0x400000);
  assert.equal(mask(base) & 0x400000, 0);
  for (const name of ['low', 'medium', 'high', 'lossless'] as const) {
    const o = normalizeHyperOptions(HYPER_PRESETS[name]);
    assert.equal(o.rasterizeText, false, `${name} must not force rasterizing`);
    assert.equal(o.removeSignatures, false, `${name} must not remove signatures`);
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

test('mrc maps onto the engine MRC tokens and stays off by default and when lossless', () => {
  for (const name of ['low', 'medium', 'high', 'lossless'] as const) {
    const tokens = buildHyperTokens(normalizeHyperOptions(HYPER_PRESETS[name]));
    assert.ok(!tokens.some((t) => t.startsWith('40=')), `${name} must not enable MRC`);
  }
  const auto = buildHyperTokens(normalizeHyperOptions({
    ...HYPER_PRESETS.medium,
    mrc: 'auto',
    mrcBackgroundDpi: 70,
    mrcBackgroundQuality: 25,
    mrcForegroundQuality: 25,
  }));
  for (const t of ['40=1', '42=70', '43=25', '44=25']) assert.ok(auto.includes(t), t);
  const force = buildHyperTokens(normalizeHyperOptions({ ...HYPER_PRESETS.medium, mrc: 'force' }));
  for (const t of ['40=2', '42=75', '43=30', '44=50']) assert.ok(force.includes(t), t);
  const lossless = buildHyperTokens(normalizeHyperOptions({ ...HYPER_PRESETS.lossless, mrc: 'force' }));
  assert.ok(!lossless.some((t) => t.startsWith('40=')));
  assert.equal(normalizeHyperOptions({ mrc: 'sometimes' }).mrc, 'off');
  assert.equal(normalizeHyperOptions({ mrcBackgroundDpi: 5 }).mrcBackgroundDpi, 36);
});

test('contentPrecision rounds only on lossy runs and clamps to 0 or 3..8', () => {
  const medium = buildHyperTokens(normalizeHyperOptions(HYPER_PRESETS.medium));
  assert.ok(medium.includes('67=6'));
  const lossless = buildHyperTokens(normalizeHyperOptions(HYPER_PRESETS.lossless));
  assert.ok(!lossless.some((t) => t.startsWith('67=')));
  assert.equal(normalizeHyperOptions({ contentPrecision: 1 }).contentPrecision, 3);
  assert.equal(normalizeHyperOptions({ contentPrecision: 0 }).contentPrecision, 0);
  assert.equal(normalizeHyperOptions({ contentPrecision: 20 }).contentPrecision, 8);
});
