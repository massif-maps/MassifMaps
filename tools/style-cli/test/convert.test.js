import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { resolveSpriteUrl } from '../dist/mapbox2css/sprite.js';

/** These tests assert on the translated literals, so they read the style before the palette
  * pass moves them out - see variables.test.js for the hoisting itself. */
const NO_PALETTE = { variables: false };

const HERE = dirname(fileURLToPath(import.meta.url));
const style = JSON.parse(readFileSync(join(HERE, 'fixtures', 'style.json'), 'utf8'));
const table = JSON.parse(readFileSync(join(HERE, '..', 'src', 'generated', 'properties.json'), 'utf8'));

function run() {
    return convert(style, table, NO_PALETTE);
}

test('background becomes the Map block', () => {
    const { mss } = run();
    // The Map block also carries the building lighting settings when a style has buildings.
    assert.match(mss, /Map \{\n(?:\s+[a-z-]+: [^\n]*\n)*\}/);
    assert.match(mss, /\n\s+background-color: #f0f0f0;\n/);
});

test('each MapBox layer becomes an attachment on its source-layer', () => {
    const { mss } = run();
    assert.match(mss, /#transportation\[zoom >= 7\]\[zoom < 21\].*::road_casing \{/);
    // road-fill's colour and opacity read a field, and both stay one expression - the decoder
    // evaluates them per feature, so there is nothing to split. See split.test.js.
    assert.match(mss, /#transportation.*::road_fill \{/);
    assert.match(mss, /#landcover\[class = 'wood'\]::landcover \{/);
});

test('the project layer list is the draw order REVERSED', () => {
    // loadMapProject inserts at begin(), so the last entry is drawn first (bottom).
    const { project } = run();
    assert.deepEqual(JSON.parse(project).layers, [
        'building',
        'transportation_name',
        'transportation',
        'landcover',
    ]);
});

test('zoom-driven paint stays a per-frame function', () => {
    const { mss } = run();
    assert.match(mss, /line-width: linear\(\(\[view::zoom\] - 1\), \(6, 1\), \(16, 12\)\);/);
    assert.match(mss, /line-width: step\(\(\[view::zoom\] - 1\), \(0, 1\), \(10, 2\), \(14, 6\)\);/);
});

test('unsupported layers and properties are dropped AND counted', () => {
    const { coverage } = run();
    assert.ok(coverage.dropped.has('layer type "heatmap"'), 'heatmap layer reported');
    assert.ok(!coverage.dropped.has('line-blur'), 'line-blur is carried now, not dropped');
    assert.equal(coverage.dropped.get('fill-antialias').reason, 'always on in the vt renderer');
    assert.ok(coverage.droppedCount >= 3);
    assert.match(coverage.report(), /^Coverage: \d+\/\d+ properties \(\d+%\)/);
});

test('every emitted property exists in the generated allowlist', () => {
    const { mss, coverage } = run();
    const allowed = new Set(table.properties.map((p) => p.cartocss));
    for (const property of coverage.emitted.keys()) {
        // Map settings, not symbolizer properties: they are declared in CartoCSSMapLoader, so the
        // generated symbolizer table does not list them.
        if (property === 'background-color' || property.startsWith('building-')) continue;
        assert.ok(allowed.has(property), `${property} is not a known CartoCSS property`);
    }
    assert.ok(mss.length > 0);
});

/**
 * The end-to-end check: the generated CartoCSS has to survive the real compiler, not just look
 * plausible. Skipped when massif-style is not built, so `npm test` still runs without a toolchain.
 */
test('the generated CartoCSS compiles with massif-style', (t) => {
    const binary = process.env.MASSIF_STYLE_BIN;
    if (!binary) {
        t.skip('set MASSIF_STYLE_BIN to the massif-style binary to run this');
        return;
    }
    const { mss, project } = run();
    const dir = mkdtempSync(join(tmpdir(), 'mapbox2css-'));
    writeFileSync(join(dir, 'style.mss'), mss);
    writeFileSync(join(dir, 'project.json'), project);

    execFileSync(binary, ['css2xml', join(dir, 'project.json'), join(dir, 'style.xml')], {
        stdio: 'pipe',
    });
    const xml = readFileSync(join(dir, 'style.xml'), 'utf8');
    assert.match(xml, /<Map/);
    assert.match(xml, /LineSymbolizer/);
});

test('a dash ramped over zoom still dashes, at the LAST stop that dashes', () => {
    // CartoCSS takes ONE dash pattern. MapTiler ramps its footway dash with `step`, and taking
    // nothing left every path drawn solid; its disputed border ramps FROM a solid `[1, 0]`, so the
    // base is not the answer either. Between two stops that both dash, the last one is the one
    // whose line is widest - and a dash is a multiple of that width.
    const line = (dash) => ({ id: 'l', type: 'line', 'source-layer': 'pathway',
        paint: { 'line-dasharray': dash, 'line-width': 2 } });
    const mss = (dash) => convert({ layers: [line(dash)] }, table, NO_PALETTE).mss;

    assert.match(mss(['step', ['zoom'], ['literal', [1, 1]], 22, ['literal', [1, 1.5]]]),
        /line-dasharray: 2,3;/);
    assert.match(mss({ stops: [[14, [0.5, 0.5]], [18, [0.3, 0.1]]] }), /line-dasharray: 0.6,0.2;/);
    // [1, 0] has no gap - it IS a solid line - so the pattern below it is the one to draw.
    assert.match(mss(['step', ['zoom'], ['literal', [1, 0]], 5, ['literal', [3, 2, 0.1, 2]]]),
        /line-dasharray: 6,4,0.2,4;/);
    // ...and a ramp that only ever states a solid pattern writes no dash at all, rather than a
    // "dash" as long as the line width scaled it.
    assert.ok(!mss(['step', ['zoom'], ['literal', [1, 0]], 5, ['literal', [1, 0]]]).includes('dasharray'));
});

test('a dash is scaled by the line width AT the zoom its stop starts', () => {
    // MapBox dash lengths are multiples of the line width, and Standard's steps ramp that width to
    // 80 px by z22: the mean of the stops is 43, so a 0.1 dash came out at 4.3 px where gl-js
    // draws 1.5 - coarse bands instead of fine treads.
    const steps = { id: 'l', type: 'line', 'source-layer': 'road', paint: {
        'line-width': ['interpolate', ['exponential', 1.5], ['zoom'], 12, 0, 18, 6, 22, 80],
        'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 17, ['literal', [0.2, 0.2]],
            19, ['literal', [0.1, 0.1]]],
    } };
    const out = convert({ layers: [steps] }, table, NO_PALETTE).mss;
    // width(19) = 15.1 for that ramp, and 0.1 of it is what gl-js draws there.
    assert.match(out, /line-dasharray: 1.51,1.51;/);
});

test('a fill pattern names a FILE, not the sheet-qualified sprite', () => {
    // 'misc:construction_pattern' reached the decoder verbatim and no such file has ever existed,
    // so every construction area drew as a bare outline. The sheet only says where to look.
    const sprites = new Map([['misc', {
        index: { construction_pattern: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: false } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    const out = convert({ layers: [{ id: 'c', type: 'fill', 'source-layer': 'construction',
        paint: { 'fill-pattern': 'misc:construction_pattern' } }] },
    table, { sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;
    assert.match(out, /polygon-pattern-file: url\('icons\/construction_pattern.png'\);/);
});

test('a pattern takes the fill opacity, and the fill colour under it is dropped', () => {
    // MapBox disables fill-color under a fill-pattern and fades the PATTERN with fill-opacity.
    // polygon-* is a different symbolizer from polygon-pattern-*, so those went to a solid layer
    // under the hatch instead: MapTiler's 0.15 construction areas drew fully saturated.
    const sprites = new Map([['misc', {
        index: { construction_pattern: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: false } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    const out = convert({ layers: [{ id: 'c', type: 'fill', 'source-layer': 'construction',
        paint: { 'fill-color': '#ffffff', 'fill-opacity': 0.15, 'fill-pattern': 'misc:construction_pattern' } }] },
    table, { sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;
    assert.match(out, /polygon-pattern-opacity: 0.15;/);
    assert.ok(!out.includes('polygon-fill'), 'a solid fill under the hatch is not what MapBox draws');
    assert.ok(!out.includes('polygon-opacity'), 'and polygon-opacity would fade only that fill');
});

test("a mapbox:// sprite names an API URL, since nothing can fetch that scheme", () => {
    // Mapbox styles name their sheet mapbox://sprites/<user>/<style>/<hash>; fetchBuffer read it as
    // a file path and every icon in Standard was dropped.
    assert.equal(resolveSpriteUrl('mapbox://sprites/mapbox/standard/7ixcpyhbhz67em71mmpln1klo'),
        'https://api.mapbox.com/styles/v1/mapbox/standard/sprite');
    assert.equal(resolveSpriteUrl('https://example.com/sprite'), 'https://example.com/sprite');
});
