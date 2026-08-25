import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const style = JSON.parse(readFileSync(join(HERE, 'fixtures', 'style.json'), 'utf8'));
const table = JSON.parse(readFileSync(join(HERE, '..', 'src', 'generated', 'properties.json'), 'utf8'));

function run() {
    return convert(style, table);
}

test('background becomes the Map block', () => {
    const { mss } = run();
    assert.match(mss, /Map \{\n\s+background-color: #f0f0f0;\n\}/);
});

test('each MapBox layer becomes an attachment on its source-layer', () => {
    const { mss } = run();
    assert.match(mss, /#transportation\[zoom >= 6\]\[zoom < 20\].*::road_casing \{/);
    assert.match(mss, /#transportation::road_fill \{/);
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
    assert.match(mss, /line-width: linear\(\[view::zoom\], \(6, 1\), \(16, 12\)\);/);
    assert.match(mss, /line-width: step\(\[view::zoom\], \(0, 1\), \(10, 2\), \(14, 6\)\);/);
});

test('unsupported layers and properties are dropped AND counted', () => {
    const { coverage } = run();
    assert.ok(coverage.dropped.has('layer type "heatmap"'), 'heatmap layer reported');
    assert.equal(coverage.dropped.get('line-blur').reason, 'no CartoCSS equivalent');
    assert.equal(coverage.dropped.get('fill-antialias').reason, 'always on in the vt renderer');
    assert.ok(coverage.droppedCount >= 4);
    assert.match(coverage.report(), /^Coverage: \d+\/\d+ properties \(\d+%\)/);
});

test('every emitted property exists in the generated allowlist', () => {
    const { mss, coverage } = run();
    const allowed = new Set(table.properties.map((p) => p.cartocss));
    for (const property of coverage.emitted.keys()) {
        if (property === 'background-color') continue; // a Map setting, not a symbolizer property
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
