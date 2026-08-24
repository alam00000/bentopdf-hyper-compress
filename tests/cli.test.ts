import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parse } from '../cli/bin/hyper.js';

test('target-size accepts unit suffixes', () => {
  assert.equal(parse(['a', 'b', '--target-size', '2MB']).targetSizeBytes, 2 * 1024 * 1024);
  assert.equal(parse(['a', 'b', '--target-size', '500KB']).targetSizeBytes, 500 * 1024);
  assert.equal(parse(['a', 'b', '--target-size', '1234']).targetSizeBytes, 1234);
});

test('--brotli maps onto the option override', () => {
  assert.equal(parse(['a', 'b', '--brotli']).overrides.brotli, true);
});

test('--set coerces booleans and numbers', () => {
  const p = parse(['a', 'b', '--set', 'grayscale=true', '--set', 'imageQuality=55']);
  assert.equal(p.overrides.grayscale, true);
  assert.equal(p.overrides.imageQuality, 55);
});
