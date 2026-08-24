import { spawn } from 'node:child_process';
import { readFile, readdir, mkdtemp, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export function sh(cmd, args, { timeoutMs = 120_000, cwd, env } = {}) {
  return new Promise((resolve) => {
    const child = spawn(cmd, args, {
      cwd,
      env: env ? { ...process.env, ...env } : process.env,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let out = Buffer.alloc(0);
    let err = '';
    let done = false;
    const finish = (code, timedOut) => {
      if (done) return;
      done = true;
      clearTimeout(t);
      resolve({ code, stdout: out, stderr: err, timedOut });
    };
    const t = setTimeout(() => {
      try { child.kill('SIGKILL'); } catch {}
      finish(124, true);
    }, timeoutMs);
    child.stdout.on('data', (c) => { out = Buffer.concat([out, c]); });
    child.stderr.on('data', (c) => { err += c.toString(); });
    child.on('error', () => finish(127, false));
    child.on('close', (code) => finish(code ?? 1, false));
  });
}

export async function fileSize(p) {
  try { return (await stat(p)).size; } catch { return 0; }
}

export async function withTmpDir(fn) {
  const dir = await mkdtemp(join(tmpdir(), 'hregress-'));
  try { return await fn(dir); }
  finally { try { await rm(dir, { recursive: true, force: true }); } catch {} }
}

function parsePPM(buf) {
  if (buf[0] !== 0x50 || buf[1] !== 0x36) return null;
  let pos = 2;
  const tokens = [];
  while (tokens.length < 3 && pos < buf.length) {
    while (pos < buf.length && /\s/.test(String.fromCharCode(buf[pos]))) pos++;
    if (buf[pos] === 0x23) {
      while (pos < buf.length && buf[pos] !== 0x0a) pos++;
      continue;
    }
    let n = '';
    while (pos < buf.length && /\d/.test(String.fromCharCode(buf[pos]))) {
      n += String.fromCharCode(buf[pos]); pos++;
    }
    if (n) tokens.push(parseInt(n, 10));
  }
  pos++;
  const [width, height] = tokens;
  const data = buf.subarray(pos, pos + width * height * 3);
  return { width, height, data };
}

export async function renderPages(pdfPath, { dpi = 100, maxPages = 6, timeoutMs = 90_000 } = {}) {
  return withTmpDir(async (dir) => {
    const prefix = join(dir, 'pg');
    const res = await sh('pdftoppm', ['-r', String(dpi), '-l', String(maxPages), pdfPath, prefix], { timeoutMs });
    if (res.timedOut) return null;
    let names;
    try { names = (await readdir(dir)).filter((n) => n.endsWith('.ppm')).sort(); }
    catch { return null; }
    if (names.length === 0) return null;
    const pages = [];
    for (const n of names) {
      const img = parsePPM(await readFile(join(dir, n)));
      if (img) pages.push(img);
    }
    return pages.length ? pages : null;
  });
}

function toGray(img) {
  const { width, height, data } = img;
  const g = new Float64Array(width * height);
  for (let i = 0, j = 0; j < g.length; i += 3, j++) {
    g[j] = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
  }
  return g;
}

function toGrayCrop(img, cw, ch) {
  const { width, data } = img;
  const g = new Float64Array(cw * ch);
  for (let y = 0; y < ch; y++) {
    let i = y * width * 3, j = y * cw;
    for (let x = 0; x < cw; x++, i += 3, j++) {
      g[j] = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
    }
  }
  return g;
}

function ssimStats(ga, gb) {
  const n = ga.length;
  let muA = 0, muB = 0, mse = 0, changed = 0;
  for (let i = 0; i < n; i++) {
    muA += ga[i]; muB += gb[i];
    const d = ga[i] - gb[i];
    mse += d * d;
    if (Math.abs(d) > 8) changed++;
  }
  muA /= n; muB /= n; mse /= n;
  let varA = 0, varB = 0, cov = 0;
  for (let i = 0; i < n; i++) {
    const da = ga[i] - muA, db = gb[i] - muB;
    varA += da * da; varB += db * db; cov += da * db;
  }
  varA /= n; varB /= n; cov /= n;
  const C1 = (0.01 * 255) ** 2, C2 = (0.03 * 255) ** 2;
  const ssim =
    ((2 * muA * muB + C1) * (2 * cov + C2)) /
    ((muA * muA + muB * muB + C1) * (varA + varB + C2));
  const psnr = mse === 0 ? 99 : 10 * Math.log10((255 * 255) / mse);
  return { psnr, ssim, pctChanged: changed / n };
}

export function compareImages(a, b) {
  if (a.width !== b.width || a.height !== b.height) {
    const tolW = Math.max(2, Math.round(a.width * 0.01));
    const tolH = Math.max(2, Math.round(a.height * 0.01));
    if (Math.abs(a.width - b.width) > tolW ||
        Math.abs(a.height - b.height) > tolH) {
      return { dimMismatch: true };
    }
    const cw = Math.min(a.width, b.width), ch = Math.min(a.height, b.height);
    return ssimStats(toGrayCrop(a, cw, ch), toGrayCrop(b, cw, ch));
  }
  return ssimStats(toGray(a), toGray(b));
}

export function aggregateFidelity(inPages, outPages) {
  if (!inPages || !outPages) return { ok: false, reason: 'render-failed' };
  const pages = Math.min(inPages.length, outPages.length);
  if (pages === 0) return { ok: false, reason: 'no-pages' };
  let ssimMin = 1, ssimSum = 0, psnrMin = 99, pctMax = 0, dimMismatch = false;
  for (let i = 0; i < pages; i++) {
    const c = compareImages(inPages[i], outPages[i]);
    if (c.dimMismatch) { dimMismatch = true; continue; }
    ssimMin = Math.min(ssimMin, c.ssim);
    ssimSum += c.ssim;
    psnrMin = Math.min(psnrMin, c.psnr);
    pctMax = Math.max(pctMax, c.pctChanged);
  }
  return {
    ok: true, dimMismatch,
    pageCountIn: inPages.length, pageCountOut: outPages.length,
    ssimMin: dimMismatch ? null : ssimMin,
    ssimMean: dimMismatch ? null : ssimSum / pages,
    psnrMin: dimMismatch ? null : psnrMin,
    pctChangedMax: dimMismatch ? null : pctMax,
  };
}

export async function checkValidity(pdfPath) {
  const [qpdfR, popR, muR] = await Promise.all([
    sh('qpdf', ['--check', pdfPath], { timeoutMs: 60_000 }),
    sh('pdftoppm', ['-r', '24', '-l', '1', pdfPath, join(tmpdir(), `hrv-${process.pid}-${Math.floor(performance.now())}`)], { timeoutMs: 60_000 }),
    sh('mutool', ['draw', '-r', '24', '-o', '/dev/null', pdfPath, '1'], { timeoutMs: 60_000 }),
  ]);
  return {
    qpdfOk: qpdfR.code === 0,
    qpdfNote: qpdfR.code === 0 ? '' : qpdfR.stdout.toString().split('\n').slice(0, 3).join(' ').slice(0, 200),
    opensPoppler: popR.code === 0 && !popR.timedOut,
    opensMutool: muR.code === 0 && !muR.timedOut,
  };
}

export async function textSimilarity(inPdf, outPdf) {
  const norm = (s) => s.replace(/\s+/g, ' ').trim();
  const [a, b] = await Promise.all([
    sh('pdftotext', ['-q', inPdf, '-'], { timeoutMs: 60_000 }),
    sh('pdftotext', ['-q', outPdf, '-'], { timeoutMs: 60_000 }),
  ]);
  const ta = norm(a.stdout.toString()), tb = norm(b.stdout.toString());
  if (ta.length === 0) return { hadText: false, similarity: 1 };
  const bigrams = (s) => {
    const set = new Map();
    const cap = Math.min(s.length, 200_000);
    for (let i = 0; i < cap - 1; i++) {
      const g = s.slice(i, i + 2);
      set.set(g, (set.get(g) || 0) + 1);
    }
    return set;
  };
  const A = bigrams(ta), B = bigrams(tb);
  let inter = 0, sizeA = 0, sizeB = 0;
  for (const v of A.values()) sizeA += v;
  for (const [k, v] of B) { sizeB += v; if (A.has(k)) inter += Math.min(v, A.get(k)); }
  const dice = (2 * inter) / (sizeA + sizeB || 1);
  return { hadText: true, similarity: dice };
}

export function judgeCell({ runOk, srcVal, val, fid, ssim, textSim, lossless }) {
  const dq = [];
  if (!runOk) dq.push('run-failed');
  if (val && ((srcVal.opensPoppler && !val.opensPoppler) || (srcVal.opensMutool && !val.opensMutool))) dq.push('unreadable');
  if (val && srcVal.qpdfOk && !val.qpdfOk) dq.push('qpdf-check');
  if (fid && fid.ok && fid.dimMismatch) dq.push('dim-changed');
  if (lossless && ssim != null && ssim < 0.98) dq.push('lossless-altered');
  if (ssim != null && ssim < 0.5) dq.push('severe');
  if (textSim && textSim.hadText && textSim.similarity < 0.6) dq.push('text-lost');
  return dq;
}
