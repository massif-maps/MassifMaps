import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';
import { detectSourceSchema, dropMissingFieldTests, mapSourceLayer, remapValues, retargetLayer, withMappingFilter } from '../dist/mapbox2css/schema.js';

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

test('a source layer that only exists as a class becomes the layer plus that class test', () => {
    // MapTiler planet_v4 splits by SOURCE LAYER what OpenMapTiles splits by a `class` field.
    assert.deepEqual(mapSourceLayer('forest', 'openmaptiles', 'maptiler').mappings,
        [{ layer: 'landcover', filter: ['==', ['get', 'class'], 'wood'] }]);
    assert.deepEqual(mapSourceLayer('water', 'openmaptiles', 'maptiler').mappings, [{ layer: 'water' }]);
});

test('a layer with no equivalent is dropped with a reason, never guessed at', () => {
    // A wrong guess draws the wrong features, which is worse than drawing none and much harder to
    // notice, so the report names it instead.
    const { mappings, why } = mapSourceLayer('archipelago_label', 'openmaptiles', 'maptiler');
    assert.deepEqual(mappings, []);
    assert.match(why, /archipelago/);
});

test('the class test is ANDed into the layer\'s own filter, not replacing it', () => {
    const layer = { id: 'l', type: 'line', 'source-layer': 'ferry', filter: ['==', ['get', 'brunnel'], 'bridge'] };
    assert.deepEqual(
        withMappingFilter(layer, mapSourceLayer('ferry', 'openmaptiles', 'maptiler').mappings[0]),
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
        'place', 'maptiler', (f) => dropped.push(f));
    assert.deepEqual(dropped, ['iso_a2']);
    assert.deepEqual(out, ['all', ['==', ['geometry-type'], 'Point'], ['==', ['get', 'class'], 'hamlet']]);
});

test('a filter that collapses to false says so, so the layer can be dropped', () => {
    assert.equal(dropMissingFieldTests(['all', ['!has', 'iso_a2'], ['==', ['get', 'class'], 'city']], 'place', 'maptiler', () => {}),
        false);
    // A field the target DOES have is left alone.
    const untouched = ['==', ['get', 'class'], 'city'];
    assert.deepEqual(dropMissingFieldTests(untouched, 'place', 'maptiler', () => {
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

test('the source vocabulary is read off the style, and a tie is refused rather than guessed', () => {
    assert.equal(detectSourceSchema(['place_label', 'poi_label', 'road']), 'mapbox');
    assert.equal(detectSourceSchema(['forest', 'city_label', 'water']), 'maptiler');
    // Only names that belong to one vocabulary count; the shared ones say nothing.
    assert.equal(detectSourceSchema(['water', 'waterway', 'building']), null);
    assert.throws(() => convert({ layers: [
        { id: 'w', type: 'fill', source: 's', 'source-layer': 'water', paint: { 'fill-color': '#00f' } },
    ] }, TABLE, { schema: 'openmaptiles' }), /--source-schema/);
});

test('a MapBox class value becomes what OpenMapTiles calls it, in the filter and in the paint', () => {
    const layer = {
        id: 'r', type: 'line', 'source-layer': 'road',
        filter: ['==', ['get', 'class'], 'street'],
        paint: { 'line-color': ['match', ['get', 'class'], ['street', 'service'], '#fff', '#ccc'] },
    };
    const out = retargetLayer(layer, mapSourceLayer('road', 'openmaptiles', 'mapbox').mappings[0],
                              'mapbox', { dropped: () => {}, approximate: () => {} });
    // A style picks a road's colour with a match on the same field it filters on, so both move.
    // No `street_link` exists on the MapBox side, so `minor` needs no ramp clause of its own.
    assert.deepEqual(out.filter, ['==', ['get', 'class'], 'minor']);
    assert.deepEqual(out.paint['line-color'],
        ['match', ['get', 'class'], ['minor', 'service'], '#fff', '#ccc']);
});

test('a ramp is a second field there, so a _link class becomes class + ramp', () => {
    // And the base class has to EXCLUDE ramps, or every link is drawn twice - once at link width
    // and once at motorway width.
    const link = { id: 'l', type: 'line', 'source-layer': 'road', filter: ['==', ['get', 'class'], 'motorway_link'] };
    const out = retargetLayer(link, mapSourceLayer('road', 'openmaptiles', 'mapbox').mappings[0],
                              'mapbox', { dropped: () => {}, approximate: () => {} });
    assert.deepEqual(out.filter, ['all', ['==', ['get', 'class'], 'motorway'], ['==', ['get', 'ramp'], 1]]);
});

test('one MapBox layer feeds several OpenMapTiles ones, and sorts itself between them', () => {
    // natural_label is water names, peaks and continents at once; each target keeps only the
    // classes it can carry, and the mapping whose filter collapses is not emitted at all.
    const style = {
        layers: [
            { id: 'w', type: 'symbol', source: 's', 'source-layer': 'natural_label',
              filter: ['match', ['get', 'class'], ['bay', 'sea'], true, false],
              layout: { 'text-field': ['get', 'name'] } },
            { id: 'p', type: 'symbol', source: 's', 'source-layer': 'natural_label',
              filter: ['==', ['get', 'class'], 'landform'],
              layout: { 'text-field': ['get', 'name'] } },
        ],
    };
    const { project } = convert(style, TABLE, { schema: 'openmaptiles', sourceSchema: 'mapbox' });
    assert.deepEqual(JSON.parse(project).layers, ['mountain_peak', 'water_name']);
});

test('a road and its name come off one MapBox layer and two OpenMapTiles ones', () => {
    const style = {
        layers: [
            { id: 'line', type: 'line', source: 's', 'source-layer': 'road', paint: { 'line-color': '#fff' } },
            { id: 'name', type: 'symbol', source: 's', 'source-layer': 'road',
              layout: { 'text-field': ['get', 'name'] } },
        ],
    };
    const { project } = convert(style, TABLE, { schema: 'openmaptiles', sourceSchema: 'mapbox' });
    assert.deepEqual(JSON.parse(project).layers, ['transportation_name', 'transportation']);
});

test('a field the target names differently is renamed everywhere, filter and expression alike', () => {
    const layer = {
        id: 'b', type: 'fill-extrusion', 'source-layer': 'building',
        filter: ['>', ['get', 'height'], 10],
        paint: { 'fill-extrusion-height': ['get', 'height'], 'fill-extrusion-base': ['get', 'min_height'] },
    };
    const out = retargetLayer(layer, mapSourceLayer('building', 'openmaptiles', 'mapbox').mappings[0],
                              'mapbox', { dropped: () => {}, approximate: () => {} });
    assert.deepEqual(out.filter, ['>', ['get', 'render_height'], 10]);
    assert.deepEqual(out.paint['fill-extrusion-height'], ['get', 'render_height']);
    assert.deepEqual(out.paint['fill-extrusion-base'], ['get', 'render_min_height']);
});

test('a comparison on a field the tiles do not carry is answered true, not false', () => {
    // filterrank gates every MapBox Standard POI. Answering false draws nothing at all and reads
    // as a broken style; answering true draws too much, which is visible and is reported.
    const dropped = [];
    const out = dropMissingFieldTests(
        ['all', ['<=', ['get', 'filterrank'], 2], ['==', ['geometry-type'], 'Point']],
        'poi', 'mapbox', (f) => dropped.push(f));
    assert.deepEqual(dropped, ['filterrank']);
    assert.deepEqual(out, ['==', ['geometry-type'], 'Point']);
});

test('remapValues drops a match branch whose every label is gone, and keeps the default', () => {
    assert.equal(remapValues(['match', ['get', 'class'], ['disputed_country'], true, false],
                             { class: { country: 'country', '*': null } }), false);
});

test('a missing-field test buried in another expression is dropped too, and the branch folded', () => {
    // MapBox puts the worldview test in the OUTPUT of the class match, not beside it. Stopping at
    // the boolean combinators left it standing, and since no OpenMapTiles tileset carries
    // `worldview` every country label vanished.
    const dropped = [];
    const out = dropMissingFieldTests(
        ['match', ['get', 'class'], ['country'],
            ['case', ['has', '$localized'], true,
                ['match', ['get', 'worldview'], ['all', 'US'], true, false]],
            false],
        'place', 'mapbox', (f) => dropped.push(f));
    assert.deepEqual(dropped, ['worldview']);
    // Both branches now answer true, so the whole thing folds back to the class test - which is a
    // bracketed CartoCSS predicate rather than a per-feature when().
    assert.deepEqual(out, ['match', ['get', 'class'], ['country'], true, false]);
});

test('a value-picking match becomes a case when a label needs the ramp field', () => {
    // A road's WIDTH is `match class [motorway] 3.2 ... [motorway_link] 0.8`. On a tileset where a
    // ramp IS a motorway the link branch is unreachable, so every ramp drew at full motorway width;
    // a match cannot carry a predicate as a label, so it has to become a case.
    const layer = {
        id: 'r', type: 'line', 'source-layer': 'road',
        paint: { 'line-width': ['match', ['get', 'class'], ['motorway'], 3.2, ['motorway_link'], 0.8, 0] },
    };
    const out = retargetLayer(layer, mapSourceLayer('road', 'openmaptiles', 'mapbox').mappings[0],
                              'mapbox', { dropped: () => {}, approximate: () => {} });
    assert.deepEqual(out.paint['line-width'], ['case',
        ['all', ['==', ['get', 'class'], 'motorway'], ['!=', ['get', 'ramp'], 1]], 3.2,
        ['all', ['==', ['get', 'class'], 'motorway'], ['==', ['get', 'ramp'], 1]], 0.8,
        0]);
});

test('a oneway is a string on one side and a number on the other', () => {
    // Without this the arrow layers filter on a value the tiles never hold and no oneway arrow is
    // drawn at all.
    const layer = { id: 'a', type: 'symbol', 'source-layer': 'road', filter: ['==', ['get', 'oneway'], 'true'] };
    const out = retargetLayer(layer, mapSourceLayer('road', 'openmaptiles', 'mapbox').mappings[0],
                              'mapbox', { dropped: () => {}, approximate: () => {} });
    assert.deepEqual(out.filter, ['==', ['get', 'oneway'], 1]);
});

test('an arrow goes to transportation and a name to transportation_name', () => {
    // Both are symbol layers on `road`; only the one that writes TEXT belongs in the name layer.
    // An arrow needs the road geometry and the `oneway` field, which live in `transportation`.
    const [line, name] = mapSourceLayer('road', 'openmaptiles', 'mapbox').mappings;
    assert.equal(line.when({ id: 'a', type: 'symbol', layout: { 'icon-image': 'oneway' } }), true);
    assert.equal(name.when({ id: 'a', type: 'symbol', layout: { 'icon-image': 'oneway' } }), false);
    assert.equal(name.when({ id: 'n', type: 'symbol', layout: { 'text-field': ['get', 'name'] } }), true);
});

test('a pinned field folds the branch it gated, rather than leaving a null in a concat', () => {
    // MapBox spells a shield's sprite `coalesce(concat(shield_beta, "-", reflen), ...)`. A literal
    // null left in that concat reached the SDK as `concat(null, '-')` - "Unsupported binary
    // operation", which fails the WHOLE stylesheet, not just the layer.
    const layer = {
        id: 's', type: 'symbol', 'source-layer': 'road',
        layout: {
            'text-field': ['get', 'ref'],
            'icon-image': ['case', ['has', 'shield_beta'],
                ['concat', ['get', 'shield_beta'], '-', ['to-string', ['get', 'reflen']]],
                ['concat', ['get', 'shield'], '-', ['to-string', ['get', 'reflen']]]],
        },
    };
    const name = mapSourceLayer('road', 'openmaptiles', 'mapbox').mappings[1];
    const out = retargetLayer(layer, name, 'mapbox', { dropped: () => {}, approximate: () => {} });
    assert.deepEqual(out.layout['icon-image'], ['concat', 'default', '-', '4']);
});

test('transit stops keep a class test in place of the fields OpenMapTiles lacks', () => {
    // `mode` and `stop_type` are all MapBox selects transit with; dropped, the layer labelled every
    // POI in the transit style and drew over the real one.
    const [transit] = mapSourceLayer('transit_stop_label', 'openmaptiles', 'mapbox').mappings;
    const out = retargetLayer({ id: 't', type: 'symbol', 'source-layer': 'transit_stop_label',
                                filter: ['==', ['get', 'mode'], 'rail'] },
                              transit, 'mapbox', { dropped: () => {}, approximate: () => {} });
    assert.ok(JSON.stringify(out.filter).includes('railway'));
});
