import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

import { convert } from '../dist/mapbox2css/index.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const TABLE = JSON.parse(readFileSync(join(HERE, '..', 'src', 'generated', 'properties.json'), 'utf8'));

const fill = (id, colour, extra = {}) => ({
    id, type: 'fill', 'source-layer': id.replace(/\W/g, '_'), paint: { 'fill-color': colour, ...extra },
});
const line = (id, colour, extra = {}) => ({
    id, type: 'line', 'source-layer': 'transportation', paint: { 'line-color': colour, ...extra },
});
const label = (id, font, extra = {}) => ({
    id, type: 'symbol', 'source-layer': 'place', layout: { 'text-field': '{name}', 'text-font': [font] }, paint: extra,
});

const run = (layers, background) => convert(
    { name: 'T', layers: background ? [{ id: 'bg', type: 'background', paint: { 'background-color': background } }, ...layers] : layers },
    TABLE);

test('a colour used by one layer is named after that layer and its role', () => {
    const { mss, variables } = run([fill('Glacier', '#ffffff'), line('Ferry', '#0088cc')]);
    assert.match(variables, /@glacier_fill: #ffffff;/);
    assert.match(variables, /@ferry_stroke: #0088cc;/);
    assert.match(mss, /polygon-fill: @glacier_fill;/);
    assert.match(mss, /line-color: @ferry_stroke;/);
});

test('a colour several layers share is named by what their ids have in common', () => {
    // The prefix first, then - when there is none - the token most of them carry. Without the
    // second rule the grey seven railway layers share came out as `@line_stroke_2`, which tells a
    // palette reader nothing about which grey it is.
    const { variables } = run([
        line('Minor road', '#cccccc'), line('Minor road bridge', '#cccccc'),
        line('Tunnel railway', '#bbbbbb'), line('Minor railway', '#bbbbbb'), line('Railway bridge', '#bbbbbb'),
    ]);
    assert.match(variables, /@minor_road_stroke: #cccccc;/);
    assert.match(variables, /@railway_stroke: #bbbbbb;/);
});

test('a colour with nothing in common falls back to the property, and collisions get a suffix', () => {
    const { variables } = run([line('Alpha', '#111111'), line('Beta', '#111111'), line('Gamma', '#222222'), line('Delta', '#222222')]);
    assert.match(variables, /@line_stroke: #111111;/);
    assert.match(variables, /@line_stroke_2: #222222;/);
});

test('a font is named by the face, not by the layer that happens to use it', () => {
    // What a variant swaps is the family. `@text_face_name_4` said nothing about which face it was.
    const { mss, variables } = run([label('City', 'Roboto Medium'), label('Town', 'Roboto Medium'), label('Peak', 'Roboto Italic')]);
    assert.match(variables, /@font_roboto_medium: 'Roboto Medium';/);
    assert.match(variables, /@font_roboto_italic: 'Roboto Italic';/);
    assert.match(mss, /text-face-name: @font_roboto_medium;/);
});

test('the Map background names itself, whatever else shares its colour', () => {
    const { mss, variables } = run([fill('Sand', '#f0f0f0')], '#f0f0f0');
    assert.match(variables, /@background: #f0f0f0;/);
    assert.match(mss, /Map \{\n\s+background-color: @background;\n\}/);
});

test('a colour inside a zoom ramp is hoisted, the ramp is not', () => {
    // The ramp is the style's animation and means nothing in a palette; its stops are the palette.
    const { mss, variables } = run([line('Contour', { stops: [[8, '#aa4400'], [16, '#663300']] })]);
    assert.match(mss, /line-color: linear\(\[view::zoom\], \(8, @contour_stroke\), \(16, @contour_stroke_2\)\);/);
    assert.match(variables, /@contour_stroke: #aa4400;/);
});

test('a spelling difference is not a colour difference', () => {
    // `hsl(0, 0%, 100%)` and `hsl(0,0%,100%)` are one colour, and a palette with both in it would
    // let a variant recolour half its map.
    const { variables } = run([fill('A', 'hsl(0, 0%, 100%)'), fill('B', 'hsl(0,0%,100%)')]);
    assert.equal(variables.match(/^@.*hsl/gm).length, 1);
});

test('only a shared size is hoisted, and never one that restates the default', () => {
    const shared = { 'line-width': 3 };
    const { mss, variables } = run([line('A', '#111', shared), line('B', '#222', shared), line('C', '#333', { 'line-width': 7, 'line-opacity': 1 })]);
    assert.match(variables, /@line_stroke_width: 3;/);
    assert.ok(!variables.includes(': 7;'), 'a width used once is left where a reader can see it');
    assert.ok(!/@\w*opacity/.test(variables), 'an opacity of 1 is the default and says nothing');
    assert.match(mss, /line-width: @line_stroke_width;/);
});

test('a placement priority is not a size, however often it repeats', () => {
    // Structural floats outnumbered the real ones: `@airport_labels_placement_priority: 9400000`
    // is not something a variant author tunes.
    const priced = (id) => ({ id, type: 'symbol', 'source-layer': 'place',
        layout: { 'text-field': '{name}', 'symbol-sort-key': 5 } });
    const { variables } = run([priced('A'), priced('B')]);
    assert.ok(!/priority/.test(variables ?? ''), 'a priority has no place in a palette');
});

test('the palette is listed first in the project, because the first declaration wins', () => {
    const { project } = run([fill('Glacier', '#ffffff')]);
    assert.deepEqual(JSON.parse(project).styles, ['variables.mss', 'style.mss']);
});

test('turning it off leaves the literals where they were', () => {
    const { mss, project, variables } = convert({ layers: [fill('Glacier', '#ffffff')] }, TABLE, { variables: false });
    assert.equal(variables, null);
    assert.match(mss, /polygon-fill: #ffffff;/);
    assert.deepEqual(JSON.parse(project).styles, ['style.mss']);
});

/**
 * The one check that matters: a palette is only correct if it compiles to the SAME map. Skipped
 * when massif-style is not built, so `npm test` still runs without a toolchain.
 */
test('the palette compiles to the same mapnik XML as the literals it replaced', (t) => {
    const binary = process.env.MASSIF_STYLE_BIN;
    if (!binary) {
        t.skip('set MASSIF_STYLE_BIN to the massif-style binary to run this');
        return;
    }
    const style = JSON.parse(readFileSync(join(HERE, 'fixtures', 'style.json'), 'utf8'));
    const compile = (options) => {
        const { mss, project, variables } = convert(style, TABLE, options);
        const dir = mkdtempSync(join(tmpdir(), 'massif-palette-'));
        writeFileSync(join(dir, 'style.mss'), mss);
        writeFileSync(join(dir, 'project.json'), project);
        if (variables) writeFileSync(join(dir, 'variables.mss'), variables);
        execFileSync(binary, ['css2xml', join(dir, 'project.json'), join(dir, 'style.xml')], { stdio: 'pipe' });
        return readFileSync(join(dir, 'style.xml'), 'utf8');
    };
    assert.equal(compile({}), compile({ variables: false }));
});
