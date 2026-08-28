import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { dropMissingFieldTests, mapSourceLayer, withMappingFilter } from '../dist/mapbox2css/schema.js';

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

test('a source layer that only exists as a class becomes the layer plus that class test', () => {
    // MapTiler planet_v4 splits by SOURCE LAYER what OpenMapTiles splits by a `class` field.
    assert.deepEqual(mapSourceLayer('forest', 'openmaptiles').mapping,
        { layer: 'landcover', filter: ['==', ['get', 'class'], 'wood'] });
    assert.deepEqual(mapSourceLayer('water', 'openmaptiles').mapping, { layer: 'water' });
});

test('a layer with no equivalent is dropped with a reason, never guessed at', () => {
    // A wrong guess draws the wrong features, which is worse than drawing none and much harder to
    // notice, so the report names it instead.
    const { mapping, why } = mapSourceLayer('archipelago_label', 'openmaptiles');
    assert.equal(mapping, null);
    assert.match(why, /archipelago/);
});

test('the class test is ANDed into the layer\'s own filter, not replacing it', () => {
    const layer = { id: 'l', type: 'line', 'source-layer': 'ferry', filter: ['==', ['get', 'brunnel'], 'bridge'] };
    assert.deepEqual(
        withMappingFilter(layer, mapSourceLayer('ferry', 'openmaptiles').mapping),
        ['all', ['==', ['get', 'class'], 'ferry'], ['==', ['get', 'brunnel'], 'bridge']]);
});

test('a test on a field the target schema does not have is dropped, and the filter simplified', () => {
    // MapTiler gates every place label on iso_a2; OpenMapTiles place carries class/name/rank only,
    // so the test can only fail and the layer draws nothing at all - strictly worse than not
    // testing. Substituting a bare `true` is not enough: ["all", true, ...] is not a filter and the
    // whole layer is then dropped as malformed, which is exactly what happened first.
    const dropped = [];
    const out = dropMissingFieldTests(
        ['all', ['==', ['geometry-type'], 'Point'], ['all', ['==', ['get', 'class'], 'hamlet'], ['has', 'iso_a2']]],
        'place', (f) => dropped.push(f));
    assert.deepEqual(dropped, ['iso_a2']);
    assert.deepEqual(out, ['all', ['==', ['geometry-type'], 'Point'], ['==', ['get', 'class'], 'hamlet']]);
});

test('a filter that collapses to false says so, so the layer can be dropped', () => {
    assert.equal(dropMissingFieldTests(['all', ['!has', 'iso_a2'], ['==', ['get', 'class'], 'city']], 'place', () => {}),
        false);
    // A field the target DOES have is left alone.
    const untouched = ['==', ['get', 'class'], 'city'];
    assert.deepEqual(dropMissingFieldTests(untouched, 'place', () => {
        throw new Error('should not drop');
    }), untouched);
});

test('the whole style is retargeted, project list included', () => {
    const style = {
        layers: [
            { id: 'w', type: 'fill', source: 's', 'source-layer': 'forest', paint: { 'fill-color': '#0f0' } },
            { id: 'g', type: 'fill', source: 's', 'source-layer': 'grass', paint: { 'fill-color': '#8f8' } },
        ],
    };
    const { mss, project } = convert(style, TABLE, { schema: 'openmaptiles' });
    // Both land on landcover, each keeping the class its own layer used to mean - and as a
    // bracketed predicate, not a when(), because a one-value class test is an equality.
    assert.match(mss, /#landcover\[class = 'wood'\]::w/);
    assert.match(mss, /#landcover\[class = 'grass'\]::g/);
    assert.ok(!mss.includes('#forest'));
    // One project entry, not two.
    assert.deepEqual(JSON.parse(project).layers, ['landcover']);
});

test('a source-layer MapBox interleaves is drawn at each of its depths', () => {
    const style = {
        layers: [
            { id: 'a', type: 'fill', source: 's', 'source-layer': 'forest', paint: { 'fill-color': '#0f0' } },
            { id: 'b', type: 'fill', source: 's', 'source-layer': 'water', paint: { 'fill-color': '#00f' } },
            { id: 'c', type: 'fill', source: 's', 'source-layer': 'grass', paint: { 'fill-color': '#8f8' } },
        ],
    };
    // forest and grass both become landcover with water drawn between them, and a bare project
    // entry pulls both attachments together - so landcover has to appear twice, each entry naming
    // the attachment it draws. The list is the draw order REVERSED (loadMapProject inserts at the
    // front), so `a` is last here.
    const { project, coverage } = convert(style, TABLE, { schema: 'openmaptiles' });
    assert.deepEqual(JSON.parse(project).layers, ['landcover::c', 'water', 'landcover::a']);
    assert.match(coverage.report(), /drawn at more than one depth/);
});

test('without the option nothing is retargeted', () => {
    const style = { layers: [{ id: 'w', type: 'fill', source: 's', 'source-layer': 'forest', paint: { 'fill-color': '#0f0' } }] };
    assert.match(convert(style, TABLE).mss, /#forest::w/);
});
