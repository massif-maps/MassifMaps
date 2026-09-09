import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { useMemorySpriteHost } from './sprite-host.js';

useMemorySpriteHost();

const NO_PALETTE = { variables: false };
const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

/**
 * A `when(...)` is a WhenPredicate, which PredicateContainsChecker cannot reason about, so it
 * prunes nothing and the decoder evaluates the whole expression per feature. Every one of these
 * asserts that a test which HAS a bracketed form got it.
 */
function mssOf(layers, options = {}) {
    return convert({ layers }, TABLE, { ...NO_PALETTE, ...options }).mss;
}

function selectors(mss) {
    return mss.split('\n').filter((line) => line.startsWith('#'));
}

function roadLayer(extra) {
    return { id: 'road', type: 'line', source: 'osm', 'source-layer': 'transportation', ...extra };
}

const CLASSES = ['motorway', 'trunk', 'primary'];

test('a branch that pins a field drops the set test and the negations it was split with', () => {
    const mss = mssOf([roadLayer({
        filter: ['in', ['get', 'class'], ['literal', CLASSES]],
        layout: {
            'line-sort-key': ['match', ['get', 'class'], 'motorway', 3, 'trunk', 2, 1],
        },
        paint: {
            'line-color': ['match', ['get', 'class'], 'motorway', '#ff0000', 'trunk', '#00ff00', '#0000ff'],
        },
    })]);

    assert.ok(!mss.includes('when('), mss);
    // Lowest sort key first, and each branch keeps exactly the one test that identifies it.
    assert.deepEqual(selectors(mss), [
        "#transportation[class = 'primary']::road_b1 {",
        "#transportation[class = 'trunk']::road_b2 {",
        "#transportation[class = 'motorway']::road_b3 {",
    ]);
    // The value folds to the branch: the rule already knows the class it was split on.
    assert.ok(mss.includes('line-color: #0000ff;'), mss);
    assert.ok(mss.includes('line-color: #00ff00;'), mss);
    assert.ok(mss.includes('line-color: #ff0000;'), mss);
});

test('a set filter becomes one attachment per value when the paint branches on it', () => {
    const mss = mssOf([roadLayer({
        filter: ['in', ['get', 'class'], ['literal', ['wood', 'grass']]],
        paint: { 'line-color': ['match', ['get', 'class'], 'wood', '#0f0', '#ff0'] },
    })]);

    assert.ok(!mss.includes('when('), mss);
    assert.deepEqual(selectors(mss), [
        "#transportation[class = 'wood']::road_b1 {",
        "#transportation[class = 'grass']::road_b2 {",
    ]);
});

test('a set filter with nothing to fold stays one rule, because N rules would cost more', () => {
    const mss = mssOf([roadLayer({
        filter: ['in', ['get', 'class'], ['literal', CLASSES]],
        paint: { 'line-color': '#ff0000' },
    })]);

    assert.equal(selectors(mss).length, 1);
    assert.ok(mss.includes('when('), mss);
});

test('a set filter is left whole when the rest of it would not bracket', () => {
    // Splitting copies the rest of the filter into every attachment, so the one when() it removes
    // would come back once per value.
    const mss = mssOf([roadLayer({
        filter: ['all',
            ['in', ['get', 'class'], ['literal', ['wood', 'grass']]],
            ['in', ['get', 'brunnel'], ['literal', ['bridge', 'tunnel']]]],
        paint: { 'line-color': ['match', ['get', 'class'], 'wood', '#0f0', '#ff0'] },
    })]);

    assert.equal(selectors(mss).length, 1);
    assert.equal(mss.match(/when\(/g).length, 2);
});

test('a negated set test brackets: "none of these" is a conjunction', () => {
    const mss = mssOf([roadLayer({
        filter: ['!', ['in', ['get', 'network'], ['literal', ['us-interstate', 'us-highway']]]],
        paint: { 'line-color': '#ff0000' },
    })]);

    assert.ok(!mss.includes('when('), mss);
    assert.deepEqual(selectors(mss), [
        "#transportation[network != 'us-interstate'][network != 'us-highway']::road {",
    ]);
});

test('a null guard around a field is dropped, since a missing field compares unequal anyway', () => {
    const mss = mssOf([roadLayer({
        filter: ['!=', ['coalesce', ['get', 'subclass'], ''], 'junction'],
        paint: { 'line-color': '#ff0000' },
    })]);

    assert.deepEqual(selectors(mss), ["#transportation[subclass != 'junction']::road {"]);
});

test('a null guard compared against its OWN default is not dropped - the two differ there', () => {
    // `coalesce(subclass, '') = ''` is TRUE for a feature with no subclass, where `subclass = ''`
    // is false: the guard is doing work, so it has to survive as an expression.
    const mss = mssOf([roadLayer({
        filter: ['==', ['coalesce', ['get', 'subclass'], ''], ''],
        paint: { 'line-color': '#ff0000' },
    })]);

    assert.ok(mss.includes('when('), mss);
    assert.ok(!mss.includes("[subclass = '']"), mss);
});

test('an exclusion is not proof of itself, so it stays in the filter it is the point of', () => {
    const mss = mssOf([roadLayer({
        filter: ['all',
            ['==', ['get', 'class'], 'motorway'],
            ['!=', ['get', 'subclass'], 'junction']],
        paint: { 'line-color': ['match', ['get', 'class'], 'motorway', '#f00', '#fff'] },
    })]);

    assert.deepEqual(selectors(mss), [
        "#transportation[class = 'motorway'][subclass != 'junction']::road {",
    ]);
    assert.ok(mss.includes('line-color: #f00;'), mss);
});

test('a case whose condition the filter settles keeps only the branch it takes', () => {
    const mss = mssOf([roadLayer({
        filter: ['==', ['get', 'class'], 'motorway'],
        paint: {
            'line-width': ['case',
                ['==', ['get', 'class'], 'path'], 1,
                ['==', ['get', 'brunnel'], 'bridge'], 4,
                2],
        },
    })]);

    // The class test is settled and drops out; the bridge test is not and survives.
    assert.ok(!mss.includes("'path'"), mss);
    assert.ok(mss.includes("[brunnel] = 'bridge'"), mss);
});
