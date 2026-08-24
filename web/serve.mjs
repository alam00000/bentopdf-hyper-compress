import * as fs from 'node:fs';
import * as http from 'node:http';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PORT = Number(process.argv[2] ?? 8787);

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.css': 'text/css; charset=utf-8',
  '.pdf': 'application/pdf',
};

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);

  if (url.pathname === '/' || url.pathname === '/web' || url.pathname === '/web/') {
    res.writeHead(302, { location: '/web/index.html' }).end();
    return;
  }

  const rel = decodeURIComponent(url.pathname);
  const filePath = path.join(ROOT, rel);

  if (!filePath.startsWith(ROOT + path.sep)) {
    res.writeHead(403).end('forbidden');
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'content-type': 'text/plain' }).end(`not found: ${rel}`);
      return;
    }
    res.writeHead(200, {
      'content-type': TYPES[path.extname(filePath)] ?? 'application/octet-stream',
      'cache-control': 'no-store',
    });
    res.end(data);
  });
});

server.listen(PORT, () => {
  console.log(`Hyper Compress test page: http://localhost:${PORT}/`);
  console.log(`serving ${ROOT}`);
});
