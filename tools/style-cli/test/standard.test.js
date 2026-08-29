import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

import { convert, sceneLights } from '../dist/mapbox2css/index.js';
import { importOnly, resolveConfig, sceneBrightness } from '../dist/mapbox2css/config.js';
import { foldConfig } from '../dist/mapbox2css/fold.js';
import { groundRadiance } from '../dist/mapbox2css/emissive.js';

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

/**
 * Mapbox Standard's own `lights` block, verbatim from the published style. The four presets differ
 * ONLY here, and every colour in the map is a function of these numbers, so this fixture is what
 * makes the conformance tests below checkable without rendering anything.
 */
const STANDARD_LIGHTS = [
    { id: 'ambient', type: 'ambient', properties: {
        color: ['match', ['config', 'lightPreset'],
            'dawn', 'hsl(28, 98%, 93%)', 'day', 'hsl(0, 0%, 100%)',
            'dusk', 'hsl(228, 27%, 29%)', 'night', 'hsl(217, 100%, 11%)', 'hsl(0, 0%, 100%)'],
        intensity: ['match', ['config', 'lightPreset'],
            'dawn', 0.75, 'day', 0.8, 'dusk', 0.8, 'night', 0.5, 0.8] } },
    { id: 'directional', type: 'directional', properties: {
        direction: ['match', ['config', 'lightPreset'],
            'dawn', ['literal', [120, 50]], 'day', ['literal', [180, 20]],
            'dusk', ['literal', [240, 80]], 'night', ['literal', [270, 20]], ['literal', [180, 20]]],
        color: ['match', ['config', 'lightPreset'],
            'dawn', 'hsl(33, 98%, 77%)', 'day', 'hsl(0, 0%, 100%)',
            'dusk', 'hsl(30, 98%, 76%)', 'night', 'hsl(225, 15%, 29%)', 'hsl(0, 0%, 100%)'],
        intensity: ['interpolate', ['linear'], ['zoom'], 12,
            ['match', ['config', 'lightPreset'], 'dawn', 0.5, 'day', 0.2, 'dusk', 0, 'night', 0, 0.2],
            14, ['match', ['config', 'lightPreset'], 'dawn', 0.5, 'day', 0.2, 'dusk', 0.2, 'night', 0.5, 0.2]] } },
];

const atPreset = (fn) => (preset) => fn(STANDARD_LIGHTS, (node) => fold(node, { lightPreset: preset }));
const round = (value, places) => Number(value.toFixed(places));

/**
 * `Style.calculateLightsBrightness` (src/style/style.ts), which is what `measure-light` reads.
 *
 * These four numbers ARE the ground truth for the whole palette: Standard reads the brightness 113
 * times to switch a colour between its lit and its unlit form, so a proxy that lands even slightly
 * off samples every one of those ramps at the wrong place. An ambient-only proxy read the presets
 * as 0.80 / 0.70 / 0.23 / 0.06 and day and dawn came out alike.
 *
 * Confirmed against gl-js ITSELF, not only against its source: `map.style.getBrightness()` on the
 * real Standard at z16.2 returns 0.477778 / 0.396486 / 0.026970 / 0.013517. The residual is 8-bit
 * colour rounding. `wasm/mbref.html` is set up to repeat that measurement.
 *
 * The value is ZOOM-dependent, because the directional intensity is a zoom ramp: below its first
 * stop gl-js clamps to z12, where dusk and night have no directional light at all (0.020151 at
 * z9.12 against 0.026970 at z16.2). We bake ONE number, the top of the ramp, so a converted style
 * matches from z14 up and drifts below it.
 */
test('the scene brightness is mapbox\'s own, directional light and W3C luminance included', () => {
    const brightness = atPreset(sceneBrightness);
    assert.equal(round(brightness('day'), 3), 0.478);
    assert.equal(round(brightness('dawn'), 3), 0.396);
    assert.equal(round(brightness('dusk'), 3), 0.027);
    assert.equal(round(brightness('night'), 3), 0.014);
});

/**
 * The common ramp - 66 of Standard's 113 reads switch between an unlit and a lit colour over
 * `[0.25, 0.3]`. Which side each preset lands on is the single most visible consequence of the
 * number above, so it is asserted as the BEHAVIOUR rather than left implied by the value.
 */
test('day and dawn take a brightness ramp\'s lit end, dusk and night its unlit one', () => {
    const brightness = atPreset(sceneBrightness);
    const lit = (preset) => brightness(preset) >= 0.3;
    const unlit = (preset) => brightness(preset) <= 0.25;
    assert.ok(lit('day') && lit('dawn'), 'day and dawn are lit');
    assert.ok(unlit('dusk') && unlit('night'), 'dusk and night are not');
});

/**
 * `calculateGroundRadiance` (3d-style/render/lights.ts) with the ground normal - the per-channel
 * factor `apply_lighting_ground` multiplies every unlit 2D colour by, and what the converter folds
 * into the palette in place of a render-time light.
 *
 * Dawn is the one that matters: mapbox draws it at FULL brightness with a fifth of its blue
 * removed, which is what makes it read as dawn rather than as a slightly dimmer day.
 */
test('the ground radiance is mapbox\'s, per channel, so dawn is warm and not merely darker', () => {
    const radiance = atPreset((lights, f) => groundRadiance(sceneLights(lights, f)));
    const at = (preset) => radiance(preset).map((c) => round(c, 3));
    assert.deepEqual(at('day'), [0.994, 0.994, 0.994]);
    assert.deepEqual(at('dawn'), [1, 0.916, 0.807]);
    assert.deepEqual(at('dusk'), [0.28, 0.267, 0.347]);
    // Corroborated against a MEASURED gl-js night render (see emissive.ts): the land there needed
    // a light of about (0.18, 0.175, 0.27), which the ported formula reproduces without fitting.
    assert.deepEqual(at('night'), [0.175, 0.197, 0.278]);
    const [dawnR, , dawnB] = radiance('dawn');
    assert.ok(dawnR - dawnB > 0.15, `dawn is warm, got r-b ${dawnR - dawnB}`);
});

/** A configurable style, in the shape Standard uses: one colour per preset, one shared by both. */
const CONFIGURABLE = {
    name: 'Cfg',
    schema: { lightPreset: { default: 'day', values: ['day', 'night'] } },
    lights: [{ id: 'ambient', type: 'ambient', properties: {
        color: ['match', ['config', 'lightPreset'], 'night', 'hsl(0, 0%, 10%)', 'hsl(0, 0%, 100%)'],
        intensity: 1 } }],
    layers: [
        // Self-lit, so the light leaves both colours alone and these stay tests of the PALETTE.
        { id: 'water', type: 'fill', 'source-layer': 'water',
            paint: { 'fill-emissive-strength': 1,
                'fill-color': ['match', ['config', 'lightPreset'], 'night', '#001133', '#a0c8f0'] } },
        // Shares the water's colour by day and not by night - the case that used to make the two
        // palettes name their variables differently.
        { id: 'pond', type: 'fill', 'source-layer': 'pond',
            paint: { 'fill-emissive-strength': 1,
                'fill-color': ['match', ['config', 'lightPreset'], 'night', '#002244', '#a0c8f0'] } },
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
    assert.match(sized(['step', ['zoom'], 18, 5, 12]), /text-size: step\(\(\[view::zoom\] - 1\), \(0, 18\), \(5, 12\)\);/);
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

test('line-gap-width is carried, not split into the two strips it stands for', () => {
    // The gap is NOT DRAWN: `line-gap-width` is the road the casing runs along and `line-width` is
    // the strip on ONE side. The SDK draws that itself now (vt, GAPWIDTH), so one MapBox layer is
    // one rule. It used to become one rule PER SIDE, each offset half a gap plus half its width -
    // correct, but 142 rules on Standard against 114, and twice the line geometry.
    const cased = { id: 'road-case', type: 'line', 'source-layer': 'road',
        paint: { 'line-color': '#888', 'line-gap-width': 6, 'line-width': 1.5 } };
    const { mss } = convert({ layers: [cased] }, TABLE, { variables: false });
    assert.match(mss, /line-gap-width: 6;/);
    assert.match(mss, /line-width: 1\.5;/, 'the width is the strip, not the whole span');
    assert.ok(!/::road_case_left/.test(mss), 'no longer one rule per side');
    assert.ok(!/::road_case_right/.test(mss));
    assert.ok(!/line-offset:/.test(mss), 'and no offset: the shader cuts the middle out');
});

test('line-blur is carried too, so a soft shadow stays soft', () => {
    // Standard's bridge shadows are width 10 / blur 10. Dropped, they draw as hard dark bars with
    // a visible butt cap at each end.
    const shadow = { id: 'bridge-shadow', type: 'line', 'source-layer': 'road',
        paint: { 'line-color': '#888', 'line-width': 10, 'line-blur': 10 } };
    const { mss } = convert({ layers: [shadow] }, TABLE, { variables: false });
    assert.match(mss, /line-blur: 10;/);
});

test('the negated offset is 0 - x, which is what the grammar takes', () => {
    // MapBox offsets a line to the RIGHT of its direction of travel and mapnik to the LEFT, so
    // every offset is negated - and `-(linear(...))` is a syntax error, there being no unary minus
    // before a parenthesised value.
    const offset = { id: 'c', type: 'line', 'source-layer': 'road', paint: { 'line-color': '#888',
        'line-offset': ['interpolate', ['linear'], ['zoom'], 12, 3, 18, 12], 'line-width': 1 } };
    const { mss } = convert({ layers: [offset] }, TABLE, { variables: false });
    assert.ok(!/line-offset: -\(/.test(mss));
    assert.match(mss, /line-offset: \(0 - \(/);
});

test('a gap and a width that ramp over zoom stay per-frame functions', () => {
    const cased = { id: 'road-case', type: 'line', 'source-layer': 'road', paint: {
        'line-color': '#888',
        'line-gap-width': ['interpolate', ['linear'], ['zoom'], 12, 3, 18, 12],
        'line-width': ['interpolate', ['linear'], ['zoom'], 14, 0.5, 18, 1] } };
    const { mss } = convert({ layers: [cased] }, TABLE, { variables: false });
    assert.match(mss, /line-gap-width: linear\(\(\[view::zoom\] - 1\), \(12, 3\), \(18, 12\)\);/);
    assert.match(mss, /line-width: linear\(\(\[view::zoom\] - 1\), \(14, 0\.5\), \(18, 1\)\);/);
});

/** A style whose lights differ per preset, which is how emissive-strength becomes visible. */
const LIT = {
    schema: { lightPreset: { default: 'day', values: ['day', 'night'] } },
    lights: [{ id: 'ambient', type: 'ambient', properties: {
        color: ['match', ['config', 'lightPreset'], 'night', 'hsl(0, 0%, 10%)', 'hsl(0, 0%, 100%)'],
        intensity: 1 } }],
    layers: [
        { id: 'ground', type: 'fill', 'source-layer': 'land',
            paint: { 'fill-color': 'hsl(20, 20%, 90%)', 'fill-emissive-strength': 0 } },
        { id: 'sign', type: 'fill', 'source-layer': 'sign',
            paint: { 'fill-color': 'hsl(20, 20%, 90%)', 'fill-emissive-strength': 1 } },
    ],
};

test('the default preset is drawn unlit, so it matches the source style', () => {
    // sceneBrightness is an ambient-only proxy; only the RATIO between two presets of one style
    // means anything, so the default preset has to come out at factor 1 or every converted day
    // render would be darker than the style it came from.
    // Both layers share the colour by day, so it hoists to ONE variable - the value is the point.
    const { variables } = convert(LIT, TABLE, { presets: [] });
    assert.match(variables, /^@\w+: hsl\(20, 20%, 90%\);$/m);
    assert.ok(!/hsl\(20, 20%, (?!90%)/.test(variables), 'nothing else was darkened either');
});

test('a non-emissive layer follows the light down; a fully emissive one does not', () => {
    const { presets } = convert(LIT, TABLE);
    const night = presets.get('night');
    // MapBox's own ground radiance: an ambient at 10% is pow(0.1, 2.2) of light, and returning
    // that to sRGB is 0.1 - so the ground is drawn at a tenth. The light multiplies the CHANNELS,
    // and scaling all three by the same k does not keep the HSL saturation NUMBER - 20% -> 2.22%
    // here - while the channel ratios, which are what the eye reads, are untouched.
    assert.match(night, /@ground_fill: hsl\(20, 2.22%, 9%\);/);
    assert.match(night, /@sign_fill: hsl\(20, 20%, 90%\);/, 'self-lit, so night does not touch it');
});

test('a coloured light casts its own hue, and only over the lit part', () => {
    // Standard's night ambient is hsl(217, 100%, 11%) with the directional light at intensity 0,
    // so the only light in the scene is blue - and a land drawn with the lightness alone came out
    // brown. The cast is measured against the DEFAULT preset's light, so day is untouched.
    const blue = { ...LIT, lights: [{ id: 'ambient', type: 'ambient', properties: {
        color: ['match', ['config', 'lightPreset'], 'night', 'hsl(217, 100%, 10%)', 'hsl(0, 0%, 100%)'],
        intensity: 1 } }] };
    const { presets, variables } = convert(blue, TABLE);
    const hue = /@ground_fill: hsl\(([\d.]+),/.exec(presets.get('night'));
    assert.ok(hue, 'the ground is still written');
    assert.ok(Number(hue[1]) > 180 && Number(hue[1]) < 260, `blue-cast, got ${hue[1]}`);
    // The self-lit layer takes no cast at all, whatever the light is.
    assert.match(presets.get('night'), /@sign_fill: hsl\(20, 20%, 90%\);/);
    assert.match(variables, /hsl\(20, 20%, 90%\)/, 'and the default preset is as authored');
});

test('unstated emissive takes MapBox\'s default: a label is lit, geometry is not', () => {
    // Standard's `roads-case` states none and gl-js still draws it dark at night; read as fully
    // emissive it painted a white casing over the whole night map.
    const plain = { ...LIT, layers: [
        { id: 'ground', type: 'fill', 'source-layer': 'land', paint: { 'fill-color': 'hsl(20, 20%, 90%)' } },
        { id: 'name', type: 'symbol', 'source-layer': 'place',
            layout: { 'text-field': '{name}' }, paint: { 'text-color': 'hsl(20, 20%, 90%)' } },
    ] };
    const { presets } = convert(plain, TABLE);
    assert.match(presets.get('night'), /@ground_fill: hsl\(20, 2.22%, 9%\);/, 'geometry follows the light');
    assert.match(presets.get('night'), /hsl\(20, 20%, 90%\)/, 'and a label keeps its own colour');
});

test('runtime interaction state folds to unset, so the ordinary branch survives', () => {
    // Nothing is selected or highlighted in the SDK and nothing can become so. Standard guards
    // 3d-building's colour with `["to-boolean", ["feature-state", "select"]]`; left alone the
    // whole property was refused, and BuildingSymbolizer's default fill is BLACK - which is
    // exactly how every building came out.
    const style = { layers: [{ id: '3d-building', type: 'fill-extrusion', 'source-layer': 'building',
        paint: {
            'fill-extrusion-color': ['case',
                ['to-boolean', ['feature-state', 'select']], '#ff0000',
                '#cccccc'],
            'fill-extrusion-height': ['number', ['get', 'height']],
        } }] };
    const { mss } = convert(style, TABLE, { variables: false });
    assert.match(mss, /building-fill: #cccccc;/);
    assert.ok(!/#ff0000/.test(mss), 'the selected-feature branch is unreachable and goes');
    assert.match(mss, /building-height: \[height\];/);
});

test('a recolourable sprite keeps its NAME, since the SDK cannot tint one per feature', () => {
    // Standard writes `["image", name, { params: … }]`. The params are a runtime tint; the name is
    // what the SDK can act on. Unwrapped, a zoom-driven oneway arrow becomes a real sprite again.
    const style = { layers: [{ id: 'road-oneway-arrow', type: 'symbol', 'source-layer': 'road',
        layout: { 'icon-image': ['image', 'oneway-small', { params: { color: '#fff' } }] } }] };
    const { coverage } = convert(style, TABLE, { variables: false });
    assert.ok(!coverage.dropped.has('icon-image') || !/data-driven/.test(
        coverage.dropped.get('icon-image')?.reason ?? ''), 'the name is no longer data-driven');
});

test('a tree becomes a canopy dot, and the random scatter its middle', () => {
    // Standard draws the `tree` source-layer as 3D models from z15. Dropped as "unsupported layer
    // type", every park came out a bare green slab. There is no model to port, so the canopy is
    // what is left.
    const trees = {
        id: 'trees', type: 'model', source: 'composite', 'source-layer': 'tree', minzoom: 15,
        layout: { 'model-id': 'oak1-lod4' },
        paint: {
            'model-color': 'hsl(120, 50%, 70%)',
            'model-opacity': ['interpolate', ['linear'], ['zoom'], 15, 0, 16, 1],
            'model-rotation': [0, 0, 90],
        },
    };
    const { mss: out, coverage } = convert({ layers: [trees] }, TABLE, { variables: false });

    assert.match(out, /#tree\[zoom >= 16\]/, 'the layer is kept, one level up for the 256 px tiles');
    assert.match(out, /marker-fill: hsl\(120, 50%, 70%\);/, 'the model colour is the canopy');
    assert.match(out, /marker-line-color: darken\(hsl\(120, 50%, 70%\), 0.18\);/,
        'and a darker ring separates two');
    assert.match(out, /marker-allow-overlap: true;/);
    // NOT emitted with allow-overlap here, unlike a symbol layer: hundreds of trees per tile must
    // not go through the label culler.
    assert.match(out, /marker-clip: true;/);
    assert.ok(coverage.report().includes('a canopy dot has no orientation'));

    // Every other model layer has no 2D reduction and is still dropped.
    const turbine = { ...trees, id: 'wind', 'source-layer': 'wind_turbine' };
    const dropped = convert({ layers: [turbine] }, TABLE, { variables: false });
    assert.equal(dropped.mss.includes('#wind_turbine'), false);
    assert.ok(dropped.coverage.report().includes('no 2D stand-in for this one'));
});

test('a per-feature random folds to the middle of its range', () => {
    // Standard tints each tree with `hsl(random(...), 50, random(...))` around the greenspace
    // colour. CartoCSS has no seed to scatter with, so the bounds are what carries the intent.
    assert.equal(fold(['random', ['config', 'lo'], 140, ['id']], { lo: 100 }, {}), 120);
    // ...so the colour around it folds to one the style can state.
    assert.equal(fold(['hsl', ['random', ['config', 'lo'], 140, ['id']], 50, 70], { lo: 100 }, {}),
        'hsl(120, 50%, 70%)');
    // Only where both bounds are constant; anything else is left for the caller to report.
    assert.deepEqual(fold(['random', ['config', 'lo'], ['get', 'hi'], ['id']], { lo: 100 }, {}),
        ['random', 100, ['get', 'hi'], ['id']]);
});

test('the flat and the 3D building layers are two states of one parameter', () => {
    // Standard draws footprints and extrusions as two layers over the same source, each gated on
    // `show3dBuildings`/`show3dObjects`. Folded with the default config - 3D on - the flat pair
    // resolved to `visibility: none` and was dropped, so `buildings: 1` left no buildings at all.
    const gate = (on) => ['case', ['all', ['config', 'show3dBuildings'], ['config', 'show3dObjects']],
        on ? 'visible' : 'none', on ? 'none' : 'visible'];
    const flat = { id: '2d-building', type: 'fill-extrusion', 'source-layer': 'building', minzoom: 15,
        layout: { visibility: gate(false) }, paint: { 'fill-extrusion-height': 0.05, 'fill-extrusion-color': '#ddd' } };
    const solid = { id: '3d-building', type: 'fill-extrusion', 'source-layer': 'building', minzoom: 15,
        layout: { visibility: gate(true) }, paint: { 'fill-extrusion-height': ['get', 'height'], 'fill-extrusion-color': '#ccc' } };
    const out = convert({ layers: [flat, solid] }, TABLE, { variables: false }).mss;

    assert.match(out, /#building\[zoom >= 16\]\['param::buildings'>0\]\['param::buildings'<2\][^\n]*::_2d_building \{/);
    assert.match(out, /#building\[zoom >= 16\]\['param::buildings'>1\][^\n]*::_3d_building \{/);
    // An upper bound on the flat one, or both drew at `buildings: 2`.
    assert.ok(!/::_3d_building[^\n]*'param::buildings'<2/.test(out));
});

test('the directional light says WHERE the sun is, not only how strong', () => {
    // Standard states it per preset - day is [180, 20] - and without it the app's own sun lit the
    // buildings: at the bench's default the roofs came out at 86% of their colour where gl-js
    // draws them at ~100%, which reads as "the building colour is wrong" and is not.
    const style = {
        lights: [
            { id: 'ambient', type: 'ambient', properties: { intensity: 0.8 } },
            { id: 'directional', type: 'directional', properties: { direction: ['literal', [180, 20]], intensity: 0.2 } },
        ],
        layers: [{ id: '3d-building', type: 'fill-extrusion', 'source-layer': 'building', minzoom: 15,
            paint: { 'fill-extrusion-height': ['get', 'height'], 'fill-extrusion-color': '#eee' } }],
    };
    const out = convert(style, TABLE, { variables: false }).mss;
    assert.match(out, /sun-azimuth: 180;/, 'the azimuth runs clockwise from north in both');
    // MapBox's polar angle is measured from straight UP, so the altitude is its complement.
    assert.match(out, /sun-altitude: 70;/);
    assert.match(out, /building-ambient: 0.8;/);
});
