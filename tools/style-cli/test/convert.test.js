import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { resolveSpriteUrl } from '../dist/mapbox2css/sprite.js';
import { useMemorySpriteHost } from './sprite-host.js';

useMemorySpriteHost();


/** These tests assert on the translated literals, so they read the style before the palette
  * pass moves them out - see variables.test.js for the hoisting itself. */
const NO_PALETTE = { variables: false };
const NO_PALETTE_FOLD = { variables: false, foldCasings: true };

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

test('--ao-follows-height fades the ground AO on the ramp that lays the buildings down', () => {
    // The contact shadow belongs to the building standing in it: left at full strength under a
    // flattened city it reads as a dark ring around every footprint. Asserted here because it is a
    // STYLE decision - the renderer must not force it - so the converter is what has to emit it.
    const withAO = JSON.parse(JSON.stringify(style));
    const buildings = withAO.layers.find((layer) => layer.type === 'fill-extrusion');
    buildings.paint['fill-extrusion-ambient-occlusion-intensity'] = 0.3;

    // OFF by default: the shadow keeps the style's own value at every tilt.
    const plain = convert(withAO, table, NO_PALETTE).mss;
    const plainLine = plain.split('\n').find((l) => l.includes('building-ao-intensity:'));
    assert.ok(plainLine, 'the converter emits building-ao-intensity');
    assert.doesNotMatch(plainLine, /building_tilt_drop/);

    const { mss } = convert(withAO, table, { ...NO_PALETTE, aoFollowsHeight: true });
    const line = mss.split('\n').find((l) => l.includes('building-ao-intensity:'));
    assert.ok(line, 'the converter emits building-ao-intensity');
    // The live off switch, the style's own value, and the tilt ramp - all three, multiplied.
    assert.match(line, /\[param::building_ao\]/);
    assert.match(line, /\[param::building_tilt_drop\] \* 0\.01/);
    assert.match(line, /linear\(\[view::tilt\], \(80, 0\), \(90, 1\)\)/);
    // The same ramp the DRAWN height takes, so the two cannot drift apart.
    const ramp = /1 - \(\[param::building_tilt_drop\] \* 0\.01\) \* linear\(\[view::tilt\], \(80, 0\), \(90, 1\)\)/;
    assert.match(line, ramp);
    const height = mss.split('\n').find((l) => l.includes('building-height-view-scale:'));
    assert.ok(height, 'a style with no lights still lays its buildings down: building_tilt_drop drives it');
    assert.match(height, ramp);
});

test('a live-lit style says its colours are pre-lit, lights or not', () => {
    // Without it terrain lighting shades the ground again under the grading the renderer already gave it.
    assert.match(convert(style, table, { ...NO_PALETTE, liveLight: true }).mss, /Map \{[^}]*colors-prelit: 1;/);
    assert.doesNotMatch(convert(style, table, NO_PALETTE).mss, /colors-prelit/);
});

test('an extrusion opacity over the pitch stays live, as a ramp over the tilt', () => {
    // pitch 20 -> tilt 70, pitch 50 -> tilt 40: the keys flip and the stops run in reverse.
    const faded = JSON.parse(JSON.stringify(style));
    faded.metadata = { ...faded.metadata, 'massif:live-config': ['building_opacity'] };
    faded.schema = { building_opacity: { default: 0.6 } };
    const buildings = faded.layers.find((layer) => layer.type === 'fill-extrusion');
    buildings.paint['fill-extrusion-opacity'] = ['interpolate', ['linear'], ['pitch'],
        20, 1, 50, ['config', 'building_opacity']];
    const styleParams = new Map();
    const { mss } = convert(faded, table, { ...NO_PALETTE, styleParams });
    assert.match(mss, /building-fill-opacity: linear\(\[view::tilt\], \(40, \[param::building_opacity\]\), \(70, 1\)\);/);
    assert.equal(styleParams.get('building_opacity'), 0.6);
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
    const { coverage, mss } = run();
    assert.ok(coverage.dropped.has('layer type "heatmap"'), 'heatmap layer reported');
    assert.ok(!coverage.dropped.has('line-blur'), 'line-blur is carried now, not dropped');
    assert.equal(coverage.dropped.get('fill-antialias').reason, 'always on in the vt renderer');
    // An extrusion's LOOK is a Map setting, taken by buildingMapSettings before the per-layer pass
    // sees it. Reporting it dropped as well said a vertical gradient had been thrown away when it
    // is in the Map block - so this fixture's two drops are the only two, and both are real.
    assert.ok(!coverage.dropped.has('fill-extrusion-vertical-gradient'),
        'a building Map setting is carried, not dropped');
    assert.match(mss, /building-vertical-gradient:/);
    assert.equal(coverage.droppedCount, 2);
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
    // MapTiler ramps its footway dash with `step`, and taking nothing left every path drawn solid;
    // its disputed border ramps FROM a solid `[1, 0]`, which draws no dash below the step. An
    // interpolated ramp has no step to switch at: it takes the last stop that dashes, the one whose
    // line is widest - and a dash is a multiple of that width.
    const line = (dash) => ({ id: 'l', type: 'line', 'source-layer': 'pathway',
        paint: { 'line-dasharray': dash, 'line-width': 2 } });
    const mss = (dash) => convert({ layers: [line(dash)] }, table, NO_PALETTE).mss;

    assert.match(mss(['step', ['zoom'], ['literal', [1, 1]], 22, ['literal', [1, 1.5]]]),
        /line-dasharray: step\(\[zoom\], \(1, '2,2'\), \(23, '2,3'\)\);/);
    assert.match(mss({ stops: [[14, [0.5, 0.5]], [18, [0.3, 0.1]]] }), /line-dasharray: 0.6,0.2;/);
    // [1, 0] has no gap - it IS a solid line - so the pattern below it is the one to draw.
    assert.match(mss(['step', ['zoom'], ['literal', [1, 0]], 5, ['literal', [3, 2, 0.1, 2]]]),
        /line-dasharray: step\(\[zoom\], \(1, ''\), \(6, '6,4,1,3.2'\)\);/);
    // ...and a ramp that only ever states a solid pattern writes no dash at all, rather than a
    // "dash" as long as the line width scaled it.
    assert.ok(!mss(['step', ['zoom'], ['literal', [1, 0]], 5, ['literal', [1, 0]]]).includes('dasharray'));
});

test('a PLAIN dash over a ramped width steps its pattern over zoom bands, in one rule', () => {
    // Liberty's rail hatching: [0.2, 8] over a width running 3 px at z15 to 8 px at z20. One scale
    // cannot serve that - at 5.5 the dash drew 1.8x too long at the bottom of the range - and a
    // plain literal has no stop zoom of its own to be read at, the way a ramped dash does.
    const hatching = { id: 'l', type: 'line', 'source-layer': 'road', paint: {
        'line-width': ['interpolate', ['exponential', 1.4], ['zoom'], 14.5, 0, 15, 3, 20, 8],
        'line-dasharray': [0.2, 8],
    } };
    const out = convert({ layers: [hatching] }, table, NO_PALETTE).mss;
    const frames = dashFrames(out);

    assert.equal(frames.length, 2, 'a 2.7x width range is two bands, cut where the width doubles');
    assert.ok(Number(frames[0][1].split(',')[0]) < Number(frames[1][1].split(',')[0]), 'the lower band scales by the narrower line');
    // Measured from the first POSITIVE stop: the ramp starts at width 0, and below that there is
    // nothing for the dash to be in proportion to.
    assert.equal(frames[1][0], 19);
    assert.deepEqual(dashRules(out).map(([sel]) => sel), ['#road::l'], 'one rule, one attachment: a band each was a style each');
});

// Each rule of a converted layer, as [selector, dasharray or null].
const dashRules = (mss) => [...mss.matchAll(/^(#[^{]+)\{([^}]*)\}/gm)]
    .map((m) => [m[1].trim(), (m[2].match(/line-dasharray: ([^;]+);/) ?? [])[1] ?? null]);
// The bands of a `step([zoom], ...)` dash, as [zoom, pattern or null where the band is solid].
const dashFrames = (mss) => [...(mss.match(/line-dasharray: step\(\[zoom\], (.*)\);/) ?? [, ''])[1].matchAll(/\((\d+), '([^']*)'\)/g)]
    .map((m) => [Number(m[1]), m[2] === '' ? null : m[2]]);

test('a dash a style STEPS over zoom is a pattern per step, solid where the step is', () => {
    // Standard's stair treads: solid below z19, then a 0.1 dash. One rule drew the treads at every
    // zoom; below the step the line is solid, so that band carries no dash at all.
    const steps = { id: 'l', type: 'line', 'source-layer': 'road', paint: {
        'line-width': ['interpolate', ['exponential', 1.5], ['zoom'], 12, 0, 18, 6, 22, 80],
        'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 19, ['literal', [0.1, 0.1]]],
    } };
    const frames = dashFrames(convert({ layers: [steps] }, table, NO_PALETTE).mss);
    // the SDK's zoom is mapbox's + 1, so z19 is written 20
    assert.ok(frames.filter(([zoom]) => zoom < 20).every(([, dash]) => dash === null), 'solid below the step');
    const dashed = frames.filter(([, dash]) => dash !== null);
    assert.ok(dashed.length >= 1 && dashed.every(([zoom]) => zoom >= 20), 'dashed from the step on');
});

test('a stepped dash takes each step pattern, scaled by the width inside its own band', () => {
    // Standard's rail sleepers: [0.1, 15] to z16, [0.1, 1] to z18, [0.05, 0.5] past it, over a
    // width running 2 px to 32. As one rule they were 0.3 px every 3 px at z20, a grey band.
    const tracks = { id: 'l', type: 'line', 'source-layer': 'road', minzoom: 13, paint: {
        'line-width': ['interpolate', ['exponential', 1.5], ['zoom'], 16, 2, 18, 6, 20, 16, 22, 32],
        'line-dasharray': ['step', ['zoom'], ['literal', [0.1, 15]], 16, ['literal', [0.1, 1]],
            18, ['literal', [0.05, 0.5]]],
    } };
    const frames = dashFrames(convert({ layers: [tracks] }, table, NO_PALETTE).mss);
    const gaps = frames.map(([, dash]) => Number(dash.split(',')[1]));
    // 0.1 x 2 px is a 0.2 px sleeper, widened to 1 px out of its 30 px gap
    assert.equal(gaps[0], 29.2, 'below z16 the width is 2, so 15 widths is 30 px, less the pixel the sleeper takes');
    assert.ok(frames.every(([, dash]) => Number(dash.split(',')[0]) >= 1), 'and no sleeper is drawn under a pixel');
    assert.ok(gaps.length >= 3, 'a band per step');
    for (let i = 2; i < gaps.length; i++) assert.ok(gaps[i] > gaps[i - 1], 'and the gap grows with the line past the steps');
    assert.ok(gaps[gaps.length - 1] > 8, 'at z20+ a sleeper is metres apart, not a hatch');
});

test('a width whose stops are data-driven still scales the dash at that zoom', () => {
    // Standard's cycleway: every stop of the width ramp is a `match` on the type. Reading the ramp
    // failed on the first non-number and the dash fell back to the mean of every literal in it -
    // 13.3 px, where gl-js draws 1.3 at the zoom the pattern starts. The match's FALLBACK is the
    // width nearly every feature has; the piste branch is the exception.
    const cycleway = { id: 'l', type: 'line', 'source-layer': 'road', paint: {
        'line-width': ['interpolate', ['linear'], ['zoom'],
            12, ['match', ['get', 'type'], ['piste'], 0.5, 0],
            18, ['match', ['get', 'type'], ['piste'], 4, 2],
            22, ['match', ['get', 'type'], ['piste'], 40, 20]],
        'line-dasharray': ['step', ['zoom'], ['literal', [1]], 16, ['literal', [1, 1]]],
    } };
    const frames = dashFrames(convert({ layers: [cycleway] }, table, NO_PALETTE).mss);
    assert.equal(frames[0][1], null, 'solid below z16');
    // the band from z16 reads the fallback branch in its middle: 0 + (17.5-12)/6 * 2 = 1.83
    assert.deepEqual(frames[1], [17, '1.83,1.83']);
});

test('a width chosen per config value scales the dash by its fallback ramp', () => {
    // Massif's e-ink tracks are 1.6x wider: a match on the variant around two ramps. Averaged over
    // both, the grade2 [5, 2] dash drew 50 px where gl-js draws 5.
    const track = { id: 'l', type: 'line', 'source-layer': 'road', paint: {
        'line-width': ['match', ['config', 'variant'], 'eink',
            ['interpolate', ['linear'], ['zoom'], 12, 1.6, 16, 1.6],
            ['interpolate', ['linear'], ['zoom'], 12, 1, 16, 1]],
        'line-dasharray': [5, 2],
    } };
    const out = convert({ layers: [track] }, table, NO_PALETTE).mss;
    assert.match(out, /line-dasharray: 5,2;/);
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

test('--fold-casings leaves a pair alone when the fill orders its own features', () => {
    const pair = { layers: [
        { id: 'road-casing', type: 'line', source: 'osm', 'source-layer': 'transportation',
          paint: { 'line-color': '#c08a3e', 'line-width': 8 } },
        { id: 'road-fill', type: 'line', source: 'osm', 'source-layer': 'transportation',
          paint: { 'line-color': '#ffffff', 'line-width': 5 } },
    ] };
    // No sort key: the pair folds, and one rule draws the casing from the fill's own buffer.
    assert.match(convert(pair, table, NO_PALETTE_FOLD).mss, /line-border-width: \(\(8 - 5\) \/ 2\);/);

    // With one, the fill becomes a rule per class and a folded casing would draw over the road
    // beside it, so the fold is skipped and the casing keeps its own rules - all before the fills.
    pair.layers[1].layout = { 'line-sort-key': ['match', ['get', 'class'], 'motorway', 2, 1] };
    const ordered = convert(pair, table, NO_PALETTE_FOLD).mss;
    assert.ok(!ordered.includes('line-border-width'));
    // Whatever each side expands into, every casing rule comes before every fill rule - which is
    // the ordering a mapbox casing LAYER gives, and the whole point of not folding here.
    const rules = ordered.split('\n').filter((l) => l.startsWith('#transportation'));
    const lastCasing = rules.findLastIndex((r) => r.includes('::road_casing'));
    const firstFill = rules.findIndex((r) => r.includes('::road_fill'));
    assert.ok(lastCasing >= 0 && firstFill >= 0);
    assert.ok(lastCasing < firstFill, 'a casing rule is emitted after a fill rule');
});

test('a style carries its own fonts, and project.json names them for whoever ships it', () => {
    // The decoder scans <style>/fonts/ and needs no list; a project served over HTTP cannot be
    // listed, which is what the list is for. It is the only way a face reaches the web build.
    const layers = [{ id: 'l', type: 'symbol', 'source-layer': 'place',
        layout: { 'text-field': ['get', 'name'], 'text-font': ['Noto Sans Bold'] } }];
    const withFonts = convert({ layers }, table,
        { ...NO_PALETTE, fonts: ['NotoSans-Bold.ttf'] }).project;
    assert.deepEqual(JSON.parse(withFonts).fonts, ['NotoSans-Bold.ttf']);

    // A style that carries none says nothing rather than an empty list.
    assert.ok(!('fonts' in JSON.parse(convert({ layers }, table, NO_PALETTE).project)));
});


test('a palette the style asks for becomes a parameter table, read per feature', () => {
    // metadata is ignored by every renderer, so a layer can ask for this and stay a valid MapLibre
    // style. Opt-in per property: a table is worth it for a palette meant to be tuned, and not for
    // the two-branch colour ramp on a road - only the author knows which is which.
    const styleParams = new Map();
    const { mss } = convert({
        layers: [{
            id: 'poi-major', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi',
            metadata: { 'massif:params': ['text-color'] },
            layout: { 'text-field': ['get', 'name'] },
            paint: {
                'text-color': ['match', ['get', 'class'],
                    ['bus', 'railway'], '#2e5a80', 'park', '#4a7a3a', '#666666'],
            },
        }],
    }, table, { ...NO_PALETTE, styleParams });

    // The fallback stays in the rule: a class the table does not name still draws, and `??` is what
    // a parameter miss falls through on.
    assert.match(mss, /text-fill: \(\(get\(\[param::poi-fill\], \[class\]\)\) \?\? #666666\);/);
    assert.deepEqual(styleParams.get('poi-fill'), { default: { bus: '#2e5a80', railway: '#2e5a80', park: '#4a7a3a' } });
});

test('a parameter colour goes in as hex, because that is what the decoder can parse', () => {
    // A rule's hsl() is read by the CartoCSS compiler; a PARAMETER is a plain string parseColor has
    // to read at runtime, and its grammar knows #rrggbb, rgb() and the CSS names but not hsl().
    const styleParams = new Map();
    convert({
        layers: [{
            id: 'poi-major', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi',
            metadata: { 'massif:params': ['text-color'] },
            layout: { 'text-field': ['get', 'name'] },
            paint: {
                'text-color': ['match', ['get', 'class'],
                    'bus', 'hsl(216, 60%, 50%)', 'park', 'hsl(126, 42%, 40%)', '#666666'],
            },
        }],
    }, table, { ...NO_PALETTE, styleParams });

    assert.deepEqual(styleParams.get('poi-fill').default, { bus: '#3370cc', park: '#3b9144' });
});

test('a palette that follows the hour is one table per brightness stop', () => {
    // measure-light over two class matches: a lookup per stop, not the ternary chain per feature
    // the ramp would otherwise become.
    const styleParams = new Map();
    const { mss } = convert({
        layers: [{
            id: 'poi-major', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi',
            metadata: { 'massif:params': ['text-color'] },
            layout: { 'text-field': ['get', 'name'] },
            paint: {
                'text-color': ['interpolate', ['linear'], ['measure-light', 'brightness'],
                    0.25, ['match', ['get', 'class'], 'bus', '#aabbcc', '#eeeeee'],
                    0.3, ['match', ['get', 'class'], 'bus', '#2e5a80', '#666666']],
            },
        }],
    }, table, { ...NO_PALETTE, styleParams });

    assert.match(mss, /text-fill: linear\(\[view::brightness\], \(0\.25, \(\(get\(\[param::poi-fill-b25\], \[class\]\)\) \?\? #eeeeee\)\), \(0\.3, \(\(get\(\[param::poi-fill\], \[class\]\)\) \?\? #666666\)\)\);/);
    assert.equal(styleParams.get('poi-fill-b25').default.bus, '#aabbcc');
    assert.equal(styleParams.get('poi-fill').default.bus, '#2e5a80');
});

test('a palette per variant is one set of tables per variant, picked by the parameter', () => {
    // A match on a live config whose branches each fold: a per-draw parameter test, then one lookup,
    // instead of the per-feature chain a variant-dependent palette would otherwise become.
    const styleParams = new Map();
    const { mss } = convert({
        metadata: { 'massif:live-config': ['variant'] },
        schema: { variant: { default: 'streets', values: ['streets', 'eink'] } },
        layers: [{
            id: 'poi-major', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi',
            metadata: { 'massif:params': ['text-color'] },
            layout: { 'text-field': ['get', 'name'] },
            paint: {
                'text-color': ['match', ['config', 'variant'],
                    'eink', '#000000',
                    ['match', ['get', 'class'], 'bus', '#2e5a80', '#666666']],
            },
        }],
    }, table, { ...NO_PALETTE, styleParams });

    assert.match(mss, /text-fill: \(\(\[param::variant\] = 'eink'\) \? #000000 : \(\(get\(\[param::poi-fill\], \[class\]\)\) \?\? #666666\)\);/);
    assert.equal(styleParams.get('poi-fill').default.bus, '#2e5a80');
});

test('a day/night palette under a config branch keeps that branch its own tables', () => {
    // Folded without the branch's suffix, both branches wrote poi-fill-bus and the last one won.
    const styleParams = new Map();
    const ramp = (night, day) => ['interpolate', ['linear'], ['measure-light', 'brightness'],
        0.25, ['match', ['get', 'class'], 'bus', night, '#666666'], 0.3, ['match', ['get', 'class'], 'bus', day, '#666666']];
    const { mss } = convert({
        metadata: { 'massif:live-config': ['poiStyle'] },
        schema: { poiStyle: { default: 'badge', values: ['badge', 'plain'] } },
        layers: [{
            id: 'poi-major', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi',
            metadata: { 'massif:params': ['text-color'] },
            layout: { 'text-field': ['get', 'name'] },
            paint: { 'text-color': ['match', ['config', 'poiStyle'], 'plain', ramp('#111111', '#222222'), ramp('#333333', '#444444')] },
        }],
    }, table, { ...NO_PALETTE, styleParams, liveLight: true });

    assert.equal(styleParams.get('poi-fill-plain').default.bus, '#222222');
    assert.equal(styleParams.get('poi-fill-plain-b25').default.bus, '#111111');
    assert.equal(styleParams.get('poi-fill').default.bus, '#444444');
    assert.equal(styleParams.get('poi-fill-b25').default.bus, '#333333');
    assert.match(mss, /get\(\[param::poi-fill-plain\], \[class\]\)/);
});

test('a property the style does not ask for keeps its ternary', () => {
    const styleParams = new Map();
    const { mss } = convert({
        layers: [{
            id: 'poi-major', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi',
            layout: { 'text-field': ['get', 'name'] },
            paint: { 'text-color': ['match', ['get', 'class'], 'bus', '#2e5a80', '#666666'] },
        }],
    }, table, { ...NO_PALETTE, styleParams });

    assert.match(mss, /text-fill: \(\(\[class\] = 'bus'\) \? #2e5a80 : #666666\);/);
    assert.equal(styleParams.size, 0);
});

test('a zoom an app sets is a selector on a parameter, not a when() per feature', () => {
    const styleParams = new Map();
    const layer = (metadata) => ({ id: 'track', type: 'line', source: 'openmaptiles', 'source-layer': 'transportation',
        minzoom: 12, filter: ['==', ['get', 'class'], 'track'], metadata, paint: { 'line-color': '#000000' } });
    const convertWith = (metadata) => convert({ metadata: { 'massif:live-config': ['track_min_zoom'] },
        schema: { track_min_zoom: { default: 12 } }, layers: [layer(metadata)] },
        table, { ...NO_PALETTE, styleParams, tileDrawSize: 512 }).mss;

    // massif:minzoom-param replaces the layer's own start, which stays maplibre's minzoom
    const own = convertWith({ 'massif:minzoom-param': 'track_min_zoom' });
    assert.match(own, /#transportation\[zoom >= 'param::track_min_zoom'\]\[class = 'track'\]::track/);
    assert.equal(styleParams.get('track_min_zoom'), 12);
    // a layer starting BELOW its default keeps that floor beside the parameter
    const floored = convert({ metadata: { 'massif:live-config': ['track_min_zoom'] },
        schema: { track_min_zoom: { default: 14 } },
        layers: [layer({ 'massif:minzoom-param': 'track_min_zoom' })] }, table, { ...NO_PALETTE, tileDrawSize: 512 }).mss;
    assert.match(floored, /#transportation\[zoom >= 'param::track_min_zoom'\]\[zoom >= 12\]\[class = 'track'\]::track/);
    // and a zoom compared with a config in a filter brackets the same way
    const filtered = convertWith({ 'massif:filter': ['>=', ['zoom'], ['config', 'track_min_zoom']] });
    assert.match(filtered, /\[zoom >= 'param::track_min_zoom'\]/);
    assert.doesNotMatch(filtered, /when\(/);
});

test('a zoom range a child project moves is a project constant, resolved when the style compiles', () => {
    const { mss, project } = convert({ layers: [{
        id: 'bus-icon', type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi', minzoom: 17, maxzoom: 18,
        filter: ['==', ['get', 'class'], 'bus'], layout: { 'text-field': ['get', 'name'] },
        metadata: { 'massif:minzoom-const': 'bus_minzoom', 'massif:maxzoom-const': 'bus_label_minzoom' },
    }] }, table, { ...NO_PALETTE, tileDrawSize: 512 });

    assert.match(mss, /#poi\[zoom >= \$bus_minzoom\]\[zoom < \$bus_label_minzoom\]\[class = 'bus'\]::bus_icon/);
    assert.doesNotMatch(mss, /\[zoom >= 17\]|\[zoom < 18\]/, 'the constant replaces the literal range');
    assert.deepEqual(JSON.parse(project).constants, { bus_label_minzoom: 18, bus_minzoom: 17 });
});

test('layers of one template share it and one attachment, keeping only what they state differently', () => {
    const poi = (id, minzoom, color) => ({
        id, type: 'symbol', source: 'openmaptiles', 'source-layer': 'poi', minzoom,
        layout: { 'text-field': ['get', 'name'], 'text-size': 12 }, paint: { 'text-color': color },
        metadata: { 'massif:template': 'poi', 'massif:attachment': 'poi' },
    });
    const { mss } = convert({ layers: [poi('poi-a', 15, '#111111'), poi('poi-b', 16, '#111111'), poi('poi-c', 17, '#222222')] },
        table, { ...NO_PALETTE, tileDrawSize: 512 });

    assert.equal(mss.match(/^%poi \{/gm)?.length, 1, 'one template');
    assert.match(mss, /%poi \{[^}]*text-size: 12;[^}]*\}/, 'what every layer states');
    assert.match(mss, /%poi \{[^}]*text-fill: #111111;[^}]*\}/, 'at its most common value');
    assert.match(mss, /#poi\[zoom >= 17\]::poi \{\n  text-fill: #222222;\n[^}]*@extend %poi;\n\}/, 'a rule keeps what differs');
    assert.match(mss, /#poi\[zoom >= 15\]::poi \{\n  @extend %poi;\n\}/);
    assert.doesNotMatch(mss, /::poi_a|::poi_b|::poi_c/, 'all three draw into ::poi');
});

test('massif:draw-once names one group on every attachment a sort key splits the layer into', () => {
    const road = (metadata) => ({ id: 'road', type: 'line', source: 'openmaptiles', 'source-layer': 'transportation',
        metadata, layout: { 'line-sort-key': ['match', ['get', 'class'], 'motorway', 2, 1] },
        paint: { 'line-color': 'rgba(255, 255, 255, 0.5)' } });
    const convertWith = (metadata) => convert({ metadata: { 'massif:live-config': ['variant'] },
        schema: { variant: { default: 'streets', values: ['streets', 'hybrid'] } }, layers: [road(metadata)] },
        table, NO_PALETTE).mss;

    const constant = convertWith({ 'massif:draw-once': 'road' });
    assert.equal(constant.match(/::road_b\d+ \{[^}]*draw-once: 'road';/g)?.length, 2);
    // a family picks it per variant, so the other variants keep their plain lines
    const picked = convertWith({ 'massif:draw-once': ['match', ['config', 'variant'], 'hybrid', 'road', ''] });
    assert.match(picked, /draw-once: \(\(\[param::variant\] = 'hybrid'\) \? 'road' : ''\);/);
    assert.doesNotMatch(convertWith(undefined), /draw-once/);
    assert.doesNotMatch(convertWith({ 'massif:draw-once': '' }), /draw-once/);
});

test('a circle\'s radius becomes a marker\'s width, which is a diameter', () => {
    const out = convert({ layers: [{ id: 'dot', type: 'circle', source: 'openmaptiles', 'source-layer': 'poi',
        paint: { 'circle-radius': 5, 'circle-color': '#ff0000' } }] }, table, NO_PALETTE).mss;
    assert.match(out, /marker-width: \(2 \* 5\);/);
    assert.match(out, /marker-allow-overlap: true;/, 'a circle never collides');
});

test('under lights a wall\'s foot takes gl-js\'s faux AO, not the unlit vertical gradient', () => {
    const lit = JSON.parse(JSON.stringify(style));
    lit.lights = [{ id: 'ambient', type: 'ambient', properties: { intensity: 0.8 } },
        { id: 'sun', type: 'directional', properties: { intensity: 0.2, direction: [180, 20] } }];
    const buildings = lit.layers.find((layer) => layer.type === 'fill-extrusion');
    buildings.paint['fill-extrusion-ambient-occlusion-intensity'] = 0.15;
    const { mss } = convert(lit, table, NO_PALETTE);
    // 1 - (1 - 0.08 * 0.15) * (1 - 0.9 * 0.15) = 0.1454, to two places
    assert.match(mss, /building-vertical-gradient: 0\.15;/);
    assert.match(mss, /building-vertical-gradient-height: 6;/);
});

test('a hillshade with SDK values becomes a composite slot at its own depth', () => {
    const hillshade = {
        id: 'hillshade', type: 'hillshade', source: 'dem', maxzoom: 16,
        metadata: { 'massif:sdk-layer': { type: 'hillshade', exaggeration: 0.35, opacity: 0.55, visibleZoomRange: [0, 16] } },
    };
    const contour = { id: 'contour', type: 'line', 'source-layer': 'contour', paint: { 'line-color': '#a08060' } };
    const out = convert({ layers: [hillshade, contour] }, table, NO_PALETTE);
    assert.match(out.mss, /#hillshade\[zoom < \d+\] \{\s*hillshade-opacity: 0\.55;\s*hillshade-exaggeration: 0\.35;/);
    // TOP -> BOTTOM: the contours over the relief, as the style draws them.
    assert.deepEqual(JSON.parse(out.project).layers, ['contour', 'hillshade']);
    // Without the SDK values there is nothing to configure a slot with: dropped, as before.
    assert.doesNotMatch(convert({ layers: [{ ...hillshade, metadata: {} }] }, table, NO_PALETTE).mss, /#hillshade/);
});

test('a hillshade slot ends at its massif:maxzoom-param rather than its visibleZoomRange', () => {
    const hillshade = {
        id: 'hillshade', type: 'hillshade', source: 'dem', maxzoom: 16,
        metadata: { 'massif:sdk-layer': { type: 'hillshade', contrast: 0.35, visibleZoomRange: [0, 16] },
            'massif:maxzoom-param': 'hillshade_max_zoom' },
    };
    const mss = convert({ layers: [hillshade] }, table, { ...NO_PALETTE, tileDrawSize: 512 }).mss;
    assert.match(mss, /#hillshade\[zoom < 'param::hillshade_max_zoom'\] \{/);
    // a zoom offset shifts every literal, which a parameter cannot follow: the static end stays
    const shifted = convert({ layers: [hillshade] }, table, NO_PALETTE).mss;
    assert.match(shifted, /#hillshade\[zoom < \d+\] \{/);
    assert.doesNotMatch(shifted, /param::hillshade_max_zoom/);
});

test('a hillshade slot carries the settings that match the MapLibre paint', () => {
    const hillshade = {
        id: 'hillshade', type: 'hillshade', source: 'dem', maxzoom: 16,
        metadata: { 'massif:sdk-layer': { type: 'hillshade', hillshadeMethod: 'STANDARD', contrast: 0.35, heightScale: 1,
            shadowColor: '#544d45', highlightColor: '#faf8f5', accentColor: '#847362', visibleZoomRange: [0, 16] } },
    };
    const mss = convert({ layers: [hillshade] }, table, NO_PALETTE).mss;
    assert.match(mss, /hillshade-contrast: 0\.35;/);
    assert.match(mss, /hillshade-height-scale: 1;/);
    // CompositeVectorTileLayer's parser only knows the lower-case names
    assert.match(mss, /hillshade-method: '?standard'?;/);
    assert.match(mss, /hillshade-shadow-color: #544d45;/);
    assert.match(mss, /hillshade-highlight-color: #faf8f5;/);
    assert.match(mss, /hillshade-accent-color: #847362;/);
    assert.doesNotMatch(mss, /hillshade-exaggeration/);
});

test('the Map block records the TileDrawSize its zoom numbers are written for', () => {
    // The SDK shifts every zoom by log2(app / this): Massif is written for 512, and an app left on
    // the default 256 drew every road a level wide until it did.
    const style = { layers: [{ id: 'l', type: 'line', 'source-layer': 'road', paint: { 'line-width': 1 } }] };
    assert.match(convert(style, table, { ...NO_PALETTE, tileDrawSize: 512 }).mss, /tile-draw-size: 512;/);
    assert.match(convert(style, table, NO_PALETTE).mss, /tile-draw-size: 256;/);
});
