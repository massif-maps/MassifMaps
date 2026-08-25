import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Untranslatable, translateExpression } from '../dist/mapbox2css/expression.js';
import { translateFilter, zoomPredicates } from '../dist/mapbox2css/filter.js';

test('literals', () => {
    assert.equal(translateExpression(3), '3');
    assert.equal(translateExpression('#ff0000'), '#ff0000');
    assert.equal(translateExpression('motorway'), "'motorway'");
    assert.equal(translateExpression(true), 'true');
});

test('field and zoom access', () => {
    assert.equal(translateExpression(['get', 'name']), '[name]');
    assert.equal(translateExpression(['zoom']), '[view::zoom]');
    assert.equal(translateExpression(['has', 'name']), '([name] != null)');
});

test('interpolate over zoom becomes a per-frame linear()', () => {
    assert.equal(
        translateExpression(['interpolate', ['linear'], ['zoom'], 6, 1, 16, 12]),
        'linear([view::zoom], (6, 1), (16, 12))',
    );
    // exponential base 1 IS linear, so it is accepted.
    assert.equal(
        translateExpression(['interpolate', ['exponential', 1], ['zoom'], 0, 0, 1, 1]),
        'linear([view::zoom], (0, 0), (1, 1))',
    );
});

test('interpolate over anything but zoom is refused', () => {
    assert.throws(
        () => translateExpression(['interpolate', ['linear'], ['get', 'w'], 0, 0, 1, 1]),
        Untranslatable,
    );
    assert.throws(
        () => translateExpression(['interpolate', ['exponential', 2], ['zoom'], 0, 0, 1, 1]),
        Untranslatable,
    );
});

test('step keeps its stops', () => {
    assert.equal(
        translateExpression(['step', ['zoom'], 1, 10, 2, 14, 6]),
        'step([view::zoom], (0, 1), (10, 2), (14, 6))',
    );
});

test('case and match become ternaries', () => {
    assert.equal(
        translateExpression(['case', ['==', ['get', 'a'], 1], 'x', 'y']),
        "(([a] = 1) ? 'x' : 'y')",
    );
    assert.equal(
        translateExpression(['match', ['get', 'class'], 'motorway', 1, ['trunk', 'primary'], 2, 0]),
        "(([class] = 'motorway') ? 1 : (([class] = 'trunk' || [class] = 'primary') ? 2 : 0))",
    );
});

test("MapBox '^' becomes pow, because CartoCSS '^' is XOR", () => {
    assert.equal(translateExpression(['^', 2, 3]), 'pow(2, 3)');
});

test('concat folds into the binary CartoCSS concat', () => {
    assert.equal(
        translateExpression(['concat', ['get', 'a'], '-', ['get', 'b']]),
        "concat(concat([a], '-'), [b])",
    );
});

test("CartoCSS booleans are && and ||, not 'and'/'or'", () => {
    assert.equal(
        translateExpression(['all', ['==', ['get', 'a'], 1], ['==', ['get', 'b'], 2]]),
        '(([a] = 1) && ([b] = 2))',
    );
});

test('unsupported operators are refused, not guessed', () => {
    assert.throws(() => translateExpression(['feature-state', 'hover']), Untranslatable);
    assert.throws(() => translateExpression(['within', {}]), Untranslatable);
    assert.throws(() => translateExpression(['number-format', 1, {}]), Untranslatable);
    assert.throws(() => translateExpression(['abs', -1]), Untranslatable);
    // No '%' in the CartoCSS grammar at all.
    assert.throws(() => translateExpression(['%', 5, 2]), Untranslatable);
});

test('legacy filters become bracketed predicates', () => {
    assert.deepEqual(translateFilter(['==', 'class', 'motorway']), ["[class = 'motorway']"]);
    assert.deepEqual(translateFilter(['has', 'name']), ['[name != null]']);
    // A namespaced field has to be QUOTED in a predicate: the grammar there is (fieldid | string).
    assert.deepEqual(translateFilter(['==', '$type', 'LineString']), ["['mapnik::geometry_type' = 2]"]);
    assert.deepEqual(
        translateFilter(['all', ['==', 'a', 1], ['!=', 'b', 2]]),
        ['[a = 1]', '[b != 2]'],
    );
});

test('an or-filter falls back to when(), which is a top-level predicate', () => {
    assert.deepEqual(translateFilter(['any', ['==', 'a', 1], ['==', 'b', 2]]), [
        'when((([a] = 1) || ([b] = 2)))',
    ]);
});

test('maxzoom is exclusive', () => {
    assert.deepEqual(zoomPredicates(6, 20), ['[zoom >= 6]', '[zoom < 20]']);
    assert.deepEqual(zoomPredicates(undefined, undefined), []);
});
