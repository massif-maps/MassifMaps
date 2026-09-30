#!/usr/bin/env node
// One JPEG per MapLibre variant, for the style's doc page: node screenshots.mjs <maplibre-dir> <out-dir>
// Needs `playwright` (and its chromium) resolvable; the release workflow installs both.
import { createServer } from 'node:http';
import { mkdirSync, readFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { chromium } from 'playwright';

const [styleDir, outDir] = process.argv.slice(2).map((p) => resolve(p));
// hybrid's imagery is a placeholder in the release; the site draws it over EOX's free Sentinel-2
// mosaic, as website/src/components/MassifStyleMap.js does
const SATELLITE = {
    type: 'raster', tileSize: 256, maxzoom: 15,
    tiles: ['https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless_3857/default/g/{z}/{y}/{x}.jpg'],
    attribution: 'Sentinel-2 cloudless by EOX IT Services GmbH (contains modified Copernicus Sentinel data 2016 & 2017)',
};
const SHOTS = [
    { variant: 'streets', center: [5.7245, 45.1885], zoom: 14 },
    { variant: 'outdoor', center: [5.7262, 45.1968], zoom: 14.6 },
    { variant: 'topo', center: [5.7262, 45.1968], zoom: 14.6 },
    { variant: 'hybrid', center: [5.7245, 45.1885], zoom: 13 },
    { variant: 'eink', center: [5.7245, 45.1885], zoom: 14 },
];
const TYPES = { '.json': 'application/json', '.png': 'image/png' };

const PAGE = `<!DOCTYPE html><html><head>
  <link rel="stylesheet" href="https://unpkg.com/maplibre-gl@5/dist/maplibre-gl.css">
  <script src="https://unpkg.com/maplibre-gl@5/dist/maplibre-gl.js"></script>
  <style>html,body,#map{margin:0;width:100%;height:100%}</style></head><body><div id="map"></div></body></html>`;

const server = createServer((req, res) => {
    if (req.url === '/') {
        res.writeHead(200, { 'Content-Type': 'text/html' }).end(PAGE);
        return;
    }
    try {
        const path = join(styleDir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
        res.writeHead(200, { 'Content-Type': TYPES[extname(path)] ?? 'application/octet-stream', 'Access-Control-Allow-Origin': '*' });
        res.end(readFileSync(path));
    } catch {
        res.writeHead(404).end();
    }
}).listen(0);
const base = `http://localhost:${server.address().port}`;

mkdirSync(outDir, { recursive: true });
const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--ignore-gpu-blocklist'] });
const page = await browser.newPage({ viewport: { width: 800, height: 520 }, deviceScaleFactor: 2 });
await page.goto(base + '/');
for (const shot of SHOTS) {
    await page.evaluate(async ({ base, shot, satellite }) => {
        const style = await (await fetch(`${base}/${shot.variant}.json`)).json();
        if (style.sources.satellite) style.sources.satellite = satellite;
        // the release's sprite URL is where the website WILL serve it; the local copy is drawn instead
        style.sprite = `${base}/sprite/sprite`;
        window.shotMap?.remove();
        window.shotMap = new maplibregl.Map({ container: 'map', style, center: shot.center, zoom: shot.zoom,
            attributionControl: false, canvasContextAttributes: { preserveDrawingBuffer: true } });
        await new Promise((done) => window.shotMap.once('idle', done));
    }, { base, shot, satellite: SATELLITE });
    await page.screenshot({ path: join(outDir, `${shot.variant}.jpg`), type: 'jpeg', quality: 82 });
    process.stdout.write(`${shot.variant}.jpg\n`);
}
await browser.close();
server.close();
