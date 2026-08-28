import type { HyperCompressOptions } from './options.js';

const LATIN1 = new TextDecoder('latin1');

export interface PdfaLevel {
  part: number;
  conformance: string;
}

export function formatPdfaLevel(level: PdfaLevel): string {
  return `PDF/A-${level.part}${level.conformance}`;
}

export function parsePdfaFromXmp(xmp: string): PdfaLevel | null {
  const part = xmp.match(/pdfaid:part\s*(?:=\s*["']|>)\s*([0-9])/);
  if (!part || part[1] === undefined) return null;
  const conf = xmp.match(/pdfaid:conformance\s*(?:=\s*["']|>)\s*([A-Za-z])/);
  return {
    part: Number(part[1]),
    conformance: conf?.[1] ? conf[1].toLowerCase() : '',
  };
}

export function stripPdfaFromXmp(xmp: string): string {
  return xmp
    .replace(/\s*<pdfaid:(part|conformance|rev)>[^<]*<\/pdfaid:\1>/g, '')
    .replace(/\s*pdfaid:(part|conformance|rev)\s*=\s*["'][^"']*["']/g, '')
    .replace(/\s*xmlns:pdfaid\s*=\s*["'][^"']*["']/g, '');
}

export function conformanceSafeOptions(
  requested: HyperCompressOptions,
  level: PdfaLevel,
): HyperCompressOptions {
  const safe: HyperCompressOptions = { ...requested };

  safe.removeStandardFonts = false;
  safe.unembedAliasedFonts = false;

  safe.removeOutputIntents = false;

  safe.flattenIcc = false;

  safe.rasterizePages = false;

  if (level.part === 1) {
    safe.preferJpx = false;

    safe.subsetFonts = false;
  }

  if (level.conformance === 'a') {
    safe.removeStructTree = false;
  }

  return safe;
}

export function packAllowedWhilePreserving(): boolean {
  return false;
}

const MAX_XMP_BYTES = 1024 * 1024;

interface BufferLike {
  indexOf(value: string, byteOffset: number, encoding: string): number;
}
interface BufferCtor {
  from(ab: ArrayBufferLike, byteOffset: number, length: number): BufferLike;
}
const NodeBuffer: BufferCtor | undefined = (
  globalThis as unknown as { Buffer?: BufferCtor }
).Buffer;

function byteIndexOf(hay: Uint8Array, needle: string, from: number): number {
  if (NodeBuffer) {
    return NodeBuffer.from(hay.buffer, hay.byteOffset, hay.byteLength).indexOf(
      needle,
      from,
      'latin1',
    );
  }
  const n = needle.length;
  if (n === 0) return Math.min(from, hay.length);
  const first = needle.charCodeAt(0);
  const last = hay.length - n;
  for (let i = Math.max(0, from); i <= last; i++) {
    if (hay[i] !== first) continue;
    let j = 1;
    while (j < n && hay[i + j] === needle.charCodeAt(j)) j++;
    if (j === n) return i;
  }
  return -1;
}

export function detectPdfaInBytes(bytes: Uint8Array): PdfaLevel | null {
  const start = byteIndexOf(bytes, '<?xpacket', 0);
  if (start < 0) return null;
  const found = byteIndexOf(bytes, '<?xpacket end', start);
  const end = found < 0 ? bytes.length : found;
  const stop = Math.min(end, start + MAX_XMP_BYTES);
  return parsePdfaFromXmp(LATIN1.decode(bytes.subarray(start, stop)));
}

export function restoreHeaderVersion(source: Uint8Array, output: Uint8Array): Uint8Array {
  const readHeader = (b: Uint8Array) => {
    if (b.length < 8) return '';
    let s = '';
    for (let i = 0; i < 8; i++) s += String.fromCharCode(b[i] ?? 0);
    return s;
  };
  const from = readHeader(source);
  const to = readHeader(output);
  if (!/^%PDF-\d\.\d$/.test(from) || !/^%PDF-\d\.\d$/.test(to) || from === to) {
    return output;
  }
  const patched = output.slice();
  for (let i = 0; i < 8; i++) patched[i] = from.charCodeAt(i);
  return patched;
}
