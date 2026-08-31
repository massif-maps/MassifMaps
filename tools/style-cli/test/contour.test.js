import assert from 'node:assert/strict';
import { test } from 'node:test';

import { readFileSync } from 'node:fs';

import { convert } from '../dist/mapbox2css/index.js';
import { isContourLayer, rewriteContourFields, rewriteContourFilter } from '../dist/mapbox2css/contour.js';

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

const OPTIONS = { schema: 'div', majorDiv: 100 };

function rewrite(filter) {
    let rewrites = 0;
    const out = rewriteContourFilter(filter, OPTIONS, () => rewrites++);
    return { out, rewrites };
}

test('only contour source-layers are touched', () => {
    assert.ok(isContourLayer({ id: 'a', type: 'line', 'source-layer': 'contour' }));
    assert.ok(isContourLayer({ id: 'a', type: 'line', 'source-layer': 'contours' }));
    assert.ok(!isContourLayer({ id: 'a', type: 'line', 'source-layer': 'transportation' }));
});

test('index contours become a major-div test', () => {
    // MapTiler marks index contours as every 5th or 10th line.
    assert.deepEqual(rewrite(['in', 'nth_line', 5, 10]).out, ['>=', 'div', 100]);
    assert.deepEqual(rewrite(['==', 'nth_line', 10]).out, ['>=', 'div', 100]);
});

test('regular contours become the minor-div test', () => {
    assert.deepEqual(rewrite(['!in', 'nth_line', 5, 10]).out, ['<', 'div', 100]);
});

test('the rewrite reaches inside all/any and leaves siblings alone', () => {
    const { out, rewrites } = rewrite(['all', ['in', 'nth_line', 5, 10], ['!has', 'glacier']]);
    assert.deepEqual(out, ['all', ['>=', 'div', 100], ['!has', 'glacier']]);
    assert.equal(rewrites, 1);
});

test('the expression spelling of the key is rewritten too', () => {
    assert.deepEqual(rewrite(['==', ['get', 'nth_line'], 10]).out, ['>=', 'div', 100]);
});

test('an nth_line value that is not an index line is left alone', () => {
    // Only 5 and 10 mean "index"; anything else has no div equivalent, so it must not be guessed.
    const { out, rewrites } = rewrite(['==', 'nth_line', 2]);
    assert.deepEqual(out, ['==', 'nth_line', 2]);
    assert.equal(rewrites, 0);
});

test('the elevation field is renamed, because the two schemas spell it differently', () => {
    // MapTiler's contour tiles call it `height`; the gdal ladder and ContourTileDataSource call it
    // `ele`. Same quantity, same unit - but a label reading the wrong name draws nothing.
    assert.deepEqual(rewriteContourFields(['>', 'height', 0], OPTIONS), ['>', 'ele', 0]);
    assert.deepEqual(rewriteContourFields(['get', 'height'], OPTIONS), ['get', 'ele']);
    assert.equal(rewriteContourFields('{height} m', OPTIONS), '{ele} m');
    // It reaches inside a layer, so the filter, the label and the paint all agree.
    assert.deepEqual(
        rewriteContourFields({ layout: { 'text-field': ['get', 'height'] } }, OPTIONS),
        { layout: { 'text-field': ['get', 'ele'] } });
});

test('the field rename needs the option too', () => {
    assert.deepEqual(rewriteContourFields(['get', 'height'], { majorDiv: 100 }), ['get', 'height']);
});

test('nothing is rewritten without the option', () => {
    const filter = ['in', 'nth_line', 5, 10];
    assert.deepEqual(rewriteContourFilter(filter, { majorDiv: 100 }, () => {
        throw new Error('should not rewrite');
    }), filter);
});

test('a style drawing from several tilesets says which layers need their own source', () => {
    // A CartoCSS project is ONE datasource. MapTiler topo-v4 keeps its peaks in a separate
    // `landform` tileset, so they silently drew nothing until the report named them.
    const style = {
        sources: {
            planet: { url: 'https://example.invalid/planet.json' },
            landform: { url: 'https://example.invalid/landform.json' },
        },
        layers: [
            { id: 'r', type: 'line', source: 'planet', 'source-layer': 'road', paint: { 'line-color': '#000' } },
            { id: 'p', type: 'symbol', source: 'landform', 'source-layer': 'peak', layout: { 'text-field': '{name}' } },
        ],
    };
    const report = convert(style, TABLE).coverage.report();
    assert.match(report, /2 tilesets/);
    assert.match(report, /"landform" \(peak\)/);
    assert.match(report, /landform.json/);
});
