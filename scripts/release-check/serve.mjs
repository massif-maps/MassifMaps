#!/usr/bin/env node
/*
 * Serves index.html with the installed @massif-maps/web and @massif-maps/styles, cross-origin
 * isolated as docs/getting-started/web.md asks of any host.
 *   npm run serve        # http://localhost:8099, PORT=... to change
 */
import { createReadStream, statSync } from 'node:fs';
import { createServer } from 'node:http';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.PORT ?? 8099);
const roots = {
  '/massif/': path.join(here, 'node_modules', '@massif-maps', 'web'),
  '/styles/': path.join(here, 'node_modules', '@massif-maps', 'styles'),
};
const types = {
  '.html': 'text/html', '.mjs': 'text/javascript', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.ttf': 'font/ttf',
};

createServer((req, res) => {
  const url = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  const prefix = Object.keys(roots).find((p) => url.startsWith(p));
  const file = prefix ? path.join(roots[prefix], url.slice(prefix.length)) : path.join(here, 'index.html');
  if (prefix && !file.startsWith(roots[prefix])) return res.writeHead(403).end();
  try {
    if (!statSync(file).isFile()) throw new Error();
  } catch {
    return res.writeHead(404).end();
  }
  res.writeHead(200, {
    'Content-Type': types[path.extname(file)] ?? 'application/octet-stream',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'credentialless',
  });
  createReadStream(file).pipe(res);
}).listen(port, () => console.log(`http://localhost:${port}  (?variant=full, ?style=outdoor|topo|eink)`));
