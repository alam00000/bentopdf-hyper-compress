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

function buildPdf(
  objects: Array<string | Uint8Array>,
  root: number,
  trailerExtra = '',
): Uint8Array {
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
    push(obj);
    push('\nendobj\n');
  });

  const xrefAt = length;
  push(`xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`);
  for (const off of offsets) {
    push(`${off.toString().padStart(10, '0')} 00000 n \n`);
  }
  push(
    `trailer\n<< /Size ${objects.length + 1} /Root ${root} 0 R${trailerExtra} >>\n` +
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

function imageObject(width: number, height: number): Uint8Array {
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
  return imageObj;
}

export function imagePdf(width = 500, height = 400): Uint8Array {
  const content = 'q 560 0 0 720 26 36 cm /Im0 Do Q';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      imageObject(width, height),
    ],
    1,
  );
}

export function textOverImagePdf(): Uint8Array {
  const content = 'q 560 0 0 720 26 36 cm /Im0 Do Q BT /F1 24 Tf 40 700 Td (Scanned text) Tj ET';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> ' +
        '/Font << /F1 6 0 R >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      imageObject(500, 400),
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    ],
    1,
  );
}

export function signedPdf(stream = 'BT /F1 24 Tf 40 700 Td (Signed) Tj ET'): Uint8Array {
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

export function pdfaPdf(stream = 'BT /F1 24 Tf 40 700 Td (Archive) Tj ET'): Uint8Array {
  const xmp =
    '<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>' +
    '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF ' +
    'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">' +
    '<rdf:Description xmlns:pdfaid="http://www.aiim.org/pdfa/ns/id/" ' +
    'pdfaid:part="2" pdfaid:conformance="B"/></rdf:RDF></x:xmpmeta>' +
    '<?xpacket end="w"?>';
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

export function xmpPdf(deflated = false): Uint8Array {
  const xmp = Buffer.from(
    '<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>' +
      '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF ' +
      'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">' +
      '<rdf:Description rdf:about="" ' +
      'xmlns:dc="http://purl.org/dc/elements/1.1/">' +
      '<dc:title>HYPERXMPMARKER</dc:title></rdf:Description>' +
      '<rdf:Description rdf:about=""/>'.repeat(40) +
      '</rdf:RDF></x:xmpmeta><?xpacket end="w"?>',
    'latin1',
  );
  const data = deflated ? deflateSync(xmp) : xmp;
  const enc = new TextEncoder();
  const head = enc.encode(
    `<< /Type /Metadata /Subtype /XML${deflated ? ' /Filter /FlateDecode' : ''} ` +
      `/Length ${data.length} >>\nstream\n`,
  );
  const tail = enc.encode('\nendstream');
  const metadataObj = new Uint8Array(head.length + data.length + tail.length);
  metadataObj.set(head, 0);
  metadataObj.set(data, head.length);
  metadataObj.set(tail, head.length + data.length);

  let stream = '';
  for (let i = 0; i < 200; i++) {
    stream += `BT /F1 10 Tf 40 ${720 - (i % 60) * 11} Td (metadata line ${i}) Tj ET\n`;
  }
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
      `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
      metadataObj,
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

function streamObject(dict: string, data: Uint8Array): Uint8Array {
  const enc = new TextEncoder();
  const head = enc.encode(`<< ${dict} /Length ${data.length} >>\nstream\n`);
  const tail = enc.encode('\nendstream');
  const out = new Uint8Array(head.length + data.length + tail.length);
  out.set(head, 0);
  out.set(data, head.length);
  out.set(tail, head.length + data.length);
  return out;
}

export function layeredScanPdf(): Uint8Array {
  const w = 900;
  const h = 1200;
  const bg = Buffer.alloc(w * h * 3);
  let rng = 0x9e3779b9 >>> 0;
  const rand = (): number => {
    rng ^= rng << 13;
    rng >>>= 0;
    rng ^= rng >>> 17;
    rng ^= rng << 5;
    rng >>>= 0;
    return rng >>> 24;
  };
  for (let i = 0; i < w * h; i++) {
    const n = (rand() & 15) - 8;
    bg[i * 3] = 236 + n;
    bg[i * 3 + 1] = 228 + n;
    bg[i * 3 + 2] = 210 + n;
  }
  const rowBytes = Math.ceil(w / 8);
  const mask = Buffer.alloc(rowBytes * h, 0);
  for (let y = 0; y < h; y++) {
    const line = Math.floor(y / 40);
    const inLine = y % 40;
    if (inLine < 6 || inLine > 26 || line < 2 || line > 27 || line % 2) continue;
    for (let x = 60; x < w - 60; x++) {
      const glyph = Math.floor((x - 60) / 14);
      const gx = (x - 60) % 14;
      if (glyph % 7 === 6) continue;
      const bar = inLine < 7 || inLine > 25 ||
        ((glyph + line) % 3 === 0 && inLine > 14 && inLine < 17);
      if (gx < 1 || (gx < 11 && bar)) mask[y * rowBytes + (x >> 3)]! |= 0x80 >> (x & 7);
    }
  }
  const dims = `/Type /XObject /Subtype /Image /Width ${w} /Height ${h}`;
  const content = 'q 612 0 0 792 0 0 cm /Bg Do Q q 612 0 0 792 0 0 cm /Fg Do Q';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /XObject << /Bg 5 0 R /Fg 6 0 R >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      streamObject(`${dims} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode`,
        deflateSync(bg)),
      streamObject(`${dims} /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 7 0 R ` +
        '/Filter /FlateDecode', deflateSync(Buffer.alloc(w * h * 3, 0))),
      streamObject(`${dims} /ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /FlateDecode`,
        deflateSync(mask)),
    ],
    1,
  );
}

export function hasRenderer(): boolean {
  return existsSync(path.join(prebuilt, 'hpdf-render'));
}

export function renderGray(bytes: Uint8Array, scale = 1): { width: number; height: number; gray: Uint8Array } {
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-render-'));
  try {
    const inFile = path.join(dir, 'in.pdf');
    const outFile = path.join(dir, 'out.ppm');
    writeFileSync(inFile, bytes);
    const r = spawnSync(path.join(prebuilt, 'hpdf-render'), [inFile, outFile, String(scale)]);
    if (r.status !== 0) throw new Error(`hpdf-render failed (${r.status})`);
    const ppm = readFileSync(outFile);
    const header = /^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(ppm.subarray(0, 64).toString('latin1'));
    if (!header) throw new Error('unexpected PPM header');
    const width = Number(header[1]);
    const height = Number(header[2]);
    const px = ppm.subarray(header[0].length);
    const gray = new Uint8Array(width * height);
    for (let i = 0; i < gray.length; i++) {
      gray[i] = Math.round((px[i * 3]! + px[i * 3 + 1]! + px[i * 3 + 2]!) / 3);
    }
    return { width, height, gray };
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function bulkText(lines = 300, color = ''): string {
  let body = color;
  for (let i = 0; i < lines; i++) {
    body += `BT /F1 10 Tf 40 ${720 - (i % 60) * 11} Td (feature line ${i}: the quick brown fox) Tj ET\n`;
  }
  return body;
}

export function featurePdf(): Uint8Array {
  const xmp =
    '<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>' +
    '<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF ' +
    'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">' +
    '<rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/">' +
    '<dc:title>FEATUREXMP</dc:title></rdf:Description></rdf:RDF></x:xmpmeta>' +
    '<?xpacket end="w"?>';
  const content = bulkText(300, '1 0 0 rg 0 0 0 1 K 0.2 0.4 0.6 RG 0 0 50 50 re B\n');
  const js = 'app.alert("FEATUREJS");';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R /Outlines 7 0 R /Metadata 9 0 R ' +
        '/Names << /EmbeddedFiles << /Names [(a.txt) 10 0 R] >> ' +
        '/JavaScript << /Names [(js) 12 0 R] >> /Dests << /Names [(target) [3 0 R /Fit]] >> >> ' +
        '/OpenAction << /S /GoTo /D (target) >> ' +
        '/ViewerPreferences << /PrintScaling /None /Duplex /Simplex /DisplayDocTitle true >> >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R ' +
        '/Resources << /Font << /F1 5 0 R >> >> /Annots [6 0 R 13 0 R 14 0 R 15 0 R] ' +
        '/AA << /O << /S /JavaScript /JS (1) >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
      '<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (FEATURENOTE) >>',
      '<< /Type /Outlines /First 8 0 R /Last 8 0 R /Count 1 >>',
      '<< /Title (Chapter) /Parent 7 0 R /Dest (target) >>',
      `<< /Type /Metadata /Subtype /XML /Length ${xmp.length} >>\nstream\n${xmp}\nendstream`,
      '<< /Type /Filespec /F (a.txt) /EF << /F 11 0 R >> >>',
      '<< /Type /EmbeddedFile /Length 13 >>\nstream\nFEATUREATTACH\nendstream',
      `<< /S /JavaScript /JS (${js}) >>`,
      '<< /Type /Annot /Subtype /Link /Rect [40 40 80 60] /Border [0 0 0] /Dest (target) >>',
      '<< /Type /Annot /Subtype /Link /Rect [90 40 130 60] /Border [0 0 0] ' +
        '/A << /S /Launch /F (calc.exe) >> >>',
      '<< /Type /Annot /Subtype /FileAttachment /Rect [140 40 160 60] /FS 10 0 R >>',
      '<< /Title (FEATURETITLE) /Producer (Fixture) /CustomKey (FEATURECUSTOM) >>',
    ],
    1,
    ' /Info 16 0 R',
  );
}

export function monoScanPdf(width = 2400, height = 3000): Uint8Array {
  const rowBytes = Math.ceil(width / 8);
  const bits = Buffer.alloc(rowBytes * height, 0xff);
  let seed = 7;
  const rand = (n: number) => {
    seed = (seed * 1103515245 + 12345) % 2147483648;
    return seed % n;
  };
  for (let b = 0; b < 4000; b++) {
    const x0 = 100 + rand(width - 240), y0 = 100 + rand(height - 160);
    const bw = 8 + rand(32), bh = 20 + rand(20);
    for (let y = y0; y < y0 + bh; y++) {
      for (let x = x0; x < x0 + bw; x++) {
        if (rand(100) < 85) bits[y * rowBytes + (x >> 3)]! &= ~(0x80 >> (x & 7));
      }
    }
  }
  const content = 'q 612 0 0 792 0 0 cm /Im0 Do Q';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      streamObject(
        `/Type /XObject /Subtype /Image /Width ${width} /Height ${height} ` +
          '/ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /FlateDecode',
        deflateSync(bits),
      ),
    ],
    1,
  );
}

export function cmykImagePdf(width = 600, height = 500): Uint8Array {
  const raw = Buffer.alloc(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4;
      raw[o] = (x * 255) / width;
      raw[o + 1] = (y * 255) / height;
      raw[o + 2] = ((x + y) * 127) / (width + height);
      raw[o + 3] = 20;
    }
  }
  const content = 'q 560 0 0 720 26 36 cm /Im0 Do Q';
  return buildPdf(
    [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ' +
        '/Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> >> >>',
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      streamObject(
        `/Type /XObject /Subtype /Image /Width ${width} /Height ${height} ` +
          '/ColorSpace /DeviceCMYK /BitsPerComponent 8 /Filter /FlateDecode',
        deflateSync(raw),
      ),
    ],
    1,
  );
}

export function engineHasBranchFeatures(): boolean {
  if (!hasEngine()) return false;
  const worker = process.env.HYPER_DRV ?? path.join(prebuilt, 'hpdf-worker');
  const dir = mkdtempSync(path.join(tmpdir(), 'hyper-probe-'));
  try {
    const inFile = path.join(dir, 'in.pdf');
    writeFileSync(inFile, imagePdf(300, 200));
    const r = spawnSync(worker, [inFile, path.join(dir, 'out.pdf'), '71=1'], { encoding: 'utf8' });
    return /\[hyper-report\] image/.test(r.stderr ?? '');
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}
