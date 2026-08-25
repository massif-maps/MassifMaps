import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { isShieldLayer } from '../dist/mapbox2css/shield.js';

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

/** The MapTiler shape: the sprite name is built from the ref's length, so there is no one image. */
const SHIELD_IMAGE = ['concat', 'road_', ['to-string', ['get', 'ref_length']]];

function symbol(layout, paint) {
    return { id: 'shield', type: 'symbol', 'source-layer': 'road_label', layout, paint };
}

function mss(layer) {
    return convert({ layers: [layer] }, TABLE).mss;
}

test('a per-feature sprite behind centred text is a shield', () => {
    assert.ok(isShieldLayer(symbol(
        { 'text-field': '{ref}', 'icon-image': SHIELD_IMAGE }, { 'icon-color': '#fff' })));
    // A small offset still sits on the icon - MapTiler's bicolor shields use [0, 0.1].
    assert.ok(isShieldLayer(symbol(
        { 'text-field': '{ref}', 'icon-image': SHIELD_IMAGE, 'text-offset': [0, 0.1] }, { 'icon-color': '#fff' })));
});

test('a POI icon is not a shield: its text sits below the icon, not on it', () => {
    assert.ok(!isShieldLayer(symbol(
        { 'text-field': '{name}', 'icon-image': ['get', 'class'], 'text-anchor': 'top', 'text-offset': [0, 0.8] },
        { 'icon-color': '#fff' })));
});

test('a town circle is not a shield: the sprite is picked by ZOOM, not by the feature', () => {
    assert.ok(!isShieldLayer(symbol(
        { 'text-field': '{name}', 'icon-image': ['step', ['zoom'], 'circle', 13, ''] }, { 'icon-color': '#fff' })));
});

test('without an icon-color there is no plate to draw', () => {
    assert.ok(!isShieldLayer(symbol({ 'text-field': '{ref}', 'icon-image': SHIELD_IMAGE }, {})));
});

test('the sprite tint becomes the plate and its halo the border', () => {
    const out = mss(symbol(
        { 'text-field': '{ref}', 'icon-image': SHIELD_IMAGE, 'text-size': 10 },
        { 'icon-color': '#1a73e8', 'icon-halo-color': '#ffffff', 'icon-halo-width': 1, 'icon-opacity': 0.9 }));
    assert.match(out, /text-background-fill: #1a73e8;/);
    assert.match(out, /text-background-border-fill: #ffffff;/);
    assert.match(out, /text-background-border-width: 1;/);
    assert.match(out, /text-background-opacity: 0.9;/);
    assert.match(out, /text-background-radius: 2;/);
    assert.ok(!out.includes('marker-file'));
});

test('a text-field branching per country keeps its fallback rather than losing the label', () => {
    // MapBox's own shields slice the ref for BR; CartoCSS has no slice, and the fallback is [ref].
    const brazil = ['case', ['==', ['get', 'iso_a2'], 'BR'], ['slice', ['get', 'ref'], 3], ['get', 'ref']];
    const { mss: out, coverage } = convert({ layers: [symbol(
        { 'text-field': brazil, 'icon-image': SHIELD_IMAGE }, { 'icon-color': '#1a73e8' })] }, TABLE);
    assert.match(out, /text-name: \[ref\];/);
    assert.match(out, /text-background-fill: #1a73e8;/);
    assert.ok(coverage.report().includes('kept only its fallback branch'));
});

test('an icon beside text becomes ONE shield, not a marker that culls the label', () => {
    const sprites = new Map([['default', {
        index: { circle: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    const out = convert({ layers: [symbol(
        { 'text-field': '{name}', 'icon-image': 'circle', 'text-anchor': 'bottom', 'text-size': 12 },
        { 'icon-color': '#000000', 'text-color': '#333333' })] },
    TABLE, { sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;

    assert.ok(!out.includes('marker-'), 'a marker beside the text would be a second, colliding label');
    assert.match(out, /shield-file: url\('icons\/circle-000000.png'\);/);
    assert.match(out, /shield-name: \[name\];/);
    assert.match(out, /shield-fill: #333333;/);
    assert.match(out, /shield-unlock-image: true;/);
    // 'bottom' anchors the text's bottom edge, so the text is above and the icon drops below it.
    assert.match(out, /shield-dy: 4;/);
});

test('an icon-overlap alone never builds a fileless marker', () => {
    // marker-allow-overlap on its own makes a MarkersSymbolizer with no file, whose default fill is
    // #0000ff - a blue ellipse over every airport whose sprite could not be resolved.
    const { mss: out } = convert({ layers: [symbol(
        { 'text-field': '{name}', 'icon-image': ['get', 'subclass'], 'icon-overlap': 'never',
            'text-anchor': 'top', 'text-offset': [0, 1] }, { 'icon-color': '#333' })] }, TABLE);
    assert.ok(!out.includes('marker-'));
    assert.match(out, /text-name: \[name\];/);
});

test('a shield whose text cannot be translated draws nothing, not an empty box', () => {
    const { mss: out, coverage } = convert({ layers: [symbol(
        { 'text-field': ['slice', ['get', 'ref'], 3], 'icon-image': SHIELD_IMAGE }, { 'icon-color': '#1a73e8' })] }, TABLE);
    assert.ok(!out.includes('text-background-fill'));
    assert.ok(coverage.report().includes('no usable text-field'));
});
