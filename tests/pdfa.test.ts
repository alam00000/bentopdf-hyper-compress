import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  conformanceSafeOptions,
  detectPdfaInBytes,
  parsePdfaFromXmp,
  restoreHeaderVersion,
  stripPdfaFromXmp,
} from '../sdk/node/pdfa.js';
import { normalizeHyperOptions } from '../sdk/node/options.js';
import { HYPER_PRESETS } from '../sdk/node/presets.js';

const XMP =
  '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:Description ' +
  'xmlns:pdfaid="http://www.aiim.org/pdfa/ns/id/" ' +
  'pdfaid:part="2" pdfaid:conformance="B"/></x:xmpmeta>';

const PACKET = `<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>${XMP}<?xpacket end="w"?>`;

test('parses a pdfaid claim out of XMP', () => {
  const level = parsePdfaFromXmp(XMP);
  assert.ok(level);
  assert.equal(level.part, 2);
});

test('stripping the claim removes pdfaid but keeps the packet', () => {
  const stripped = stripPdfaFromXmp(XMP);
  assert.equal(parsePdfaFromXmp(stripped), null);
  assert.ok(stripped.includes('xmpmeta'));
});

test('detectPdfaInBytes finds the claim in raw file bytes', () => {
  const bytes = new TextEncoder().encode(`%PDF-1.7\n${PACKET}\n%%EOF`);
  const level = detectPdfaInBytes(bytes);
  assert.ok(level);
});

test('conformanceSafeOptions never leaves brotli on', () => {
  const level = detectPdfaInBytes(new TextEncoder().encode(PACKET));
  assert.ok(level);
  const o = conformanceSafeOptions(
    normalizeHyperOptions({ ...HYPER_PRESETS.medium, brotli: true }),
    level,
  );
  assert.equal(normalizeHyperOptions(o).preserveConformance, o.preserveConformance);
});

test('restoreHeaderVersion copies the source header onto the output', () => {
  const src = new TextEncoder().encode('%PDF-2.0\nrest');
  const out = new TextEncoder().encode('%PDF-1.7\nrest');
  const patched = restoreHeaderVersion(src, out);
  assert.equal(new TextDecoder().decode(patched.slice(0, 8)), '%PDF-2.0');
});

test('detectPdfaInBytes scans past a large prefix without decoding the whole file', () => {
  const enc = new TextEncoder();
  const filler = new Uint8Array(5_000_000).fill(0x20);
  const tail = enc.encode(`${PACKET}\n%%EOF`);
  const bytes = new Uint8Array(filler.length + tail.length);
  bytes.set(filler, 0);
  bytes.set(tail, filler.length);
  const level = detectPdfaInBytes(bytes);
  assert.ok(level);
  assert.equal(level.part, 2);
});

test('detectPdfaInBytes returns null on a large buffer with no packet', () => {
  const bytes = new Uint8Array(2_000_000).fill(0x41);
  assert.equal(detectPdfaInBytes(bytes), null);
});
