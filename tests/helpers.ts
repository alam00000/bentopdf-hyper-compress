import { existsSync, mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { deflateSync } from 'node:zlib';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const prebuilt = path.resolve(here, '..', '..', 'cli', 'prebuilt');

export function hasEngine(): boolean {
  const worker = process.env.HYPER_DRV ?? path.join(prebuilt, 'hpdf-worker');
  const qpdf = process.env.HYPER_QPDF ?? path.join(prebuilt, 'qpdf');
  return existsSync(worker) && existsSync(qpdf);
}

function buildPdf(objects: Array<string | Uint8Array>, root: number): Uint8Array {
  const enc = new TextEncoder();
  const parts: Uint8Array[] = [];
  let length = 0;
  const push = (chunk: Uint8Array | string): number => {
    const bytes = typeof chunk === 'string' ? enc.encode(chunk) : chunk;
    parts.push(bytes);
    length += bytes.length;
    return bytes.length;
  };

  push('%PDF-1.7\n%\xe2\xe3\xcf\xd3\n');
  const offsets: number[] = [];
  objects.forEach((obj, i) => {
    offsets.push(length);
    push(`${i + 1} 0 obj\n`);
    if (typeof obj === 'string') push(obj);
    push('\nendobj\n');
  });

  const xrefAt = length;
  push(`xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`);
  for (const off of offsets) {
    push(`${off.toString().padStart(10, '0')} 00000 n \n`);
  }
  push(
    `trailer\n<< /Size ${objects.length + 1} /Root ${root} 0 R >>\n` +
      `startxref\n${xrefAt}\n%%EOF\n`,
  );

  const out = new Uint8Array(length);
  let pos = 0;
  for (const p of parts) {
    out.set(p, pos);
    pos += p.length;
  }
  return out;
}

export function textPdf(text = 'Hyper Compress'): Uint8Array {
  const stream = `BT /F1 24 Tf 40 700 Td (${text}) Tj ET`;
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
      `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    ],
    1,
  );
}

export function richTextPdf(lines = 400): Uint8Array {
  let body = '';
  for (let i = 0; i < lines; i++) {
    body += `BT /F1 10 Tf 40 ${720 - (i % 60) * 11} Td ` +
      `(line ${i}: the quick brown fox jumps over the lazy dog) Tj ET\n`;
  }
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
      `<< /Length ${body.length} >>\nstream\n${body}\nendstream`,
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    ],
    1,
  );
}

export function imagePdf(width = 500, height = 400): Uint8Array {
  const raw = Buffer.alloc(width * height * 3);
  let seed = 1;
  for (let i = 0; i < raw.length; i += 3) {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    const n = (seed >> 16) & 63;
    raw[i] = (i + n) & 255;
    raw[i + 1] = ((i >> 3) + n) & 255;
    raw[i + 2] = ((i >> 6) + n) & 255;
  }
  const img = deflateSync(raw, { level: 6 });
  const enc = new TextEncoder();
  const dict =
    `<< /Type /XObject /Subtype /Image /Width ${width} /Height ${height} ` +
    `/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode ` +
    `/Length ${img.length} >>\nstream\n`;
  const head = enc.encode(dict);
  const tail = enc.encode('\nendstream');
  const imageObj = new Uint8Array(head.length + img.length + tail.length);
  imageObj.set(head, 0);
  imageObj.set(img, head.length);
  imageObj.set(tail, head.length + img.length);

  const content = 'q 560 0 0 720 26 36 cm /Im0 Do Q';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      imageObj,
    ],
    1,
  );
}

export function signedPdf(): Uint8Array {
  const stream = 'BT /F1 24 Tf 40 700 Td (Signed) Tj ET';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R /Perms << /DocMDP 6 0 R >> >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
      `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
      '<< /Type /Sig /Filter /Adobe.PPKLite >>',
    ],
    1,
  );
}

export function pdfaPdf(): Uint8Array {
  const xmp =
    '<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>' +
    '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF ' +
    'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">' +
    '<rdf:Description xmlns:pdfaid="http://www.aiim.org/pdfa/ns/id/" ' +
    'pdfaid:part="2" pdfaid:conformance="B"/></rdf:RDF></x:xmpmeta>' +
    '<?xpacket end="w"?>';
  const stream = 'BT /F1 24 Tf 40 700 Td (Archive) Tj ET';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
      `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
      `<< /Type /Metadata /Subtype /XML /Length ${xmp.length} >>\n` +
        `stream\n${xmp}\nendstream`,
    ],
    1,
  );
}

export function expandPdf(bytes: Uint8Array): Uint8Array {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-qdf-'));
  try {
    const inFile = path.join(dir, 'in.pdf');
    const outFile = path.join(dir, 'out.pdf');
    writeFileSync(inFile, bytes);
    const r = spawnSync(path.join(prebuilt, 'qpdf'), [
      '--qdf',
      '--object-streams=disable',
      '--stream-data=uncompress',
      inFile,
      outFile,
    ]);
    return r.status === 0 ? new Uint8Array(readFileSync(outFile)) : bytes;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

export function countMarker(bytes: Uint8Array, marker: string): number {
  const needle = new TextEncoder().encode(marker);
  let count = 0;
  for (let i = 0; i + needle.length <= bytes.length; i++) {
    let hit = true;
    for (let j = 0; j < needle.length; j++) {
      if (bytes[i + j] !== needle[j]) {
        hit = false;
        break;
      }
    }
    if (hit) count++;
  }
  return count;
}
