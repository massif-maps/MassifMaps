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
    const blocks = blocksOf({
        'line-color': ['match', ['get', 'class'], 'motorway', '#f00', '#fff'],
        'line-opacity': ['case', ['==', ['get', 'brunnel'], 'tunnel'], 0.5, 1],
    });
    assert.equal(blocks.length, 4);
});

test('past the variant cap the layer stays whole and keeps its fallback', () => {
    // 4 x 4 = 16 attachments for one layer is not worth the compile cost.
    const many = (field) => ['match', ['get', field], 'a', 1, 'b', 2, 'c', 3, 4];
    const { mss, coverage } = convert({ layers: [lineLayer({
        'line-width': many('x'), 'line-opacity': many('y'),
    })] }, TABLE);
    assert.equal(mss.split('\n').filter((l) => l.startsWith('#transportation')).length, 4);
    assert.ok(coverage.report().includes('kept only its fallback'));
});

test('a value with no fallback to fall back to is dropped, not emitted', () => {
    // `["get", "width"]` would evaluate to null and take the whole rule down with it.
    const { mss, coverage } = convert({ layers: [lineLayer({ 'line-width': ['get', 'width'], 'line-color': '#fff' })] }, TABLE);
    assert.ok(!mss.includes('line-width'));
    assert.ok(coverage.report().includes('reads a feature field'));
});

test('text-name still reads fields, because the text is evaluated per feature', () => {
    const { mss } = convert({ layers: [{
        id: 'l', type: 'symbol', 'source-layer': 'place', layout: { 'text-field': ['get', 'name'] },
    }] }, TABLE);
    assert.match(mss, /text-name: \[name\];/);
    assert.equal(mss.split('\n').filter((l) => l.startsWith('#place')).length, 1);
});
