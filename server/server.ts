import * as http from 'node:http';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';
import { compress, verifyPassword, HyperError } from '../sdk/node/index.js';
import type { CompressLevel } from '../sdk/node/index.js';

const PORT = Number(process.env.HYPER_PORT ?? 8080);
const HOST = process.env.HYPER_HOST ?? '0.0.0.0';
const MAX_UPLOAD = Number(process.env.HYPER_MAX_UPLOAD_MB ?? 500) * 1024 * 1024;
const CONCURRENCY = Math.max(1, Number(process.env.HYPER_CONCURRENCY ?? 2));
const QUEUE_LIMIT = Math.max(0, Number(process.env.HYPER_QUEUE ?? 8));
const TIMEOUT_MS = Number(process.env.HYPER_TIMEOUT_MS ?? 600_000);

const HERE = path.dirname(fileURLToPath(import.meta.url));
const INDEX_HTML = (() => {
  for (const p of [path.join(HERE, 'index.html'), path.resolve(HERE, '..', '..', 'server', 'index.html')]) {
    if (fs.existsSync(p)) return fs.readFileSync(p);
  }
  return Buffer.from('<h1>Hyper Compress</h1><p>POST a PDF to /api/compress</p>');
})();

const PRESETS: readonly CompressLevel[] = ['low', 'medium', 'high', 'lossless'];

let active = 0;
const waiting: Array<() => void> = [];

function acquire(): Promise<boolean> {
  if (active < CONCURRENCY) {
    active++;
    return Promise.resolve(true);
  }
  if (waiting.length >= QUEUE_LIMIT) return Promise.resolve(false);
  return new Promise((resolve) => {
    waiting.push(() => { active++; resolve(true); });
  });
}

function release(): void {
  active--;
  const next = waiting.shift();
  if (next) next();
}

function readBody(req: http.IncomingMessage, limit: number): Promise<Buffer | null> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on('data', (c: Buffer) => {
      size += c.length;
      if (size > limit) {
        req.destroy();
        resolve(null);
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

function sendJson(res: http.ServerResponse, status: number, body: object): void {
  const buf = Buffer.from(JSON.stringify(body));
  res.writeHead(status, { 'content-type': 'application/json', 'content-length': buf.length });
  res.end(buf);
}

async function handleCompress(req: http.IncomingMessage, res: http.ServerResponse, url: URL): Promise<void> {
  const preset = (url.searchParams.get('preset') ?? 'medium') as CompressLevel;
  if (!PRESETS.includes(preset)) {
    sendJson(res, 400, { error: 'bad_preset', presets: PRESETS });
    return;
  }
  const targetRaw = url.searchParams.get('targetSizeBytes');
  const targetSizeBytes = targetRaw ? Number(targetRaw) : undefined;
  if (targetRaw && !(targetSizeBytes! > 0)) {
    sendJson(res, 400, { error: 'bad_target_size' });
    return;
  }
  const brotli = url.searchParams.get('brotli') === 'true';
  const preservePdfa = url.searchParams.get('preservePdfa') === 'true';
  let overrides: Record<string, unknown> | undefined;
  const optionsRaw = url.searchParams.get('options');
  if (optionsRaw) {
    if (optionsRaw.length > 4096) {
      sendJson(res, 400, { error: 'bad_options' });
      return;
    }
    try {
      const parsed: unknown = JSON.parse(optionsRaw);
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) throw new Error('shape');
      overrides = parsed as Record<string, unknown>;
    } catch {
      sendJson(res, 400, { error: 'bad_options' });
      return;
    }
  }
  const password = req.headers['x-password'];

  const ok = await acquire();
  if (!ok) {
    sendJson(res, 429, { error: 'busy', detail: 'queue full, retry later' });
    return;
  }
  const dir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'hyperd-'));
  try {
    const body = await readBody(req, MAX_UPLOAD);
    if (body === null) {
      sendJson(res, 413, { error: 'too_large', maxBytes: MAX_UPLOAD });
      return;
    }
    if (body.length < 5 || body.subarray(0, 4).toString('latin1') !== '%PDF') {
      sendJson(res, 400, { error: 'not_a_pdf' });
      return;
    }
    const src = path.join(dir, 'in.pdf');
    const dst = path.join(dir, 'out.pdf');
    await fs.promises.writeFile(src, body);
    const r = await compress({
      sourcePath: src,
      savePath: dst,
      preset,
      password: typeof password === 'string' ? password : null,
      options: brotli || preservePdfa || overrides
        ? {
            ...(overrides ?? {}),
            ...(brotli ? { brotli: true } : {}),
            ...(preservePdfa ? { preserveConformance: true } : {}),
          }
        : undefined,
      targetSizeBytes,
      timeoutMs: TIMEOUT_MS,
    });
    const out = await fs.promises.readFile(r.outputPath);
    res.writeHead(200, {
      'content-type': 'application/pdf',
      'content-length': out.length,
      'x-original-size': String(r.originalSize),
      'x-compressed-size': String(r.compressedSize),
      'x-signed': String(r.signed),
      'x-pdfa': r.pdfa ? `${r.pdfa.part}${r.pdfa.conformance}` : '',
      'x-met-target': r.metTarget === null ? '' : String(r.metTarget),
    });
    res.end(out);
  } catch (e) {
    if (e instanceof HyperError) {
      const status = e.code === 'decrypt_failed' ? 400 : e.code === 'timeout' ? 504 : 500;
      sendJson(res, status, { error: e.code });
    } else {
      sendJson(res, 500, { error: 'internal' });
    }
  } finally {
    release();
    fs.promises.rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`);
  if (req.method === 'GET' && url.pathname === '/') {
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'content-length': INDEX_HTML.length });
    res.end(INDEX_HTML);
    return;
  }
  if (req.method === 'GET' && url.pathname === '/healthz') {
    sendJson(res, 200, { ok: true, active, queued: waiting.length });
    return;
  }
  if (req.method === 'GET' && /^\/(dist|web)\/[A-Za-z0-9/_-]+\.js$/.test(url.pathname)) {
    const ROOT = path.resolve(HERE, '..', '..');
    const target = path.resolve(ROOT, '.' + url.pathname);
    if (target.startsWith(path.join(ROOT, 'dist') + path.sep) ||
        target.startsWith(path.join(ROOT, 'web') + path.sep)) {
      fs.readFile(target, (err, buf) => {
        if (err) { sendJson(res, 404, { error: 'not_found' }); return; }
        res.writeHead(200, { 'content-type': 'text/javascript; charset=utf-8', 'content-length': buf.length });
        res.end(buf);
      });
      return;
    }
  }
  if (req.method === 'POST' && url.pathname === '/api/verify-password') {
    void (async () => {
      const password = req.headers['x-password'];
      const dir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'hyperv-'));
      try {
        const body = await readBody(req, MAX_UPLOAD);
        if (body === null) {
          sendJson(res, 413, { error: 'too_large', maxBytes: MAX_UPLOAD });
          return;
        }
        const src = path.join(dir, 'in.pdf');
        await fs.promises.writeFile(src, body);
        const valid = await verifyPassword(src, typeof password === 'string' ? password : '');
        sendJson(res, 200, { valid });
      } catch {
        sendJson(res, 500, { error: 'internal' });
      } finally {
        fs.promises.rm(dir, { recursive: true, force: true }).catch(() => {});
      }
    })();
    return;
  }
  if (req.method === 'POST' && url.pathname === '/api/compress') {
    void handleCompress(req, res, url);
    return;
  }
  sendJson(res, 404, { error: 'not_found' });
});

server.listen(PORT, HOST, () => {
  console.log(`hyper-compress server on http://${HOST}:${PORT} (concurrency ${CONCURRENCY}, max upload ${MAX_UPLOAD / 1024 / 1024}MB)`);
});
