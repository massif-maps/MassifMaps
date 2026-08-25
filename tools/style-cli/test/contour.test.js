import assert from 'node:assert/strict';
import { test } from 'node:test';

import { isContourLayer, rewriteContourFilter } from '../dist/mapbox2css/contour.js';

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

test('nothing is rewritten without the option', () => {
    const filter = ['in', 'nth_line', 5, 10];
    assert.deepEqual(rewriteContourFilter(filter, { majorDiv: 100 }, () => {
        throw new Error('should not rewrite');
    }), filter);
});
