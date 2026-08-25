import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { followsLine, resolvePlacement } from '../dist/mapbox2css/placement.js';

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

function symbolLayer(layout) {
    return { id: 'label', type: 'symbol', 'source-layer': 'place', layout: { 'text-field': '{name}', ...layout } };
}

function mss(layout) {
    return convert({ layers: [symbolLayer(layout)] }, TABLE).mss;
}

test('a point label is a billboard, which is what MapBox auto-alignment resolves to', () => {
    assert.equal(resolvePlacement(symbolLayer({}), 'text'), 'billboard');
    assert.equal(resolvePlacement(symbolLayer({ 'symbol-placement': 'point' }), 'text'), 'billboard');
    assert.match(mss({}), /text-placement: 'billboard';/);
});

test('an explicit map pitch-alignment is the flat CartoCSS point placement', () => {
    assert.equal(resolvePlacement(symbolLayer({ 'text-pitch-alignment': 'map' }), 'text'), 'point');
});

test('a line placement follows the line, flat, unless the pitch is viewport-aligned', () => {
    assert.equal(resolvePlacement(symbolLayer({ 'symbol-placement': 'line' }), 'text'), 'line');
    assert.equal(resolvePlacement(symbolLayer({ 'symbol-placement': 'line-center' }), 'text'), 'line');
    assert.equal(
        resolvePlacement(symbolLayer({ 'symbol-placement': 'line', 'text-pitch-alignment': 'viewport' }), 'text'),
        'billboard-line');
});

test('a viewport rotation on a line is the repeat placement - upright, not turning', () => {
    assert.equal(
        resolvePlacement(symbolLayer({ 'symbol-placement': 'line', 'text-rotation-alignment': 'viewport' }), 'text'),
        'billboard-line-repeat');
});

test('the icon resolves from its own alignments', () => {
    const layer = symbolLayer({ 'symbol-placement': 'line', 'icon-rotation-alignment': 'viewport' });
    assert.equal(resolvePlacement(layer, 'icon'), 'billboard-line-repeat');
    assert.equal(resolvePlacement(layer, 'text'), 'line');
});

test('text-max-width is ems of the text size, not pixels', () => {
    // Taken as pixels, the 10-em default wrapped every name onto one word per line.
    assert.match(mss({ 'text-max-width': 10, 'text-size': 12 }), /text-wrap-width: 120;/);
    assert.match(mss({ 'text-max-width': 8 }), /text-wrap-width: 128;/); // MapBox default size 16
});

test('a zoom-driven text-size scales the ems per frame instead of pinning one zoom', () => {
    const size = ['interpolate', ['linear'], ['zoom'], 10, 8, 16, 14];
    assert.match(mss({ 'text-max-width': 10, 'text-size': size }), /text-wrap-width: \(10 \* linear\(.*\)\);/);
    // Data-driven on BOTH sides is still one expression, not a drop.
    assert.match(mss({ 'text-max-width': ['step', ['zoom'], 8, 12, 10], 'text-size': size }),
        /text-wrap-width: \(step\(.*\) \* linear\(.*\)\);/);
});

test('an unstated max-width still wraps, because MapBox wraps at 10 ems by default', () => {
    // CartoCSS wrap-width defaults to 0 = never wrap, so silence here is not the same default.
    assert.match(mss({ 'text-size': 12 }), /text-wrap-width: 120;/);
    assert.match(mss({}), /text-wrap-width: 160;/);
});

test('a line-placed label is never wrapped', () => {
    assert.ok(followsLine(symbolLayer({ 'symbol-placement': 'line' })));
    assert.match(mss({ 'symbol-placement': 'line', 'text-max-width': 10, 'text-size': 12 }), /text-wrap-width: 0;/);
});

test('letter spacing and line height are ems too', () => {
    assert.match(mss({ 'text-letter-spacing': 0.1, 'text-size': 12 }), /text-character-spacing: 1.2;/);
    // text-line-height is a TOTAL height and 1.2 is MapBox's default, which the font already gives.
    assert.match(mss({ 'text-line-height': 1.2, 'text-size': 12 }), /text-line-spacing: 0;/);
    assert.match(mss({ 'text-line-height': 2, 'text-size': 10 }), /text-line-spacing: 8;/);
});

test('a text-offset no longer needs a constant text-size', () => {
    assert.match(mss({ 'text-offset': [0, 1.5], 'text-size': 12 }), /text-dy: 18;/);
    const size = ['interpolate', ['linear'], ['zoom'], 10, 8, 16, 14];
    assert.match(mss({ 'text-offset': [0, 1.5], 'text-size': size }), /text-dy: \(1.5 \* linear\(.*\)\);/);
});

test('text-opacity fades the halo too, or a hidden label leaves a white ghost', () => {
    // MapTiler hides a label with step(zoom, 0, ..., 13, 1); CartoCSS's text-opacity is the FILL
    // only, so the halo stayed at 1 and drew the name in white at every zoom it should not be at.
    const out = mss({ 'text-opacity': ['step', ['zoom'], 0, 13, 1] });
    assert.match(out, /text-opacity: step\(\[view::zoom\], \(0, 0\), \(13, 1\)\);/);
    assert.match(out, /text-halo-opacity: step\(\[view::zoom\], \(0, 0\), \(13, 1\)\);/);
});

test('the modern overlap spelling and the sort key reach the culler', () => {
    assert.match(mss({ 'text-overlap': 'always' }), /text-allow-overlap: true;/);
    assert.match(mss({ 'text-overlap': 'cooperative' }), /text-allow-overlap: false;/);
    // MapBox places the LOWEST sort key first; the culler takes the highest priority.
    assert.match(mss({ 'symbol-sort-key': 3 }), /text-placement-priority: \(0 - 3\);/);
});
