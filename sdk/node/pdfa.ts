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

export function detectPdfaInBytes(bytes: Uint8Array): PdfaLevel | null {
  const text = LATIN1.decode(bytes);
  const start = text.indexOf('<?xpacket');
  if (start < 0) return null;
  const end = text.indexOf('<?xpacket end', start);
  return parsePdfaFromXmp(text.slice(start, end < 0 ? text.length : end));
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
