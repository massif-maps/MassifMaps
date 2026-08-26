import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { readsFeature } from '../dist/mapbox2css/split.js';

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

function lineLayer(paint) {
    return { id: 'road', type: 'line', source: 'osm', 'source-layer': 'transportation', paint };
}

function blocksOf(paint) {
    return convert({ layers: [lineLayer(paint)] }, TABLE).mss
        .split('\n').filter((l) => l.startsWith('#transportation'));
}

test('a zoom-driven value is not a feature read', () => {
    assert.ok(!readsFeature(['interpolate', ['linear'], ['zoom'], 6, 1, 16, 12]));
    assert.ok(readsFeature(['match', ['get', 'class'], 'motorway', '#f00', '#fff']));
    assert.ok(readsFeature({ property: 'class', stops: [['motorway', 1]] }));
});

test('a field-driven colour stays one expression - only a sprite NAME has to be split', () => {
    // A property value that reads a field is evaluated per feature
    // (GenericFunctionProperty::getFunction rebuilds from the bound context), so a match becomes a
    // nested ternary and the layer stays ONE attachment. Splitting it was a workaround for
    // `Color parsing failed` that no longer reproduces, and it cost real fidelity: a 28-branch
    // country case blew the variant cap, so every road shield fell back to white.
    const mss = convert({ layers: [lineLayer({
        'line-color': ['match', ['get', 'class'], 'motorway', '#ff8000', ['trunk', 'primary'], '#ffc000', '#ffffff'],
    })] }, TABLE).mss;
    assert.equal(mss.split('\n').filter((l) => l.startsWith('#transportation')).length, 1);
    assert.match(mss, /line-color: \(\(\[class\] = 'motorway'\) \? #ff8000 : /);
    assert.ok(mss.includes('#ffc000') && mss.includes('#ffffff'));
});

test('two field-driven colours no longer multiply into a cartesian product', () => {
    const blocks = blocksOf({
        'line-color': ['match', ['get', 'class'], 'motorway', '#f00', '#fff'],
        'line-opacity': ['case', ['==', ['get', 'brunnel'], 'tunnel'], 0.5, 1],
    });
    assert.equal(blocks.length, 1);
});

test('a sprite name still becomes one attachment per branch, and excludes the earlier ones', () => {
    // An icon-image is not a value the renderer can evaluate - it has to name one file.
    const blocks = convert({ layers: [{
        id: 'p', type: 'symbol', 'source-layer': 'poi',
        layout: { 'text-field': '{name}', 'icon-image': ['case', ['==', ['get', 'a'], 1], 'one', ['==', ['get', 'b'], 2], 'two', 'other'] },
    }] }, TABLE).mss.split('\n').filter((l) => l.startsWith('#poi'));
    assert.equal(blocks.length, 3);
    assert.ok(!blocks[0].includes('!'));
    assert.match(blocks[1], /!\(\[a\] = 1\)/);
    assert.match(blocks[2], /!\(\[a\] = 1\).*!\(\[b\] = 2\)/);
});

test('a colour reading a bare field is kept, because the decoder evaluates it per feature', () => {
    const { mss } = convert({ layers: [lineLayer({ 'line-color': ['get', 'colour'], 'line-width': 2 })] }, TABLE);
    assert.match(mss, /line-color: \[colour\];/);
});

test('a zoom-driven TEXT becomes zoom bands, because a string keyframe reads as a colour', () => {
    // InterpolateExpression treats any string keyframe as a colour, so MapTiler's
    // `step(zoom, [name], 15, concat(...))` had the decoder parsing "Beauregard" as one and losing
    // the whole rule - every lift station and peak label with it.
    const layer = { id: 's', type: 'symbol', 'source-layer': 'poi_station', layout: {
        'text-field': ['step', ['zoom'], ['get', 'name'], 15, ['concat', ['get', 'name'], ' m']] } };
    const out = convert({ layers: [layer] }, TABLE).mss;
    assert.ok(!/text-name: step\(/.test(out), 'a text ramp must not survive as an interpolation');
    assert.match(out, /#poi_station\[zoom >= 0\]\[zoom < 15\]/);
    assert.match(out, /#poi_station\[zoom >= 15\]/);
    assert.match(out, /text-name: \[name\];/);
    assert.match(out, /text-name: concat\(\[name\], ' m'\);/);
});

test('a zoom-driven sprite name becomes one attachment per zoom band', () => {
    // A town's circle: `{stops: [[6, 'circle'], [12, ' ']]}` names one file per band, never a blend.
    const blocks = convert({ layers: [{
        id: 'town', type: 'symbol', 'source-layer': 'town_label',
        layout: { 'text-field': '{name}', 'icon-image': { stops: [[6, 'circle'], [12, ' ']] } },
    }] }, TABLE).mss.split('\n').filter((l) => l.startsWith('#town_label'));
    assert.equal(blocks.length, 2);
    assert.match(blocks[0], /\[zoom >= 6\]\[zoom < 12\]/);
    assert.match(blocks[1], /\[zoom >= 12\]/);
});

test('text-name still reads fields, because the text is evaluated per feature', () => {
    const { mss } = convert({ layers: [{
        id: 'l', type: 'symbol', 'source-layer': 'place', layout: { 'text-field': ['get', 'name'] },
    }] }, TABLE);
    assert.match(mss, /text-name: \[name\];/);
    assert.equal(mss.split('\n').filter((l) => l.startsWith('#place')).length, 1);
});
