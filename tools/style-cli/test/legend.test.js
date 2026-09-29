/*
 * `massif-style legend`: the C++ resolver run on a real CartoCSS compile (skipped without
 * MASSIF_STYLE_BIN), and the SVG sheet drawn from what it returns.
 */

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { labelText, renderLegendSvg } from '../dist/legend-svg.js';

const PROJECT = join(dirname(fileURLToPath(import.meta.url)), 'fixtures', 'legend', 'project.json');

function resolve(t, ...params) {
    const binary = process.env.MASSIF_STYLE_BIN;
    if (!binary) {
        t.skip('set MASSIF_STYLE_BIN to the massif-style binary to run this');
        return null;
    }
    const args = ['legend', ...params.flatMap((p) => ['--params', p]), PROJECT];
    const legend = JSON.parse(execFileSync(binary, args, { encoding: 'utf8' }));
    return {
        sections: legend.sections.map((s) => s.id),
        items: Object.fromEntries(legend.sections.flatMap((s) => s.items).map((i) => [i.id, i])),
    };
}

test('each item resolves to the swatch the compiled style draws it with', (t) => {
    const legend = resolve(t);
    if (!legend) return;
    const { items } = legend;
    assert.deepEqual(items.motorway.lines, [{ color: '#8c8c8c', width: 8 }, { color: '#e8a0a0', width: 6 }], 'casing, then fill');
    assert.deepEqual(items.primary.lines.map((l) => l.width), [6, 4], 'a line-border is a wider line under it');
    assert.deepEqual(items['track-grade3'].lines[0].dasharray, [6, 2]);
    assert.equal(items['sac-t1'].lines[0].opacity, 0.8);
    assert.deepEqual(items['mtb-hard'].lines, [{ color: '#000000', width: 1, offset: 3 }], 'the attachment keeps the overlay alone');
    assert.deepEqual([items.wood.kind, items.wood.color, items.wood.pattern], ['fill', '#c8e6b0', 'icons/forest.svg']);
    assert.deepEqual([items.shelter.kind, items.shelter.icon], ['poi', 'icons/shelter.svg']);
    assert.equal(items.spring.marker.color, '#1e88e5');
    assert.equal(items.peak.kind, 'poi', 'a name beside an icon is a POI, not a shield');
    assert.deepEqual([items['shield-interstate'].kind, items['shield-interstate'].icon, items['shield-interstate'].text.value],
        ['shield', 'icons/interstate.svg', '95'], 'a road number on its image');
    assert.deepEqual(items['shield-departmental'].plate, { color: '#f7d117', border: '#ffffff', borderWidth: 1.5, radius: 3 }, 'a road number on a plate');
    assert.equal(items.city.kind, 'label');
});

test('style parameters reach the legend', (t) => {
    const legend = resolve(t, 'trail_color=#00ff00', 'show_mtb=0', 'eink=1');
    if (!legend) return;
    assert.equal(legend.items['sac-t1'].lines[1].color, '#00ff00');
    assert.ok(!legend.sections.includes('mtb'), 'a section its parameter switches off is gone');
    assert.deepEqual([legend.items.water.color, legend.items.water.outline.color], ['#ffffff', '#000000']);
});

const RESOLVED = {
    title: 'Legend',
    sections: [
        { label: { en: 'Roads', fr: 'Routes' }, items: [
            { id: 'motorway', label: 'Motorway', kind: 'line', lines: [
                { color: '#8296b0', width: 8 },
                { color: '#a1b0c4', width: 6, dasharray: [4, 2] },
            ] },
        ] },
        { label: 'Land', items: [
            { id: 'water', label: 'Water', kind: 'fill', color: '#99ddff', opacity: 0.5, pattern: 'icons/water.png' },
        ] },
        { label: 'Road numbers', items: [
            { id: 'n7', label: 'National road', kind: 'shield', text: { value: 'N 7', color: '#ffffff' },
                plate: { color: '#c0392b', radius: 3 } },
        ] },
    ],
};

test('the SVG sheet draws each swatch with its resolved paint', () => {
    const svg = renderLegendSvg(RESOLVED, { lang: 'fr', imageHref: (file) => `data:${file}` });
    assert.match(svg, /^<svg /);
    assert.match(svg, />Routes</, 'a localised section label in the asked language');
    assert.match(svg, /stroke="#8296b0" stroke-width="8"[^>]*\/><line [^>]*stroke="#a1b0c4"[^>]*stroke-dasharray="4,2"/, 'lines bottom to top');
    assert.match(svg, /fill="#99ddff" fill-opacity="0.5"/);
    assert.match(svg, /href="data:icons\/water.png"/, 'images go through imageHref');
    assert.match(svg, /rx="3" fill="#c0392b"/, 'a plate');
    assert.match(svg, />N 7</, 'with its number');
});

test('labels pick a language, then fall back to the first', () => {
    assert.equal(labelText({ en: 'Roads', fr: 'Routes' }, 'fr'), 'Routes');
    assert.equal(labelText({ en: 'Roads', fr: 'Routes' }, 'de'), 'Roads');
    assert.equal(labelText('Water'), 'Water');
});
