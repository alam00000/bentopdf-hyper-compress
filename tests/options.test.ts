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

const medium = normalizeHyperOptions(HYPER_PRESETS.medium);
const tokensOf = (o: Partial<typeof medium>): string[] =>
  buildHyperTokens(normalizeHyperOptions({ ...HYPER_PRESETS.medium, ...o }));
const maskOf = (o: Partial<typeof medium>): number =>
  Number(tokensOf(o).find((t) => t.startsWith('20='))?.slice(3) ?? 0);

test('presets keep their engine tokens: the new options default to today\'s behaviour', () => {
  const expected: Record<string, string[]> = {
    low: ['1=200', '2=80', '3=4', '8=1', '18=1', '7=1', '63=10', '10=1', '13=1', '14=1', '16=1', '17=1', '20=8322', '67=6'],
    medium: ['1=150', '2=50', '3=4', '8=1', '18=1', '7=1', '63=10', '6=1', '10=1', '13=1', '14=1', '16=1', '17=1', '20=8322', '67=6'],
    high: ['1=72', '59=150', '2=20', '3=4', '8=1', '18=1', '7=1', '63=10', '6=1', '10=1', '12=1', '9=1', '13=1', '14=1', '16=1', '17=1', '20=565442', '67=6'],
    lossless: ['3=0', '7=1', '10=1', '13=1', '14=1', '16=1', '17=1', '20=8322'],
  };
  for (const name of ['low', 'medium', 'high', 'lossless'] as const) {
    assert.deepEqual(buildHyperTokens(normalizeHyperOptions(HYPER_PRESETS[name])), expected[name], name);
  }
});

test('colorTarget maps to the colour target and page colour conversion tokens', () => {
  assert.ok(!tokensOf({}).some((t) => t.startsWith('51=') || t.startsWith('69=')));
  for (const [target, id] of [['rgb', 1], ['cmyk', 2], ['gray', 3]] as const) {
    const t = tokensOf({ colorTarget: target });
    assert.ok(t.includes(`51=${id}`) && t.includes('69=1'), target);
  }
  assert.deepEqual(tokensOf({ grayscale: true }).filter((t) => /^(51|69)=/.test(t)), ['51=3']);
  assert.ok(!tokensOf({ colorTarget: 'cmyk', lossless: true }).some((t) => /^(51|69)=/.test(t)));
  assert.equal(normalizeHyperOptions({ colorTarget: 'lab' }).colorTarget, 'original');
});

test('per-class max dpi: maxDpi is the shorthand, classes override, mono is opt-in', () => {
  const plain = tokensOf({ maxDpi: 150 });
  assert.ok(plain.includes('1=150'));
  assert.ok(!plain.some((t) => /^(57|58|59)=/.test(t)));
  const split = tokensOf({ maxDpi: 150, maxDpiColor: 200, maxDpiGray: 100, maxDpiMono: 300 });
  for (const t of ['1=150', '57=200', '58=100', '59=300']) assert.ok(split.includes(t), t);
  assert.ok(!tokensOf({ maxDpi: 150, maxDpiColor: 150 }).some((t) => t.startsWith('57=')));
  const noGlobal = tokensOf({ maxDpi: 0, maxDpiGray: 120 });
  assert.ok(!noGlobal.some((t) => t.startsWith('1=')));
  assert.ok(noGlobal.includes('58=120'));
  assert.ok(!tokensOf({ maxDpiColor: 100, lossless: true }).some((t) => /^(1|57|58|59)=/.test(t)));
  assert.equal(normalizeHyperOptions({ maxDpiMono: 5000 }).maxDpiMono, 1200);
});

test('thresholdDpi only resamples above it and never below the limit', () => {
  assert.ok(!tokensOf({}).some((t) => t.startsWith('50=')), 'default keeps the built-in rule');
  assert.ok(tokensOf({ maxDpi: 150, thresholdDpi: 225 }).includes('50=225'));
  assert.ok(tokensOf({ maxDpi: 150, thresholdDpi: 100 }).includes('50=150'));
  assert.ok(tokensOf({ maxDpi: 150, forceDownsample: true }).includes('50=150'));
  assert.ok(tokensOf({ maxDpi: 150, maxDpiGray: 100, forceDownsample: true }).includes('50=100'));
  assert.ok(tokensOf({ maxDpi: 150, thresholdDpi: 300, forceDownsample: true }).includes('50=300'));
});

test('JPEG subsampling and progressive map onto the engine JPEG tokens', () => {
  assert.ok(!tokensOf({}).some((t) => t.startsWith('53=') || t.startsWith('55=')));
  assert.ok(tokensOf({ jpegSubsampling: '420' }).includes('53=1'));
  assert.ok(tokensOf({ jpegSubsampling: '422' }).includes('53=2'));
  assert.ok(tokensOf({ jpegSubsampling: '444' }).includes('53=3'));
  assert.ok(tokensOf({ jpegProgressive: true }).includes('55=1'));
  assert.equal(normalizeHyperOptions({ jpegSubsampling: 420 }).jpegSubsampling, '420');
  assert.equal(normalizeHyperOptions({ jpegSubsampling: '411' }).jpegSubsampling, 'auto');
});

test('speed maps to its own option id and balanced emits nothing', () => {
  assert.ok(!tokensOf({}).some((t) => t.startsWith('70=')));
  assert.ok(tokensOf({ speed: 'quality' }).includes('70=1'));
  assert.ok(tokensOf({ speed: 'fast' }).includes('70=2'));
  assert.equal(normalizeHyperOptions({ speed: 'turbo' }).speed, 'balanced');
});

test('discard toggles set their engine bits and nothing else', () => {
  const bits: Array<[keyof typeof medium, number]> = [
    ['removeMetadata', 0x100],
    ['removeBookmarks', 0x4],
    ['removeAttachments', 0x20],
    ['removeActions', 0x1000],
    ['removeNamedDests', 0x800],
    ['removePrintSettings', 0x200],
    ['removeSearchIndex', 0x400],
    ['removeSignatureAppearances', 0x10000],
  ];
  const base = maskOf({});
  for (const [key, bit] of bits) {
    assert.equal(base & bit, 0, `${key} must be off by default`);
    assert.equal(maskOf({ [key]: true }), base | bit, key);
  }
});

test('annotations keep, flatten and remove use the right bits; removeAnnots stays flatten', () => {
  const flatten = 0x40000;
  const remove = 0x800000;
  assert.equal(maskOf({}) & (flatten | remove), 0);
  assert.equal(maskOf({ annotations: 'flatten' }) & (flatten | remove), flatten);
  assert.equal(maskOf({ annotations: 'remove' }) & (flatten | remove), remove);
  assert.equal(maskOf({ removeAnnots: true }) & (flatten | remove), flatten);
  assert.equal(normalizeHyperOptions({ removeAnnots: true }).annotations, 'flatten');
  assert.equal(normalizeHyperOptions({ removeAnnots: true, annotations: 'remove' }).annotations, 'remove');
  assert.equal(normalizeHyperOptions({ annotations: 'hide' }).annotations, 'keep');
});

test('linearize and minVersion are SDK options with no engine token', () => {
  const t = tokensOf({ linearize: true, minVersion: '1.7' });
  assert.deepEqual(t, tokensOf({}));
  assert.equal(normalizeHyperOptions({ minVersion: 1.7 }).minVersion, '1.7');
  assert.equal(normalizeHyperOptions({ minVersion: 2 }).minVersion, '2.0');
  assert.equal(normalizeHyperOptions({ minVersion: '1.3' }).minVersion, 'none');
});

test('mrcDetectPhotos is on by default and only emits a token when turned off with MRC', () => {
  assert.equal(medium.mrcDetectPhotos, true);
  assert.ok(!tokensOf({ mrc: 'auto' }).includes('72=0'));
  assert.ok(tokensOf({ mrc: 'auto', mrcDetectPhotos: false }).includes('72=0'));
  assert.ok(!tokensOf({ mrcDetectPhotos: false }).includes('72=0'));
});

test('jbig2Symbols is off in every preset and only emits a token when lossy', () => {
  for (const level of Object.keys(HYPER_PRESETS) as (keyof typeof HYPER_PRESETS)[])
    assert.equal(normalizeHyperOptions(HYPER_PRESETS[level]).jbig2Symbols, false);
  assert.ok(!tokensOf({}).includes('73=1'));
  assert.ok(tokensOf({ jbig2Symbols: true }).includes('73=1'));
  assert.ok(!tokensOf({ jbig2Symbols: true, lossless: true }).includes('73=1'));
});

test('raiseHeaderVersion raises a lower header and never lowers one', async () => {
  const { raiseHeaderVersion } = await import('../sdk/node/pdfa.js');
  const pdf = (v: string) => new TextEncoder().encode(`%PDF-${v}\n%rest`);
  const dec = (b: Uint8Array) => new TextDecoder().decode(b.subarray(0, 8));
  assert.equal(dec(raiseHeaderVersion(pdf('1.4'), '1.7')), '%PDF-1.7');
  assert.equal(dec(raiseHeaderVersion(pdf('1.7'), '2.0')), '%PDF-2.0');
  assert.equal(dec(raiseHeaderVersion(pdf('1.7'), '1.5')), '%PDF-1.7');
  assert.equal(dec(raiseHeaderVersion(pdf('2.0'), '1.7')), '%PDF-2.0');
  const same = pdf('1.6');
  assert.equal(raiseHeaderVersion(same, 'none'), same);
});

test('preserving PDF/A refuses removeMetadata, colorTarget, linearize and minVersion 2.0 with warnings', async () => {
  const { conformanceSafeOptions, conformanceWarnings } = await import('../sdk/node/pdfa.js');
  const requested = normalizeHyperOptions({
    ...HYPER_PRESETS.medium,
    removeMetadata: true, colorTarget: 'cmyk', linearize: true, minVersion: '2.0',
  });
  const safe2 = conformanceSafeOptions(requested, { part: 2, conformance: 'b' });
  assert.equal(safe2.removeMetadata, false);
  assert.equal(safe2.colorTarget, 'original');
  assert.equal(safe2.linearize, false);
  assert.equal(safe2.minVersion, 'none');
  const warnings = conformanceWarnings(requested, safe2).join('\n');
  for (const w of ['removeMetadata', 'colorTarget', 'linearize', 'minVersion']) {
    assert.match(warnings, new RegExp(w));
  }
  assert.equal(conformanceSafeOptions(requested, { part: 4, conformance: '' }).minVersion, '2.0');
});

test('the target ladder lowers per-class limits too', async () => {
  const { targetLadder } = await import('../sdk/node/target.js');
  const base = normalizeHyperOptions({ ...HYPER_PRESETS.medium, maxDpiColor: 300, maxDpiGray: 300 });
  for (const rung of targetLadder(base)) {
    const t = buildHyperTokens(rung);
    assert.ok(!t.some((x) => /^(57|58)=/.test(x)), 'rungs must not keep a higher class limit');
  }
});

test('every option has a documented default that matches normalization', async () => {
  const { HYPER_OPTION_DOCS } = await import('../sdk/node/options.js');
  const defaults = normalizeHyperOptions({});
  for (const [key, doc] of Object.entries(HYPER_OPTION_DOCS)) {
    assert.equal(defaults[key as keyof typeof defaults], doc.default, key);
    assert.doesNotMatch(doc.description, /\u2014/, `${key} description must not use an em dash`);
  }
});

test('lossless never flattens ICC profiles, even when asked', () => {
  const tokens = buildHyperTokens(normalizeHyperOptions({ ...HYPER_PRESETS.medium, lossless: true, flattenIcc: true }));
  assert.ok(!tokens.includes('8=1'), tokens.join(' '));
  assert.ok(buildHyperTokens(normalizeHyperOptions({ ...HYPER_PRESETS.medium, flattenIcc: true })).includes('8=1'));
});
