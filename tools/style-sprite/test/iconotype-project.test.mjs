import assert from 'node:assert/strict';
import { test } from 'node:test';

import { PUA_FIRST, PUA_SIZE, codepointOf, hashCodepoint, project } from '../iconotype-project.mjs';

const icon = (name) => ({ name, paths: ['M0 0H1024V1024Z'] });

test('a codepoint comes from the name alone, inside the Private Use Area', () => {
    const code = hashCodepoint('restaurant');
    assert.equal(code, hashCodepoint('restaurant'));
    assert.ok(code >= PUA_FIRST && code < PUA_FIRST + PUA_SIZE);
    assert.equal(codepointOf(['cafe', 'restaurant']).get('restaurant'), codepointOf(['restaurant']).get('restaurant'),
        'adding an icon moves no other');
});

test('a collision probes to the next free slot, and a kept code never moves', () => {
    const [a, b] = findCollision();
    const fresh = codepointOf([a, b]);
    assert.notEqual(fresh.get(a), fresh.get(b));
    // b was there first: a new a that hashes onto it takes the next slot instead
    const kept = codepointOf([a, b], new Map([[b, hashCodepoint(b)]]));
    assert.equal(kept.get(b), hashCodepoint(b));
    assert.equal(kept.get(a), PUA_FIRST + ((hashCodepoint(a) - PUA_FIRST + 1) % PUA_SIZE));
});

test('an icon whose SVG is gone stays in the project, unselected, holding its code', () => {
    const before = project('Test', [icon('bar'), icon('cafe')], null);
    const after = project('Test', [icon('cafe')], before);
    const bar = after.icons.find((i) => i.name === 'bar');
    assert.equal(bar.selected, false);
    assert.equal(bar.code, before.icons.find((i) => i.name === 'bar').code);
    assert.deepEqual(after.icons.map((i) => i.code), [...after.icons.map((i) => i.code)].sort(), 'sorted by code');
    assert.equal(after.output.styles[0].path, 'Test.json');
});

function findCollision() {
    const seen = new Map();
    for (let i = 0; ; i++) {
        const name = `icon${i}`;
        const code = hashCodepoint(name);
        if (seen.has(code)) return [seen.get(code), name];
        seen.set(code, name);
    }
}
