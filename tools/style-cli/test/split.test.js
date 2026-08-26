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

test('a match over a field becomes one attachment per branch, with constant colours', () => {
    const mss = convert({ layers: [lineLayer({
        'line-color': ['match', ['get', 'class'], 'motorway', '#ff8000', ['trunk', 'primary'], '#ffc000', '#ffffff'],
    })] }, TABLE).mss;
    // A field in a property VALUE kills the rule in the decoder; a predicate is the way to say it.
    assert.ok(!/line-color:.*\[class\]/.test(mss));
    assert.match(mss, /#transportation\[class = 'motorway'\]::road_b1 \{\n  line-color: #ff8000;/);
    assert.match(mss, /line-color: #ffc000;/);
    assert.match(mss, /line-color: #ffffff;/);
});

test('later branches exclude the earlier ones, because MapBox takes the first match', () => {
    const blocks = blocksOf({ 'line-color': ['case', ['==', ['get', 'a'], 1], '#111', ['==', ['get', 'b'], 2], '#222', '#333'] });
    assert.equal(blocks.length, 3);
    assert.ok(!blocks[0].includes('!'));
    assert.match(blocks[1], /!\(\[a\] = 1\)/);
    assert.match(blocks[2], /!\(\[a\] = 1\).*!\(\[b\] = 2\)/);
});

test('two field-driven properties give the product of their branches', () => {
    // Not only the colour: narrowing this to colours alone put the decoder errors back, so
    // anything that reads a field is split.
    const blocks = blocksOf({
        'line-color': ['match', ['get', 'class'], 'motorway', '#f00', '#fff'],
        'line-opacity': ['case', ['==', ['get', 'brunnel'], 'tunnel'], 0.5, 1],
    });
    assert.equal(blocks.length, 4);
    assert.ok(!blocks.join('').includes('[brunnel]') || blocks.every((b) => !/line-opacity:.*\[/.test(b)));
});

test('past the variant cap the layer stays whole and keeps its fallback', () => {
    // 4 x 4 = 16 attachments for one layer is not worth the compile cost.
    const many = (field) => ['match', ['get', field], 'a', '#111', 'b', '#222', 'c', '#333', '#444'];
    const { mss, coverage } = convert({ layers: [{
        id: 'road', type: 'symbol', 'source-layer': 'transportation',
        layout: { 'text-field': '{name}' },
        paint: { 'text-color': many('x'), 'text-halo-color': many('y') },
    }] }, TABLE);
    assert.equal(mss.split('\n').filter((l) => l.startsWith('#transportation')).length, 4);
    assert.ok(coverage.report().includes('kept only its fallback'));
});

test('a colour with no fallback to fall back to is dropped, not emitted', () => {
    // `["get", "colour"]` would take the whole rule down with it.
    const { mss, coverage } = convert({ layers: [lineLayer({ 'line-color': ['get', 'colour'], 'line-width': 2 })] }, TABLE);
    assert.ok(!mss.includes('line-color'));
    assert.ok(coverage.report().includes('reads a feature field'));
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
