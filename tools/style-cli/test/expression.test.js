import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Untranslatable, expandTokens, translateExpression } from '../dist/mapbox2css/expression.js';
import { translateFilter, zoomPredicates } from '../dist/mapbox2css/filter.js';

test('literals', () => {
    assert.equal(translateExpression(3), '3');
    assert.equal(translateExpression('#ff0000'), '#ff0000');
    assert.equal(translateExpression('motorway'), "'motorway'");
    assert.equal(translateExpression(true), 'true');
});

test('field and zoom access', () => {
    assert.equal(translateExpression(['get', 'name']), '[name]');
    assert.equal(translateExpression(['zoom']), '([view::zoom] - 1)');
    assert.equal(translateExpression(['has', 'name']), '([name] != null)');
});

test('interpolate over zoom becomes a per-frame linear()', () => {
    assert.equal(
        translateExpression(['interpolate', ['linear'], ['zoom'], 6, 1, 16, 12]),
        'linear(([view::zoom] - 1), (6, 1), (16, 12))',
    );
    // exponential base 1 IS linear, so it is accepted.
    assert.equal(
        translateExpression(['interpolate', ['exponential', 1], ['zoom'], 0, 0, 1, 1]),
        'linear(([view::zoom] - 1), (0, 0), (1, 1))',
    );
});

test('interpolate over anything but zoom is refused', () => {
    assert.throws(
        () => translateExpression(['interpolate', ['linear'], ['get', 'w'], 0, 0, 1, 1]),
        Untranslatable,
    );

});

test('an exponential ramp is carried as one, not resampled and not flattened to linear', () => {
    // The SDK interpolates the base itself now (mapnikvt InterpolateExpression, EXPONENTIAL), so
    // the curve is handed over whole rather than approximated by extra stops. Flattening it to a
    // plain linear is out by about a third at the midpoint at base 2, and dropping the property
    // left a MapTiler pathway drawing as a fat solid grey line instead of a thin dashed one.
    assert.equal(translateExpression(['interpolate', ['exponential', 2], ['zoom'], 10, 0, 14, 8]),
        'exponential(2, ([view::zoom] - 1), (10, 0), (14, 8))');
    // base 1 IS linear, so it stays a plain linear with no base to carry.
    assert.equal(translateExpression(['interpolate', ['exponential', 1], ['zoom'], 10, 0, 14, 8]),
        'linear(([view::zoom] - 1), (10, 0), (14, 8))');
});

test('a slice compared to a literal is a prefix test, which CartoCSS spells as a regex', () => {
    // CartoCSS has no substring, but `=~` is a FULL std::regex_match, so a prefix is `D.*`.
    // It has to be the OPERATOR: `.match(...)` parses in mapnikvt's expression grammar but not in
    // the CartoCSS one, and the style then fails to load at all. Every country-specific road shield in MapTiler streets-v4 is gated on one
    // of these, and without it they all fell through to the fallback colour: French D-roads drew
    // on a white plate where MapTiler draws yellow.
    assert.equal(translateExpression(['==', ['slice', ['get', 'ref'], 0, 1], 'D']), "([ref] =~ 'D.*')");
    assert.equal(translateExpression(['in', ['slice', ['get', 'ref'], 0, 1], ['literal', ['N', 'M']]]),
        "([ref] =~ '(N|M).*')");
    assert.equal(translateExpression(['!=', ['slice', ['get', 'ref'], 0, 3], 'BR-']), "(!([ref] =~ 'BR-.*'))");
    // A literal that cannot equal a slice of that length is simply false.
    assert.equal(translateExpression(['==', ['slice', ['get', 'ref'], 0, 1], 'AB']), 'false');
    // Regex metacharacters in style data must lose their meaning.
    assert.equal(translateExpression(['==', ['slice', ['get', 'ref'], 0, 1], '.']), "([ref] =~ '\\..*')");
    // A slice that is not a prefix has no regex form and is still refused.
    assert.throws(() => translateExpression(['==', ['slice', ['get', 'ref'], 1, 3], 'x']));
});

test('step keeps its stops', () => {
    assert.equal(
        translateExpression(['step', ['zoom'], 1, 10, 2, 14, 6]),
        'step(([view::zoom] - 1), (0, 1), (10, 2), (14, 6))',
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

test('or is ||, but and CANNOT be && - the grammar makes it unparseable', () => {
    assert.equal(
        translateExpression(['any', ['==', ['get', 'a'], 1], ['==', ['get', 'b'], 2]]),
        '(([a] = 1) || ([b] = 2))',
    );
    // CartoCSSParser term3 has `qi::lit("&") > unary` for bitwise and, and `>` is an expectation:
    // on '&&' it eats the first '&', demands a unary, meets the second and fails with no backtrack.
    assert.equal(
        translateExpression(['all', ['==', ['get', 'a'], 1], ['==', ['get', 'b'], 2]]),
        '(([a] = 1) ? ([b] = 2) : false)',
    );
});

test('legacy stop functions become per-frame interpolation', () => {
    assert.equal(
        translateExpression({ stops: [[7, '#d0d0d0'], [11, '#dddddd']] }),
        'linear(([view::zoom] - 1), (7, #d0d0d0), (11, #dddddd))',
    );
    assert.equal(
        translateExpression({ type: 'interval', stops: [[5, 1], [9, 2]] }),
        'step(([view::zoom] - 1), (5, 1), (9, 2))',
    );
});

test('a stop function over a feature property is a lookup, never an interpolation', () => {
    assert.equal(
        translateExpression({ property: 'class', type: 'categorical', stops: [['a', 1], ['b', 2]], default: 0 }),
        "([class] = 'a' ? 1 : ([class] = 'b' ? 2 : 0))",
    );
    assert.throws(
        () => translateExpression({ property: 'w', stops: [[0, 1], [1, 2]] }),
        Untranslatable,
    );
});

test('geometry-type compares against the decoder number, not the MapBox name', () => {
    assert.equal(
        translateExpression(['==', ['geometry-type'], 'Polygon']),
        '([mapnik::geometry_type] = 3)',
    );
});

test('the legacy {token} text-field form becomes field references', () => {
    assert.equal(expandTokens('{height}'), '[height]');
    assert.equal(expandTokens('{ref} {name}'), "concat(concat([ref], ' '), [name])");
    assert.equal(expandTokens('plain'), "'plain'");
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

test('the expression spelling of a comparison is bracketed too, not wrapped in when()', () => {
    // A bracketed predicate is a plain filter the decoder decides per rule; when() carries an
    // expression it evaluates per FEATURE. Every modern style is written in expressions, so
    // leaving these to when() taxed the whole style: 389 when()s on MapTiler topo-v4, 191 after.
    assert.deepEqual(translateFilter(['==', ['get', 'class'], 'motorway']), ["[class = 'motorway']"]);
    assert.deepEqual(translateFilter(['!=', ['get', 'rank'], 3]), ['[rank != 3]']);
    assert.deepEqual(translateFilter(['==', ['geometry-type'], 'LineString']), ["['mapnik::geometry_type' = 2]"]);
    assert.deepEqual(translateFilter(['>=', ['get', 'rank'], 2]), ['[rank >= 2]']);
    // Two fields compared to each other has no bracketed form and still needs when().
    assert.match(translateFilter(['==', ['get', 'a'], ['get', 'b']])[0], /^when\(/);
});

test('a match used as a boolean is an or-chain, not a ternary', () => {
    // MapTiler spells "class is one of these" as ["match", input, [...], true, false]; the generic
    // match translation wrapped it in `? true : false`, which the decoder re-evaluates per feature.
    assert.deepEqual(
        translateFilter(['match', ['get', 'class'], ['minor', 'service'], true, false]),
        ["when(([class] = 'minor' || [class] = 'service'))"],
    );
    // One label is just an equality, so it brackets.
    assert.deepEqual(
        translateFilter(['match', ['get', 'class'], ['minor'], true, false]),
        ["[class = 'minor']"],
    );
    // A match that yields anything other than true/false is still a ternary - it is a real match.
    assert.match(translateFilter(['match', ['get', 'class'], ['minor'], 1, 0])[0], /\? 1 : 0/);
});

test('the in OPERATOR is not the in FILTER, and a style using it must not be dropped', () => {
    // ["in", needle, ["literal", [...]]] is the expression operator; ["in", key, v1, v2] is the
    // legacy filter. Only the second was handled, so a layer whose filter used the first was
    // dropped whole - MapTiler streets-v4 lost every minor-road FILL that way and drew the outline
    // alone, which reads as grey roads.
    assert.deepEqual(
        translateFilter(['in', ['get', 'class'], ['literal', ['track', 'service']]]),
        ["when(([class] = 'track' || [class] = 'service'))"]);
    // A one-element haystack is an equality, so it brackets like any other.
    assert.deepEqual(translateFilter(['in', ['get', 'class'], ['literal', ['track']]]),
        ["when(([class] = 'track'))"]);
    assert.match(translateExpression(['case', ['in', ['get', 'class'], ['literal', ['a', 'b']]], '#f00', '#00f']),
        /\(\[class\] = 'a' \|\| \[class\] = 'b'\)/);
    assert.equal(translateExpression(['in', ['get', 'class'], ['literal', []]]), 'false');
    // A haystack that is not a literal array has no CartoCSS form and is still refused - a bare
    // array would swallow ["get", "class"], whose elements are strings too.
    assert.throws(() => translateExpression(['in', ['get', 'a'], ['get', 'b']]));
    assert.throws(() => translateExpression(['in', ['get', 'a'], 'substring']));
});

test('maxzoom is exclusive, and both ends carry the zoom shift', () => {
    // A MapBox zoom is one level lower than the SDK's - see ZOOM_OFFSET.
    assert.deepEqual(zoomPredicates(6, 20), ['[zoom >= 7]', '[zoom < 21]']);
    assert.deepEqual(zoomPredicates(undefined, undefined), []);
});

test('a type assertion carries only its value, and several are a coalesce', () => {
    // MapBox uses `number`/`string`/`boolean` to prove a type to its own checker. CartoCSS is not
    // typed, so the assertion means nothing and only the value survives. Refusing them dropped
    // poi-label's whole FILTER - `["number", ["get", "filterrank"]]` - and with it every POI.
    assert.equal(translateExpression(['number', ['get', 'filterrank']]), '[filterrank]');
    assert.equal(translateExpression(['string', ['get', 'name']]), '[name]');
    assert.equal(translateExpression(['number', ['get', 'a'], ['get', 'b']]), '([a] ?? [b])');
});

test('arithmetic is variadic, and a lone minus is negation', () => {
    // `["+", a, b, c]` is a sum, not a malformed pair. poi-label's filter adds three terms.
    assert.equal(translateExpression(['+', 1, 2, 3]), '(1 + 2 + 3)');
    assert.equal(translateExpression(['*', ['get', 'a'], 2, 3]), '([a] * 2 * 3)');
    // No unary minus before a parenthesised value in the grammar.
    assert.equal(translateExpression(['-', ['get', 'a']]), '(0 - [a])');
});

test('sqrt goes through pow, there being no sqrt in CartoCSS', () => {
    assert.equal(translateExpression(['sqrt', ['get', 'a']]), 'pow([a], 0.5)');
});

test('cubic-bezier is an easing, not CartoCSS cubic, which is a spline', () => {
    // MapBox's cubic-bezier eases BETWEEN each pair of stops; CartoCSS's `cubic` is a spline
    // THROUGH all of them and cglib's overshoots. Standard sizes its settlement labels this way,
    // and taken as `cubic` "Paris" went 38 -> 71 device pixels over one zoom level where the
    // easing moves 20.0 -> 20.3. Linear is within about a unit and cannot overshoot.
    const out = translateExpression(
        ['interpolate', ['cubic-bezier', 0.2, 0, 0.9, 1], ['zoom'], 3, 13, 6, 18, 8, 20, 15, 24]);
    assert.match(out, /^linear\(/);
    assert.ok(!out.includes('cubic('), 'never the spline');
});

test('a ramp at the stop of another ramp collapses: CartoCSS cannot nest two', () => {
    // Mapbox Standard writes its water fill as a zoom ramp whose far stop is a brightness ramp.
    // The nested form parses and then draws NOTHING - every lake came out empty on device.
    const notes = [];
    const out = translateExpression(['interpolate', ['linear'], ['zoom'],
        13, '#aaddff',
        14, ['interpolate', ['linear'], ['measure-light', 'brightness'], 0, '#004466', 0.02, '#aaddff']], notes);
    assert.ok(!/linear\([^)]*linear\(/.test(out), out);
    assert.match(out, /view::brightness/);
    assert.match(notes.join(' '), /cannot nest/);
});
