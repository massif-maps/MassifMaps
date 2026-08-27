import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { importOnly, resolveConfig, sceneBrightness } from '../dist/mapbox2css/config.js';
import { foldConfig } from '../dist/mapbox2css/fold.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const TABLE = JSON.parse(readFileSync(join(HERE, '..', 'src', 'generated', 'properties.json'), 'utf8'));

const fold = (node, values, scene) => foldConfig(node, new Map(Object.entries(values)), scene);

/** Mapbox Standard's own idiom: pull a config colour apart, adjust a channel, put it back. */
const HSL_IDIOM = ['let',
    'h', ['at', 0, ['to-hsla', ['config', 'colorLand']]],
    's', ['at', 1, ['to-hsla', ['config', 'colorLand']]],
    'l', ['at', 2, ['to-hsla', ['config', 'colorLand']]],
    ['hsl', ['var', 'h'], ['var', 's'], ['-', ['var', 'l'], 10]]];

test('the config colour idiom folds to a plain colour', () => {
    // 878 config reads across 150 layers, nearly all of them wrapped in this. CartoCSS has no
    // let/var/at/to-hsla, so left alone it is 74 colour properties that convert to nothing at all.
    assert.equal(fold(HSL_IDIOM, { colorLand: 'hsl(20, 20%, 95%)' }), 'hsl(20, 20%, 85%)');
});

test('a config value nobody set is left as an expression, not guessed at', () => {
    assert.deepEqual(fold(['config', 'unknownKnob'], { other: 1 }), ['config', 'unknownKnob']);
});

test('a match on a folded config keeps only the branch it lands on', () => {
    const expr = ['match', ['config', 'lightPreset'], 'night', '#000', 'day', '#fff', '#888'];
    assert.equal(fold(expr, { lightPreset: 'night' }), '#000');
    assert.equal(fold(expr, { lightPreset: 'dusk' }), '#888', 'no branch matches, so the fallback');
});

test('a case keeps the branches that are still undecided', () => {
    // Structure has to survive: every preset shares one style.mss, so a fold may only remove what
    // is now decided - never reorder or rewrite what still reads a feature.
    const expr = ['case', ['==', ['config', 'show'], false], '#000', ['==', ['get', 'x'], 1], '#111', '#222'];
    assert.deepEqual(fold(expr, { show: true }), ['case', ['==', ['get', 'x'], 1], '#111', '#222']);
    assert.equal(fold(expr, { show: false }), '#000');
});

test('measure-light resolves to the scene brightness, and its ramp with it', () => {
    // Standard reads the light 113 times to say "night" without naming the preset.
    // Outside the stops the endpoint is taken VERBATIM - no round trip through HSL, so a colour
    // the style wrote as hex stays hex and the palette reads the way the source did.
    const lit = ['interpolate', ['linear'], ['measure-light', 'brightness'], 0.25, '#000000', 0.3, '#ffffff'];
    assert.equal(fold(lit, {}, { brightness: 0.8 }), '#ffffff');
    assert.equal(fold(lit, {}, { brightness: 0.06 }), '#000000');
    // Between them it is a real blend, in the space the stops were written in.
    assert.equal(fold(lit, {}, { brightness: 0.275 }), 'hsl(0, 0%, 50%)');
});

test('an input below the first stop clamps, rather than dividing by a zero-width span', () => {
    // night's brightness of 0.06 under a ramp starting at 0.1 produced a literal NaN in the
    // opacity of every landuse polygon - a value the SDK reads as garbage, not as a drop.
    const ramp = ['interpolate', ['linear'], ['measure-light', 'brightness'], 0.1, 0.8, 0.4, 1];
    assert.equal(fold(ramp, {}, { brightness: 0.06 }), 0.8);
    assert.equal(fold(ramp, {}, { brightness: 9 }), 1, 'and above the last stop too');
});

test('a ramp whose stops are still expressions snaps to the nearer end', () => {
    // Standard ramps a per-feature `match` over the brightness; at dusk the value lands between the
    // two and there is nothing to blend. Snapping keeps ONE value per property, which is what every
    // preset sharing a style.mss depends on.
    const perFeature = (c) => ['match', ['get', 'class'], 'a', c, c];
    const ramp = ['interpolate', ['linear'], ['measure-light', 'brightness'], 0.2, perFeature('#111'), 0.4, perFeature('#222')];
    assert.deepEqual(fold(ramp, {}, { brightness: 0.23 }), perFeature('#111'));
    assert.deepEqual(fold(ramp, {}, { brightness: 0.38 }), perFeature('#222'));
});

test('an arithmetic result that is not a finite number is left unfolded', () => {
    assert.deepEqual(fold(['/', ['config', 'a'], 0], { a: 1 }), ['/', 1, 0]);
});

test('the schema gives the defaults and imports override them', () => {
    const { parameters } = resolveConfig({
        schema: { lightPreset: { default: 'day', values: ['dawn', 'day', 'dusk', 'night'] } },
        imports: [{ id: 'basemap', config: { lightPreset: 'night' } }],
        layers: [],
    });
    assert.equal(parameters.get('lightPreset').default, 'night');
    assert.deepEqual(Object.keys(parameters.get('lightPreset').values), ['dawn', 'day', 'dusk', 'night']);
});

test('a config read that no schema declares is reported, not silently left dangling', () => {
    const { undeclared } = resolveConfig({
        layers: [{ id: 'l', type: 'fill', 'source-layer': 's', paint: { 'fill-color': ['config', 'mystery'] } }],
    });
    assert.deepEqual(undeclared, ['mystery']);
});

test('a root style with no layers of its own says where its layers are', () => {
    // Standard's root document is an `imports` entry and a config block; converting it silently
    // produced an empty style.
    const why = importOnly({ imports: [{ id: 'basemap', url: 'mapbox://styles/mapbox/standard' }], layers: [] });
    assert.match(why, /imports mapbox:\/\/styles\/mapbox\/standard/);
    assert.equal(importOnly({ layers: [{ id: 'a' }] }), null, 'a flattened style is not import-only');
});

test('the brightness comes from the style, not from a table of guesses', () => {
    const lights = [{ id: 'ambient', type: 'ambient', properties: {
        color: ['match', ['config', 'lightPreset'], 'night', 'hsl(217, 100%, 11%)', 'hsl(0, 0%, 100%)'],
        intensity: ['match', ['config', 'lightPreset'], 'night', 0.5, 0.8] } }];
    const at = (preset) => sceneBrightness(lights, (node) => fold(node, { lightPreset: preset }));
    assert.equal(Math.round(at('day') * 100) / 100, 0.8);
    assert.equal(Math.round(at('night') * 100) / 100, 0.06);
});

/** A configurable style, in the shape Standard uses: one colour per preset, one shared by both. */
const CONFIGURABLE = {
    name: 'Cfg',
    schema: { lightPreset: { default: 'day', values: ['day', 'night'] } },
    lights: [{ id: 'ambient', type: 'ambient', properties: {
        color: ['match', ['config', 'lightPreset'], 'night', 'hsl(0, 0%, 10%)', 'hsl(0, 0%, 100%)'],
        intensity: 1 } }],
    layers: [
        { id: 'water', type: 'fill', 'source-layer': 'water',
            paint: { 'fill-color': ['match', ['config', 'lightPreset'], 'night', '#001133', '#a0c8f0'] } },
        // Shares the water's colour by day and not by night - the case that used to make the two
        // palettes name their variables differently.
        { id: 'pond', type: 'fill', 'source-layer': 'pond',
            paint: { 'fill-color': ['match', ['config', 'lightPreset'], 'night', '#002244', '#a0c8f0'] } },
    ],
};

test('every light preset gets a palette, over ONE shared style.mss', () => {
    const { mss, presets, variables } = convert(CONFIGURABLE, TABLE);
    assert.deepEqual([...presets.keys()], ['night']);
    assert.match(variables, /@water_fill: #a0c8f0;/);
    assert.match(presets.get('night'), /@water_fill: #001133;/);
    assert.ok(!mss.includes('#a0c8f0'), 'the literal moved into the palette, not into the rules');
});

test('the palettes share a name set, or they are not drop-in', () => {
    // Keyed on the default preset alone, `water` and `pond` share one variable by day and need two
    // by night: the night palette then declared names style.mss never mentions.
    const { presets, variables } = convert(CONFIGURABLE, TABLE);
    const names = (palette) => palette.match(/^@[a-z0-9_]+/gm).sort();
    assert.deepEqual(names(presets.get('night')), names(variables));
    assert.match(presets.get('night'), /@pond_fill: #002244;/);
    assert.match(variables, /@pond_fill: #a0c8f0;/);
});

test('a preset whose rules do not line up is refused rather than half-written', () => {
    const diverging = { ...CONFIGURABLE, layers: [
        { id: 'water', type: 'fill', 'source-layer': 'water', paint: {
            // `within` has no CartoCSS form, so the night branch drops the property entirely and
            // the two passes no longer carry the same declarations.
            'fill-color': ['match', ['config', 'lightPreset'], 'night', ['within', {}], '#a0c8f0'] } },
    ] };
    const { presets, coverage } = convert(diverging, TABLE);
    assert.equal(presets.size, 0);
    assert.match(coverage.report(), /preset "night"/);
});

test('turning the presets off leaves one palette', () => {
    const { presets } = convert(CONFIGURABLE, TABLE, { presets: [] });
    assert.equal(presets.size, 0);
});

test('a style with no config is untouched by any of this', () => {
    const plain = { layers: [{ id: 'w', type: 'fill', 'source-layer': 'water', paint: { 'fill-color': '#a0c8f0' } }] };
    const { presets, mss } = convert(plain, TABLE);
    assert.equal(presets.size, 0);
    assert.match(mss, /polygon-fill: @w_fill;/);
});

test('a step over a FIELD becomes ternaries; over zoom it stays a function', () => {
    // Standard sizes 10 label layers by ["step", ["get", "sizerank"], …]: 35 text-sizes dropped,
    // and unlike interpolate there is nothing to unroll - a step has one outcome per stop.
    const label = (size) => ({ id: 'l', type: 'symbol', 'source-layer': 'place',
        layout: { 'text-field': '{name}', 'text-size': size } });
    const sized = (size) => convert({ layers: [label(size)] }, TABLE, { variables: false }).mss;
    assert.match(sized(['step', ['get', 'sizerank'], 18, 5, 12]), /text-size: \(\(\[sizerank\] >= 5\) \? 12 : 18\);/);
    assert.match(sized(['step', ['zoom'], 18, 5, 12]), /text-size: step\(\[view::zoom\], \(0, 18\), \(5, 12\)\);/);
});

test('the viewport terms resolve to a flat, centred view, so their clause folds away', () => {
    // road-label's filter is true below 40 degrees of pitch and progressively stricter above it.
    // Left unresolved it is an untranslatable filter, which drops the whole LAYER, not just the
    // test - 27 layers, every label in the style.
    const clause = ['case', ['<=', ['pitch'], 40], true,
        ['step', ['pitch'], true, 40, ['<', ['distance-from-center'], 1]]];
    assert.equal(fold(clause, {}), true);
});

test('a label layer whose filter tests the pitch still becomes a rule', () => {
    const label = { id: 'road-label', type: 'symbol', 'source-layer': 'road',
        layout: { 'text-field': '{name}' },
        filter: ['all', ['has', 'name'], ['case', ['<=', ['pitch'], 40], true, false]] };
    const { mss } = convert({ layers: [label] }, TABLE, { variables: false });
    assert.match(mss, /#road.*::road_label \{/);
    assert.match(mss, /text-name:/);
});

test('a format run keeps its text and drops the per-section styling', () => {
    // Standard writes every POI name as ["format", ["coalesce", ...], {}] - CartoCSS styles the
    // whole label at once, so only the text carries over.
    const label = (field) => convert({ layers: [{ id: 'poi', type: 'symbol', 'source-layer': 'poi_label',
        layout: { 'text-field': field } }] }, TABLE, { variables: false }).mss;
    assert.match(label(['format', ['coalesce', ['get', 'name_en'], ['get', 'name']], {}]),
        /text-name: \(\[name_en\]\) \?\? \(\[name\]\);/);
    assert.match(label(['format', ['get', 'a'], {}, ' ', {}, ['get', 'b'], {}]),
        /text-name: concat\(concat\(\[a\], ' '\), \[b\]\);/);
});

test('buildings get a style parameter, so an app can drop the 3D pass', () => {
    // The same three states the hand-written styles under assets/style use: 0 off, 1 flat, 2 with
    // extrusions. `['param::buildings'>0]` gates the footprint and `>1` the extrusion, so a device
    // that cannot afford the 3D pass turns it off without anyone editing the CartoCSS.
    const style = { layers: [
        { id: 'b2d', type: 'fill', 'source-layer': 'building', paint: { 'fill-color': '#eee' } },
        { id: 'b3d', type: 'fill-extrusion', 'source-layer': 'building',
            paint: { 'fill-extrusion-color': '#ddd', 'fill-extrusion-height': 10 } },
    ] };
    const { mss, project } = convert(style, TABLE, { variables: false });
    assert.match(mss, /#building\['param::buildings'>0\]::b2d \{/);
    assert.match(mss, /#building\['param::buildings'>1\]::b3d \{/);
    assert.equal(JSON.parse(project).styleparameters.buildings, 2, 'defaults to what the source drew');
});

test('a style with no buildings declares no such parameter', () => {
    const { project } = convert({ layers: [
        { id: 'w', type: 'fill', 'source-layer': 'water', paint: { 'fill-color': '#a0c8f0' } }] },
    TABLE, { variables: false });
    assert.equal(JSON.parse(project).styleparameters, undefined);
});
